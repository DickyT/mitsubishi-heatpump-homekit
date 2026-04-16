# mitsubishi-heatpump-homekit

ESP32-based Mitsubishi CN105 heat pump bridge, now being rebuilt on **ESP-IDF + Espressif ESP HomeKit SDK**.

## Current Status

Current baseline: **M0-M4 platform + minimal WebUI foundation complete**.

[`PLAN.md`](./PLAN.md) now uses Milestone numbering for future work. Older "Phase" labels refer only to historical checkpoints and should not be used as the forward-looking implementation order.

Completed baseline:

- Old Arduino/HomeSpan implementation has been removed from `main`
- `main` is now an ESP-IDF project skeleton
- Componentized platform services are in place: logging, SPIFFS, CN105 UART, and STA-only Wi-Fi
- CN105 UART is initialized as `UART1 RX=GPIO26 TX=GPIO32 2400 8E1`
- SPIFFS is mounted on a custom 4MB flash partition table
- ESP-IDF logs are mirrored to `/spiffs/latest.log` after filesystem mount
- Wi-Fi power save is disabled; Wi-Fi failure stays in STA/reconnect mode and does not start a fallback AP
- Minimal ESP-IDF WebUI is available on port `80`
- `GET /`, `GET /api/health`, and `GET /api/status` are available for platform verification
- The migration target is ESP-IDF + Espressif `esp-homekit-sdk`, not HomeSpan or Matter

Not restored yet:

- CN105 protocol core and mock state
- real CN105 transport
- HomeKit SDK integration

The previous working Arduino implementation is preserved in git history and branches for reference.

## Current Baseline Verification

Verify that the platform and minimal WebUI foundation is healthy before CN105 protocol, real transport, or HomeKit SDK logic is brought back.

The current baseline is considered healthy when:

- it builds successfully
- it flashes successfully
- serial output shows the custom 4MB partition table
- serial output shows SPIFFS mounted
- serial output shows persistent logging enabled at `/spiffs/latest.log`
- serial output shows CN105 UART initialized on `rx=26 tx=32`
- serial output shows STA Wi-Fi connected, or STA reconnect attempts if Wi-Fi is unavailable
- heartbeat logs include Wi-Fi mode, IP, RSSI, MAC, and last event
- `http://<esp-ip>/` loads
- `http://<esp-ip>/api/health` returns JSON
- `http://<esp-ip>/api/status` returns JSON with Wi-Fi, SPIFFS, and CN105 UART status

## Repository Layout

- [`CMakeLists.txt`](./CMakeLists.txt): ESP-IDF project root
- [`main/app_main.cpp`](./main/app_main.cpp): app bootstrap entrypoint
- [`components/app_config`](./components/app_config): centralized compile-time config
- [`components/platform_fs`](./components/platform_fs): SPIFFS mount and filesystem stats
- [`components/platform_log`](./components/platform_log): ESP-IDF log setup and persistent log mirroring
- [`components/platform_uart`](./components/platform_uart): CN105 UART setup
- [`components/platform_wifi`](./components/platform_wifi): Wi-Fi STA-only setup and network heartbeat status
- [`components/web`](./components/web): minimal ESP-IDF HTTP/WebUI foundation
- [`partitions.csv`](./partitions.csv): custom 4MB flash partition table
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

Then the current baseline can be tested with the usual flow:

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

For quieter Codex/tool runs, use `--quiet-first`. It captures the first `idf.py` run and only reruns with verbose output if the quiet attempt fails:

```bash
./build.py --quiet-first build
./build.py --quiet-first flash-auto --no-build
```

The project default flash baud is `115200` for the current M5Stack/ESP32 board.
`serial-log` also overwrites an ignored local copy at `serial_logs/latest-serial.log` by default.

## Local Wi-Fi Config

Wi-Fi credentials are intentionally kept out of git. To test STA mode locally:

```bash
cp components/app_config/include/app_config_local.example.h components/app_config/include/app_config_local.h
```

Then edit `components/app_config/include/app_config_local.h`:

```cpp
#define APP_WIFI_SSID "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

If no real SSID is configured, the device stays offline and does not start a fallback AP. If STA connection fails, it keeps retrying in STA mode.

Expected serial output:

- custom partition table with `factory` and `spiffs`
- `SPIFFS mounted`
- `Persistent log enabled: /spiffs/latest.log`
- `Initializing CN105 UART: uart=1 rx=26 tx=32 baud=2400 format=8E1`
- `WiFi power save disabled`
- `WebUI ready: http://<esp-ip>:80/`
- either `Connected to ...` or reconnect/offline STA status
- a repeating platform heartbeat every 5 seconds with Wi-Fi status

## Upstream Reference

- Upstream reference: [echavet/MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome)
- HomeKit SDK target: [espressif/esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk)
- HomeKit SDK common examples: [esp-homekit-sdk/examples/common](https://github.com/espressif/esp-homekit-sdk/tree/master/examples/common)

## Next Planned Step

M5: Reintroduce the CN105 offline core and mock state:

- add `components/core_cn105`
- restore protocol constants, checksum, SET payload builder, response parser, and state model
- keep Fahrenheit as the Web/HomeKit-facing temperature unit
- add mock state so Web/API can build, decode, and apply CN105 packets without a real air-conditioner connection
