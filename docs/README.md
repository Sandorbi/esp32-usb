# Capability-test control page

Static HTML/CSS/JavaScript with no build step. Keep all four web assets together: `index.html`, `style.css`, `app.mjs`, and `transport.mjs`.

## Publish / update

Commit and push to the branch GitHub Pages uses. With Pages configured as **main /docs**, deployment is automatic. Open the deployed HTTPS URL in Chrome on Android and refresh after deployment completes.

Upload the new ESP32 firmware separately through PlatformIO. The webpage does not flash firmware.

## Connect and demonstrate

1. Connect the phone to the ESP32 with the working USB data cable.
2. Select **Android phone · WebUSB**, tap **Connect board**, select CP2102 and allow access.
3. Tap STOP and check the log for `OK: STOP; all LEDs OFF`.
4. Select intensity 1–5, then START. This sends `START_SEQUENCE <intensity>`.
5. A/B/C come on at 0/400/900 ms and all switch off at 2000 ms.
6. Start again and press STOP before completion to show immediate interruption.
7. Tap **Set safe · 1000**, then START. While the LEDs are on, tap **Set high · 3000**. The next 200 ms firmware check cuts off the outputs without a STOP command.
8. Set the sensor below 2500 before another normal run. The number field accepts any whole value from 0 to 4095; tap **Set sensor value** to send it.

The sensor controls send `SET_SENSOR <value>`. That command only changes the simulated input; the ESP32 independently checks it and performs the cutoff. The app log shows firmware acknowledgments/errors; a successful USB write alone is not proof that a command was accepted. The value field is an entry field, not live sensor telemetry.

Disconnect does not send STOP. A running sequence still ends at 2 seconds and the firmware continues checking its sensor without the app.

## Local computer preview

Run `node tools/serve-web.mjs` from the project root and open `http://localhost:8080`. Select **Computer · Web Serial** in Chrome/Edge and close PlatformIO's serial monitor first. A phone must use the HTTPS site; the laptop's LAN HTTP address is not a secure context.

## Troubleshooting

- Empty chooser: check OTG and the data cable. WebUSB selects Silicon Labs VID `10c4`, PID `ea60`.
- Claim/permission failure: close other serial apps. Use Web Serial on a computer to retain its existing serial driver.
- No response: confirm the firmware is uploaded, press EN/reset if necessary, and try STOP again.
- Old timing or unknown command: upload the capability-test firmware and refresh the hosted page.
- Unsafe input: set a value below 2500, then explicitly start again.

[Chrome WebUSB guide](https://developer.chrome.com/docs/capabilities/usb) · [Web Serial guide](https://developer.chrome.com/docs/capabilities/serial) · [Silicon Labs CP210x protocol](https://www.silabs.com/documents/public/application-notes/AN571.pdf)
