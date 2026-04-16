# mitsubishi-heatpump-homekit

ESP32-based Mitsubishi CN105 heat pump bridge, now being rebuilt on **ESP-IDF + esp-matter**.

## Current Status

This repository is at **Phase 3** of the migration:

- Old Arduino/HomeSpan implementation has been removed from `main`
- `main` is now an ESP-IDF project with component-based platform services
- CN105 UART is initialized as `UART1 RX=GPIO26 TX=GPIO32 2400 8E1`
- SPIFFS is mounted on a custom 4MB flash partition table
- ESP-IDF logs are mirrored to `/spiffs/latest.log` after filesystem mount
- Wi-Fi is initialized through a reusable ESP-IDF component with power save disabled

The previous working Arduino implementation is preserved in git history and branches for reference.

## Phase 3 Goal

Verify that the ESP-IDF platform foundation can bring up networking before WebUI, CN105 protocol, or Matter logic is brought back.

Phase 3 is considered complete when:

- it builds successfully
- it flashes successfully
- serial output shows the custom 4MB partition table
- serial output shows SPIFFS mounted
- serial output shows persistent logging enabled at `/spiffs/latest.log`
- serial output shows CN105 UART initialized on `rx=26 tx=32`
- serial output shows either STA Wi-Fi connected or fallback AP started
- heartbeat logs include Wi-Fi mode, IP, RSSI, MAC, and last event

## Repository Layout

- [`CMakeLists.txt`](./CMakeLists.txt): ESP-IDF project root
- [`main/app_main.cpp`](./main/app_main.cpp): app bootstrap entrypoint
- [`components/app_config`](./components/app_config): centralized compile-time config
- [`components/platform_fs`](./components/platform_fs): SPIFFS mount and filesystem stats
- [`components/platform_log`](./components/platform_log): ESP-IDF log setup and persistent log mirroring
- [`components/platform_uart`](./components/platform_uart): CN105 UART setup
- [`components/platform_wifi`](./components/platform_wifi): Wi-Fi STA/AP setup and network heartbeat status
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

Then this phase can be tested with the usual flow:

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

If no real SSID is configured, the device starts a fallback AP named `Mitsubishi-Setup` with password `12345678`.

Expected serial output:

- custom partition table with `factory` and `spiffs`
- `SPIFFS mounted`
- `Persistent log enabled: /spiffs/latest.log`
- `Initializing CN105 UART: uart=1 rx=26 tx=32 baud=2400 format=8E1`
- `WiFi power save disabled`
- either `Connected to ...` or `Fallback AP started`
- a repeating Phase 3 heartbeat every 5 seconds with Wi-Fi status

## Upstream Reference

- Upstream reference: [echavet/MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome)

## Next Planned Step

Reintroduce the minimal HTTP/WebUI foundation:

- HTTP server on port 80
- simple health endpoint
- small status page driven by the Wi-Fi/platform status components
