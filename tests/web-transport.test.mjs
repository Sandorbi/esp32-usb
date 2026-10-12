import test from 'node:test';
import assert from 'node:assert/strict';
import { CP2102, lineDecoder, SerialConnection } from '../docs/transport.mjs';
function device() {
  return {
    opened: false, controls: [], writes: [],
    configuration: { interfaces: [{ interfaceNumber: 2, alternates: [{ alternateSetting: 1, endpoints: [{ type: 'bulk', direction: 'in', endpointNumber: 3, packetSize: 64 }, { type: 'bulk', direction: 'out', endpointNumber: 4 }] }] }] },
    async open() { this.opened = true; }, async close() { this.opened = false; },
    async claimInterface(n) { assert.equal(n, 2); },
    async selectAlternateInterface(n, a) { assert.equal(n, 2); assert.equal(a, 1); },
    async controlTransferOut(setup, bytes) { this.controls.push([setup, bytes]); return { status: 'ok' }; },
    async transferOut(endpoint, bytes) { this.writes.push([endpoint, bytes]); return { status: 'ok', bytesWritten: bytes.length }; },
    async transferIn(endpoint, size) { assert.equal(endpoint, 3); assert.equal(size, 64); return { status: 'ok', data: new DataView(new Uint8Array([79, 75, 10]).buffer) }; }
  };
}
test('CP2102 sets 115200 8N1 and uses descriptor endpoints for commands and replies', async () => {
  const usb = device(), port = new CP2102(usb);
  await port.open();
  const baud = usb.controls.find(([s]) => s.request === 0x1e)[1];
  assert.equal(new DataView(baud.buffer).getUint32(0, true), 115200);
  assert.equal(usb.controls.find(([s]) => s.request === 3)[0].value, 0x0800);
  assert.ok(usb.controls.every(([s]) => s.index === 2));
  await port.write(new TextEncoder().encode('STATUS\n'));
  assert.equal(usb.writes[0][0], 4);
  assert.equal(new TextDecoder().decode(usb.writes[0][1]), 'STATUS\n');
  assert.equal(new TextDecoder().decode(await port.read()), 'OK\n');
  await port.close(); assert.equal(usb.opened, false);
});
test('failed setup closes the device; partial command and stalled read fail explicitly', async () => {
  const usb = device(), port = new CP2102(usb);
  usb.controlTransferOut = async () => ({ status: 'stall' });
  await assert.rejects(port.open(), /setup request/); assert.equal(usb.opened, false);
  const other = device(), ready = new CP2102(other); await ready.open();
  other.transferOut = async () => ({ status: 'ok', bytesWritten: 1 });
  await assert.rejects(ready.write(new TextEncoder().encode('STOP\n')), /not fully sent/);
  other.transferIn = async () => ({ status: 'stall' });
  await assert.rejects(ready.read(), /USB read failed/);
  await ready.close();
});
test('line framing handles split USB packets, CRLF, Unicode, and bounded noise', () => {
  const lines = [], decode = lineDecoder(line => lines.push(line));
  for (const text of ['Sequence: RU', 'NNING\r', '\nSafety: ACTIVE\r\n']) decode(new TextEncoder().encode(text));
  const unicode = new TextEncoder().encode('é\n');
  decode(unicode.slice(0, 1)); decode(unicode.slice(1));
  assert.deepEqual(lines, ['Sequence: RUNNING', 'Safety: ACTIVE', 'é']);
  decode(new TextEncoder().encode('x'.repeat(5000)));
  assert.ok(lines.at(-1).endsWith('[truncated]'));
});
test('serial disconnect cancels a pending reader and releases locks before closing', async () => {
  const events = [];
  const port = {
    async open(options) { assert.equal(options.baudRate, 115200); },
    async setSignals(signals) { assert.equal(signals.requestToSend, false); },
    readable: { getReader: () => ({ async cancel() { events.push('cancel'); }, releaseLock() { events.push('reader unlocked'); } }) },
    writable: { getWriter: () => ({ releaseLock() { events.push('writer unlocked'); } }) },
    async close() { events.push('closed'); }
  };
  const serial = new SerialConnection(port); await serial.open(); await serial.close();
  assert.deepEqual(events, ['cancel', 'reader unlocked', 'writer unlocked', 'closed']);
});
