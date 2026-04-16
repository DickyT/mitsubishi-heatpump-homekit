# mitsubishi-heatpump-homekit

ESP32-based Mitsubishi CN105 heat pump bridge, now being rebuilt on **ESP-IDF + esp-matter**.

## Current Status

This repository has been reset to **Phase 0** of the migration:

- Old Arduino/HomeSpan implementation has been removed from `main`
- `main` is now a minimal ESP-IDF project skeleton
- The only runtime behavior is a simple `app_main()` hello-world style bootstrap with a heartbeat log

The previous working Arduino implementation is preserved in git history and branches for reference.

## Phase 0 Goal

Verify that the repository now works as a clean ESP-IDF project before any CN105, WebUI, filesystem, or Matter logic is brought back.

Phase 0 is considered complete when:

- the project opens correctly in VSCode with the ESP-IDF extension
- it builds successfully
- it flashes successfully
- serial monitor shows the bootstrap logs and repeating heartbeat

## Repository Layout

- [`CMakeLists.txt`](./CMakeLists.txt): ESP-IDF project root
- [`main/app_main.cpp`](./main/app_main.cpp): Phase 0 bootstrap entrypoint
- [`CODEX_GUIDE.md`](./CODEX_GUIDE.md): local project guide and hardware rules
- [`original_version`](./original_version): upstream MitsubishiCN105ESPHome reference as a submodule

## Hardware Assumptions

- ESP32 or M5Stack ATOM Lite class device
- Debug logs stay on the default serial console
- CN105 UART remains reserved for:
  - `GPIO26` as `RX`
  - `GPIO32` as `TX`
- ESP32 is externally powered by USB/5V

CN105 wiring target remains:

- `Pin2` -> `GND`
- `Pin4 (TX)` -> `ESP32 GPIO26 (RX)`
- `Pin5 (RX)` -> `ESP32 GPIO32 (TX)`

## Verification

This project uses the global EIM-managed ESP-IDF `v5.4.1` install at:

```text
/Users/dkt/.espressif/v5.4.1/esp-idf
```

You can activate the environment manually with:

```bash
source "/Users/dkt/.espressif/tools/activate_idf_v5.4.1.sh"
```

Then Phase 0 can be tested with the usual flow:

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

For this project, the preferred local wrapper is:

```bash
./build.py build
./build.py flash monitor
```

Or use the project-specific auto-flash helper:

```bash
./build.py flash-auto --monitor
./build.py flash-auto --port /dev/cu.usbserial-xxxx --monitor
./build.py serial-log --seconds 15
```

The project default flash baud is `115200` for the current M5Stack/ESP32 board.
`serial-log` also overwrites an ignored local copy at `serial_logs/latest-serial.log` by default.

Expected serial output:

- startup banner mentioning `Phase 0`
- chip/flash information
- a repeating heartbeat every 5 seconds

## Upstream Reference

- Upstream reference: [echavet/MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome)

## Next Planned Step

Reintroduce the first layer of reusable platform infrastructure on ESP-IDF:

- structured logging
- filesystem mount
- Wi-Fi service
- CN105 UART service
