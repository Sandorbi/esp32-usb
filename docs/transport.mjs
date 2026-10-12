// CP2102 requests: Silicon Labs AN571, https://www.silabs.com/documents/public/application-notes/AN571.pdf
export const USB_FILTER = { vendorId: 0x10c4, productId: 0xea60 };

export class CP2102 {
  constructor(device) { this.device = device; }
  async control(request, value, data) {
    const result = await this.device.controlTransferOut({ requestType: 'vendor', recipient: 'interface', request, value, index: this.interfaceNumber }, data);
    if (result.status !== 'ok') throw new Error(`USB setup request ${request.toString(16)} failed: ${result.status}`);
  }
  async open() {
    try {
      await this.device.open();
      if (!this.device.configuration) await this.device.selectConfiguration(1);
      for (const iface of this.device.configuration.interfaces) {
        for (const alternate of iface.alternates) {
          const input = alternate.endpoints.find(e => e.type === 'bulk' && e.direction === 'in');
          const output = alternate.endpoints.find(e => e.type === 'bulk' && e.direction === 'out');
          if (!input || !output) continue;
          this.interfaceNumber = iface.interfaceNumber;
          this.input = input; this.output = output;
          await this.device.claimInterface(this.interfaceNumber);
          await this.device.selectAlternateInterface(this.interfaceNumber, alternate.alternateSetting);
          await this.control(0x00, 1); // Enable UART.
          const baud = new Uint8Array(4);
          new DataView(baud.buffer).setUint32(0, 115200, true);
          await this.control(0x1e, 0, baud);
          await this.control(0x03, 0x0800); // 8 data bits, no parity, 1 stop bit.
          await this.control(0x13, 0, new Uint8Array(16)); // No flow control; DTR/RTS inactive.
          await this.control(0x07, 0x0300); // Keep ESP32 reset/boot control lines deasserted.
          return;
        }
      }
      throw new Error('No compatible USB serial interface found.');
    } catch (error) { await this.close(); throw error; }
  }
  async read() {
    const result = await this.device.transferIn(this.input.endpointNumber, this.input.packetSize);
    if (result.status !== 'ok') throw new Error(`USB read failed: ${result.status}`);
    return result.data;
  }
  async write(bytes) {
    const result = await this.device.transferOut(this.output.endpointNumber, bytes);
    if (result.status !== 'ok' || result.bytesWritten !== bytes.byteLength) throw new Error('USB command was not fully sent. Reconnect the board.');
  }
  async close() {
    // Closing aborts an outstanding transferIn, so an idle board cannot block disconnect.
    if (this.device.opened) await this.device.close();
  }
}

export class SerialConnection {
  constructor(port) { this.port = port; }
  async open() {
    try {
      await this.port.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none' });
      await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });
      this.reader = this.port.readable.getReader();
      this.writer = this.port.writable.getWriter();
    } catch (error) { await this.close(); throw error; }
  }
  async read() { const result = await this.reader.read(); return result.done ? null : result.value; }
  async write(bytes) { await this.writer.write(bytes); }
  async close() {
    if (this.reader) { await this.reader.cancel().catch(() => {}); this.reader.releaseLock(); this.reader = null; }
    if (this.writer) { this.writer.releaseLock(); this.writer = null; }
    if (this.port.readable || this.port.writable) await this.port.close();
  }
}

export function lineDecoder(onLine) {
  const decoder = new TextDecoder();
  let pending = '';
  return bytes => {
    pending += decoder.decode(bytes, { stream: true });
    const lines = pending.split(/\r\n|\n|\r/);
    pending = lines.pop();
    for (const line of lines) if (line) onLine(line);
    if (pending.length > 4096) { onLine(pending.slice(0, 4096) + ' [truncated]'); pending = ''; }
  };
}
