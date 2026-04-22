# CN105 Probe App

This is a standalone minimal ESP-IDF debug app for CN105 UART experiments.

It is intentionally separate from the main Mitsubishi HomeKit firmware so it does **not** pull in:

- `esp-homekit-sdk`
- WebUI assets and HTTP server code
- SPIFFS/log/file-management components
- Wi-Fi and admin/maintenance logic

It only builds a tiny UART probe app that talks to:

- console UART at `115200 8N1`
- CN105 UART at `2400 8E1`

## Default Pinout

The default CN105 pin mapping in this debug app matches the current fixed Atom Lite cable test:

- `RX = GPIO32`
- `TX = GPIO26`

Typing `0` in the serial console swaps the mapping live to:

- `RX = GPIO26`
- `TX = GPIO32`

## Console Commands

Type one character in the serial monitor:

- `0`: swap CN105 RX/TX pins and reopen UART
- `1`: send `CONNECT 0x5A`
- `2`: send `CONNECT 0x5B`
- `3`: send one INFO request, cycling through `0x02 -> 0x03 -> 0x06 -> 0x09`
- `4`: send a debug `SET` packet for `ON + COOL + 75F + AUTO fan + AUTO vane + center wide vane`
- `5`: send one INFO burst: `0x02 + 0x03 + 0x06 + 0x09`
- `h` or `?`: print the menu again

## Build

From the repository root:

```bash
./build.py --app cn105-probe build
./build.py --app cn105-probe flash-auto --monitor
```

Or directly from this subproject with native ESP-IDF:

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Why This Exists

This app is for low-level CN105 debugging when we want to answer questions like:

- does the board really transmit and receive on the expected pins?
- does the indoor unit respond to `0x5A` or `0x5B`?
- is there any difference between the two pin directions?
- is the issue protocol, wiring, or logic-level compatibility?
