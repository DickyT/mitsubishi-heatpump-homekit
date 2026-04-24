# Installer / Probe App

This is a standalone ESP-IDF installer firmware for first-run setup and CN105
hardware validation.

It replaces the old serial-only CN105 probe. The app now provides:

- BLE Wi-Fi provisioning through Espressif's mobile provisioning app using
  Security 1 and PoP `abcd1234`.
- An installer WebUI on port `80` after Wi-Fi connects.
- CN105 auto-probing from the browser using user-selected RX/TX GPIO pins.
- LED GPIO color test for WS2812-style status LEDs.
- Full-overwrite writes to the formal firmware's `device_cfg` NVS namespace.
- OTA upload of the formal firmware app binary.

The installer uses the same OTA partition layout as the formal firmware:

- `nvs`
- `otadata`
- `phy_init`
- `ota_0`
- `ota_1`
- `spiffs`

## Build And Flash

From the repository root:

```bash
./build.py --app installer build
./build.py --app installer flash-auto --monitor
```

Every successful build also exports a packaged firmware set into:

```text
firmware_exports/<version>/
```

The four flash images are renamed as:

```text
<project_name>_<version>_<0xOFFSET>.bin
```

The legacy app alias still works:

```bash
./build.py --app cn105-probe build
```

## First-Run Flow

1. Flash the installer firmware.
2. Use Espressif's mobile provisioning app to connect over BLE.
3. Provision Wi-Fi using the BLE service name printed in serial logs, e.g.
   `PROV_MITSUBISHI_ABCDEF`.
4. In the Espressif app, use Security 1 with Proof of Possession `abcd1234`.
5. Open `http://<installer-ip>/`.
6. Run CN105 auto-probe with the physical RX/TX GPIO pins.
7. Test the status LED GPIO if needed.
8. Save step 1, then save step 2 so the installer writes NVS explicitly twice.
9. Upload the formal firmware app binary from the exported package:
   `firmware_exports/<version>/mitsubishi_heatpump_homekit_<version>_0x20000.bin`
10. Click `Reboot and Apply OTA`.

After reboot, the formal firmware reads the NVS values written by the installer
and starts normally with the same partition table.

## CN105 Probe Notes

The auto-probe walks:

- `2400 / 4800 / 9600`
- push-pull
- push-pull + RX pullup
- open-drain + RX pullup
- open-drain + TX/RX pullup
- CONNECT `0x5A` and `0x5B`

Only legal CN105 responses are treated as a positive match:

- `0x7A / 0x7B` connect acknowledgements
- `0x62` info responses
- `0x61` SET acknowledgements

Echoed request packets are counted as RX bytes but are not treated as a legal
CN105 response.
