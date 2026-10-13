import { CP2102, SerialConnection, USB_FILTER, lineDecoder } from './transport.mjs';
const $ = id => document.getElementById(id);
const buttons = [...document.querySelectorAll('[data-command]')];
let connection = null;
let busy = false;
let sending = false;
let replyTimer;
let received = 0;
function log(kind, message) {
  const lines = $('log').textContent.split('\n');
  lines.push(`${new Date().toLocaleTimeString()}  ${kind}  ${message}`);
  $('log').textContent = lines.slice(-300).join('\n');
  $('log').scrollTop = $('log').scrollHeight;
}
function notice(text, error = false) { $('notice').textContent = text; $('notice').classList.toggle('error', error); }
function supported() { return window.isSecureContext && !!navigator[$('transport').value]; }
function render() {
  $('connect').disabled = !!connection || busy || !supported();
  $('disconnect').disabled = !connection || busy || sending;
  $('transport').disabled = !!connection || busy;
  buttons.forEach(button => { button.disabled = !connection || busy || sending; });
  $('badge').textContent = connection ? 'Connected' : busy ? 'Connecting…' : 'Disconnected';
  $('badge').classList.toggle('connected', !!connection);
}
function checkSupport() {
  if (!window.isSecureContext) notice('Open this page over HTTPS (or localhost on a computer) to access USB.', true);
  else if (!supported()) notice('This browser does not support the selected method. Use Chrome on Android with WebUSB, or desktop Chrome/Edge with Web Serial.', true);
  else notice('Connect the board using a USB data cable, then tap Connect board.');
  render();
}
async function disconnect(message = 'Disconnected. The board still ends each sequence at 2 seconds and checks its safety input.') {
  const old = connection;
  connection = null; busy = true;
  clearTimeout(replyTimer); render();
  try { await old?.close(); }
  catch (error) { log('!', `Closing connection: ${error.message}`); }
  finally { busy = false; render(); notice(message); log('•', message); }
}
async function receive(active) {
  const decode = lineDecoder(line => { received++; clearTimeout(replyTimer); log('RX', line); });
  try {
    while (connection === active) {
      const bytes = await active.read();
      if (connection !== active) return;
      if (bytes === null) throw new Error('The serial port closed.');
      decode(bytes);
    }
  } catch (error) {
    if (connection === active) await disconnect(`Connection lost: ${error.message}`);
  }
}
$('connect').addEventListener('click', async () => {
  busy = true; render();
  let candidate;
  try {
    // Browser device selection must originate from a user gesture.
    candidate = $('transport').value === 'usb'
      ? new CP2102(await navigator.usb.requestDevice({ filters: [USB_FILTER] }))
      : new SerialConnection(await navigator.serial.requestPort({ filters: [{ usbVendorId: USB_FILTER.vendorId, usbProductId: USB_FILTER.productId }] }));
    await candidate.open();
    connection = candidate;
    notice('Connected at 115200 baud. Tap STOP to check the firmware response.');
    log('•', 'Connected. No command sent yet.');
    void receive(candidate);
  } catch (error) {
    await candidate?.close().catch(() => {});
    const message = error.name === 'NotFoundError' ? 'No device selected. Tap Connect board to try again.' : `Could not connect: ${error.message} Close other serial apps; on a computer use Web Serial.`;
    notice(message, true); log('!', message);
  } finally { busy = false; render(); }
});
$('disconnect').addEventListener('click', () => void disconnect());
for (const button of buttons) button.addEventListener('click', async () => {
  if (!connection || sending || busy) return;
  const active = connection;
  const before = received;
  const command = button.dataset.command === 'START_SEQUENCE'
    ? `START_SEQUENCE ${$('intensity').value}` : button.dataset.command;
  sending = true; render(); clearTimeout(replyTimer);
  try {
    await active.write(new TextEncoder().encode(command + '\n'));
    if (connection !== active) return;
    log('TX', command);
    notice(`${command} sent. Check the board log for its response.`);
    if (received === before) replyTimer = setTimeout(() => {
      if (connection === active) notice('No complete response received. Verify the firmware is uploaded and press STOP again. If needed, press the board’s EN/reset button.', true);
    }, 2500);
  } catch (error) {
    if (connection === active) await disconnect(`Send failed: ${error.message}`);
  } finally { sending = false; render(); }
});
$('clear').addEventListener('click', () => { $('log').textContent = ''; });
$('transport').addEventListener('change', checkSupport);
navigator.usb?.addEventListener('disconnect', event => {
  if (connection?.device === event.device) void disconnect('USB cable disconnected.');
});
if (navigator.serial) $('transport').value = 'serial';
checkSupport();
