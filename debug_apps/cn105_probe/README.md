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

Two hardware findings from this project are now baked into the probe logic:

- `CONNECT ACK (0x7A/0x7B)` must be framed using `byte[4] + 6`, not a fixed
  `8` bytes. A real unit can legitimately return `FC 7A 01 30 01 00 54`.
- On this Atom Lite setup, `RX_PULLUP` has been materially more useful than
  assuming TX open-drain alone would solve the link.

## Default Pinout

The default CN105 pin mapping in this debug app matches the current fixed Atom Lite cable test:

- `RX = GPIO26`
- `TX = GPIO32`

Typing `0` in the serial console swaps the mapping live to:

- `RX = GPIO32`
- `TX = GPIO26`

## Console Commands

Type one character in the serial monitor:

- `0`: swap CN105 RX/TX pins and reopen UART
- `1`: send `CONNECT 0x5A`
- `2`: send `CONNECT 0x5B`
- `3`: send one INFO request, cycling through `0x02 -> 0x03 -> 0x06 -> 0x09`
- `4`: send a debug `SET` packet for `POWER ON + COOL + 75F + AUTO fan + AUTO vane + center wide vane`
- `5`: send a minimal `POWER OFF` SET packet
- `6`: cycle electrical profile:
  - push-pull
  - push-pull + RX pullup
  - open-drain + RX pullup
  - open-drain + TX/RX pullup
- `7`: cycle CN105 baud rate through `2400 -> 4800 -> 9600`
- `8`: automatically detect the best pinout/baud/profile/connect combination and keep it active
- `9`: send one INFO burst: `0x02 + 0x03 + 0x06 + 0x09` using the current active config
- `h` or `?`: print the menu again

## Electrical Profiles

The `6` command reopens the CN105 UART and cycles through four pad configurations after `uart_set_pin()`:

- push-pull
- push-pull + RX internal pullup
- open-drain + RX internal pullup
- open-drain + TX/RX internal pullup

This is useful because some field reports point to `rx_pin: INPUT_PULLUP` being the important setting, while other experiments focus on open-drain TX behavior.

The auto-detect flow is designed to simulate arriving at an unknown indoor unit
with no prior assumptions. It walks:

- primary/swapped pinout
- `2400 / 4800 / 9600`
- all four electrical profiles
- both `0x5A` and `0x5B`

Then it keeps the best match active so you can immediately try `CONNECT`, `INFO`,
`POWER ON`, `POWER OFF`, and status queries against the detected config.

The internal pullups are good enough for low-speed experiments at `2400 8E1`, but an external pullup resistor is still the better long-term hardware solution if one of the open-drain-style profiles turns out to be the missing piece.

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
