# ESP32 USB test page

Plain HTML, CSS, and JavaScript; no packages, build step, or backend. The firmware in `src/main.cpp` is unchanged. Only START, STOP, and STATUS are sent, each followed by a newline. There is no polling or keep-alive.

## Open on your phone

Publish this `docs` folder on an HTTPS static host. For GitHub Pages, push these files to your repository, then in **Settings → Pages** choose **Deploy from a branch**, your branch, and **/docs**. Open the URL GitHub provides in Chrome on your Xiaomi. This task does not enable Pages or publish the repository.

1. Upload the existing firmware with PlatformIO.
2. Connect the board to your phone using a data cable / OTG adapter.
3. Keep **Android phone · WebUSB** selected, tap **Connect board**, choose CP2102, and allow USB access.
4. Tap **STATUS**: expect `Sequence: IDLE` and `Safety: OFF`.
5. Tap **START**: expect `OK: Hot flash started`, then `OK: Sequence complete` after about 4.38 seconds.
6. Without another command, expect `SAFETY TIMEOUT: All outputs OFF` about five seconds after START.
7. Try START followed immediately by STOP: expect `OK: All LEDs OFF`.
8. Unplug and reconnect to test recovery. Tap Connect again to reopen the device.

STATUS resets the firmware's safety timer. Disconnect does not send STOP; press STOP first if you want immediate shutdown. Neither a successful USB write nor the Connected badge confirms firmware execution: inspect RX responses.

## Computer preview

From the project root, run `node tools/serve-web.mjs`, then open `http://localhost:8080`. Use **Computer · Web Serial** in Chrome or Edge to communicate with the board through its existing serial driver. Close the PlatformIO serial monitor first. Android should use the hosted HTTPS URL: an ordinary laptop LAN HTTP address is not a secure context.

## Troubleshooting

- No chooser: use a supported browser and HTTPS, opening the page directly in a tab.
- No device: check the cable supports data and that the phone supports/enables USB host (OTG) mode. The WebUSB filter expects Silicon Labs VID `10c4`, PID `ea60`.
- Permission/claim error: close competing serial apps. Desktop USB drivers can reserve the interface; use Web Serial on a computer instead of replacing the driver.
- Connected, no response: verify firmware upload; tap the board's EN/reset button, wait for boot, and try STATUS. The connection does not deliberately reset the ESP32.
- Garbled boot output: some ROM boot messages can differ from the application's UART output; commands use 115200, 8N1.

Hardware compatibility must be verified with the actual Xiaomi, cable, and board. Mock tests cannot confirm USB host support, board wiring, or physical LED behavior.

## Verification and references

Run `node --test tests/web-transport.test.mjs` from the project root.

- [Chrome WebUSB documentation](https://developer.chrome.com/docs/capabilities/usb)
- [Chrome Web Serial documentation](https://developer.chrome.com/docs/capabilities/serial)
- [Silicon Labs CP210x protocol, AN571](https://www.silabs.com/documents/public/application-notes/AN571.pdf)
