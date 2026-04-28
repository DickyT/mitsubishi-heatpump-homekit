# Kiri Bridge Firmware Recovery

Use this guide when normal BLE WiFi provisioning is not enough, or when you
want to restore a Kiri Bridge to a known-good installer firmware.

This guide assumes you bought Kiri Bridge hardware. You do not need command
line tools or a local build environment.

## What You Need

- Kiri Bridge hardware kit
- USB data cable
- Chrome or another Web Serial capable browser
- Latest `.kiri` package from GitHub Releases

## Which Package Should I Use?

- `kiri_installer_<version>.kiri`: recovery and first-time restore path.
- `kiri_bridge_<version>.kiri`: normal production firmware.

If you are unsure, flash the latest installer first. The installer starts a BLE
provisioning flow and provides a WebUI that can install the production firmware
after WiFi is configured.

## Browser Recovery Flash

1. Download the latest `.kiri` package from GitHub Releases.
2. Open <https://kiri.dkt.moe/flash.html>.
3. Plug the Kiri Bridge into your computer over USB.
4. Click `Select Serial Device`.
5. Select the Kiri Bridge serial device.
6. Wait for the page to verify the ESP32 chip and MAC address.
7. Choose the `.kiri` firmware package.
8. Confirm that this is your Kiri Bridge hardware.
9. Click `Install Firmware / OTA`.
10. Wait for the write to finish.
11. When the success dialog appears, manually restart the Kiri Bridge by holding
    the side reset button, power cycling it, or plugging it into the heat pump.

The web flasher intentionally does not reboot the device automatically. This
avoids a Chrome/macOS Web Serial crash that can happen when USB serial devices
reset and re-enumerate immediately after flashing.

## After Flashing the Installer

1. Restart the Kiri Bridge.
2. Wait for the LED to rapidly blink blue.
3. Open <https://kiri.dkt.moe/ble_provisioning.html>.
4. Provision WiFi.
5. Open the installer portal shown by the provisioning page:

```text
http://<device-ip>:8080/
```

6. Follow the installer WebUI instructions to upload the current Kiri package.

## Safety Notes

- Only flash packages published for Kiri Bridge.
- The flasher verifies the package metadata and checksums before writing.
- The flasher verifies that the connected chip is an ESP32-class device before
  continuing.
- Do not unplug the Kiri Bridge while firmware is being written.
