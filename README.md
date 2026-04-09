# mitsubishi-heatpump-homekit

ESP32-based Mitsubishi CN105 heat pump bridge for HomeKit, developed in small, testable steps.

## Current Status

The project is currently in a staged build-out:

- Working local web app for remote-control prototyping
- Fahrenheit-first temperature UX for U.S. usage
- Mock/offline CN105 state model and draft payload preview
- Reference upstream codebase tracked as a git submodule at [`original_version`](./original_version)

The current web app includes:

- `/` virtual remote UI
- `/debug` HTTP and serial debug page
- `/api/status` mock device status endpoint
- `/api/remote/build` mock CN105 config preview endpoint

## Hardware Assumptions

- ESP32 or M5Stack ATOM Lite class device
- Debug logs stay on default `Serial`
- CN105 UART is reserved for:
  - `GPIO32` as `RX`
  - `GPIO26` as `TX`
- ESP32 is externally powered by USB/5V

CN105 wiring assumption:

- `Pin2` -> `GND`
- `Pin4 (TX)` -> `ESP32 GPIO32 (RX)`
- `Pin5 (RX)` -> `ESP32 GPIO26 (TX)`

## Repository Layout

- [`mitsubishi-homekit.ino`](./mitsubishi-homekit.ino): Arduino entrypoint
- [`src/`](./src): app config, web UI, mock protocol, temperature conversion helpers
- [`CODEX_GUIDE.md`](./CODEX_GUIDE.md): project guide and local rules for future implementation work
- [`original_version`](./original_version): upstream MitsubishiCN105ESPHome reference as a submodule

## Development Notes

- The project is currently optimized for Arduino IDE bring-up and real-device iteration.
- PlatformIO config is included for future use, but local board testing may still happen from Arduino IDE.
- Temperature behavior is Fahrenheit-first at the UI layer, while mock/device state still preserves raw Celsius values internally.
- Local WiFi credentials should live in [`src/AppSecrets.h`](./src/AppSecrets.h), using [`src/AppSecrets.example.h`](./src/AppSecrets.example.h) as the template. The real secrets file is gitignored.

## Upstream Reference

The reference implementation is tracked as a submodule:

- Upstream: [echavet/MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome)

## Next Planned Step

Build a real offline CN105 payload builder from the current virtual remote fields, then wire it into live UART communication once the CN105 cable is available.
