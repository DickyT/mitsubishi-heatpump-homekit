# mitsubishi-heatpump-homekit

ESP32-based Mitsubishi CN105 heat pump bridge, now being rebuilt on **ESP-IDF + Espressif ESP HomeKit SDK**.

## Current Status

Current baseline: **ESP-IDF platform, WebUI, CN105 offline core, HomeKit bridge, and real CN105 transport implementation are in place**.

[`PLAN.md`](./PLAN.md) now describes the repository in terms of current status and remaining validation work. Older `M7/M8` wording should be treated as historical migration context, not the current forward plan.

Completed baseline:

- Old Arduino/HomeSpan implementation has been removed from `main`
- `main` is now an ESP-IDF project skeleton
- Componentized platform services are in place: logging, SPIFFS, CN105 UART, and STA-only Wi-Fi
- CN105 UART is initialized as `UART1 RX=GPIO26 TX=GPIO32 2400 8E1`
- SPIFFS is mounted on a custom 4MB flash partition table
- ESP-IDF logs are mirrored to a per-boot SPIFFS log file after filesystem mount
- Wi-Fi power save is disabled; Wi-Fi failure stays in STA/reconnect mode and does not start a fallback AP
- ESP-IDF WebUI is available on port `8080`; HomeKit/HAP owns port `80`
- `GET /`, `GET /api/health`, and `GET /api/status` are available for platform verification
- WebUI pages use tiny HTML shells plus build-time gzip static asset fragments under `/assets/*`; the browser loader fetches fragments serially to avoid large chunked HTML responses and concurrent static asset bursts on ESP32
- CN105 offline protocol core is available with SET payload builder, packet decode, and mock state
- WebUI feature layer is available: `/` is the virtual remote, `/debug` is the raw decode/API console, `/logs` is log viewing/live tail, and `/files` is the SPIFFS file manager
- HomeKit SDK bridge is integrated and can run over the shared CN105 state model
- Project-facing temperature APIs use Fahrenheit; the protocol core converts to CN105 Celsius payload bytes internally
- The migration target is ESP-IDF + Espressif `esp-homekit-sdk`, not HomeSpan or Matter
- Real CN105 transport code is present and is now the default build mode; `mock` transport remains available as a fallback/dev mode

Not restored yet:

- final long-run hardware validation of the default real CN105 transport path

The previous working Arduino implementation is preserved in git history and branches for reference.

## Current Baseline Verification

Verify that the full ESP-IDF baseline is healthy with real CN105 transport enabled by default.

The current baseline is considered healthy when:

- it builds successfully
- it flashes successfully
- serial output shows the custom 4MB partition table
- serial output shows SPIFFS mounted
- serial output shows persistent logging enabled at a per-boot `/spiffs/*-log.txt` file
- serial output shows CN105 UART initialized on `rx=26 tx=32`
- serial output shows STA Wi-Fi connected, or STA reconnect attempts if Wi-Fi is unavailable
- heartbeat logs include Wi-Fi mode, IP, RSSI, MAC, and last event
- `http://<esp-ip>:8080/` loads
- `http://<esp-ip>:8080/api/health` returns JSON
- `http://<esp-ip>:8080/api/status` returns JSON with Wi-Fi, SPIFFS, CN105 UART status, and HomeKit status
- CN105 offline self-test logs `77F SET roundtrip`
- `GET http://<esp-ip>:8080/api/cn105/mock/build-set?...temperature_f=77...&apply=1` builds and applies a mock SET packet that reads back as `77F`
- `http://<esp-ip>:8080/debug` loads and raw decode returns JSON
- `http://<esp-ip>:8080/logs` shows SPIFFS log files and live current log output
- `http://<esp-ip>:8080/files` can list, download, upload, create, and delete SPIFFS files
- serial output shows HomeKit started with setup code and setup payload
- Apple Home can pair, then WebUI and HomeKit show the same mock power/mode/temperature state after refresh
- serial output shows CN105 connect/info polling and the WebUI runtime mode reflects `真实 CN105`

## Repository Layout

- [`CMakeLists.txt`](./CMakeLists.txt): ESP-IDF project root
- [`main/app_main.cpp`](./main/app_main.cpp): app bootstrap entrypoint
- [`components/app_config`](./components/app_config): centralized compile-time config
- [`components/platform_fs`](./components/platform_fs): SPIFFS mount, safe path helpers, and file manager operations
- [`components/platform_led`](./components/platform_led): board status RGB LED policy for Wi-Fi/CN105 health
- [`components/platform_log`](./components/platform_log): ESP-IDF log setup, persistent log mirroring, log pruning, and live log reads
- [`components/platform_uart`](./components/platform_uart): CN105 UART setup
- [`components/platform_wifi`](./components/platform_wifi): Wi-Fi STA-only setup and network heartbeat status
- [`components/web`](./components/web): minimal ESP-IDF HTTP/WebUI foundation
- [`components/web/tools/build_assets.py`](./components/web/tools/build_assets.py): build-time gzip splitter for WebUI shell assets
- [`components/core_cn105`](./components/core_cn105): offline CN105 protocol core and mock state
- [`components/homekit_bridge`](./components/homekit_bridge): Espressif HomeKit SDK binding over mock CN105 state
- [`debug_apps/cn105_probe`](./debug_apps/cn105_probe): standalone minimal ESP-IDF CN105 UART probe app for low-level wiring and handshake tests
- [`partitions.csv`](./partitions.csv): custom 4MB flash partition table
- [`CODEX_GUIDE.md`](./CODEX_GUIDE.md): local project guide and hardware rules
- [`original_version`](./original_version): upstream MitsubishiCN105ESPHome reference as a submodule
- [`external/esp-homekit-sdk`](./external/esp-homekit-sdk): Espressif HomeKit SDK submodule

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

## LED Status Policy

The current codebase drives the board RGB LED with this policy:

- `green solid`: Wi-Fi connected and CN105 link healthy
- `blue solid`: Wi-Fi is not connected
- `orange solid`: Wi-Fi is connected, but CN105 is not connected
- `red solid`: both Wi-Fi and CN105 are unavailable

Implementation note:

- LED control lives in `components/platform_led`
- the LED policy stays simple and diagnostic-first rather than becoming a second notification system

## CN105 Transport Notes

Recent hardware debugging produced a few implementation details that are now
reflected in the main firmware:

- CN105 `CONNECT ACK` packets (`0x7A` / `0x7B`) must be framed using the packet
  header length field, i.e. `byte[4] + 6`, rather than assuming a fixed
  8-byte response. Real hardware can return a valid 7-byte ACK such as
  `FC 7A 01 30 01 00 54`.
- The current real-transport baseline for the tested M5Stack ATOM Lite wiring
  is:
  - `UART1`
  - `2400 8E1`
  - `GPIO26` as `RX`
  - `GPIO32` as `TX`
  - CN105 `RX` pullup enabled on the ESP32 side
- The standalone probe app at [`debug_apps/cn105_probe`](./debug_apps/cn105_probe)
  exists specifically to validate pinout, baud, pullup behavior, and
  `0x5A`/`0x5B` connect behavior against unknown indoor units.

## CN105 Polling Strategy

The real CN105 transport does not use one fixed poll interval anymore.

Current behavior:

- if the indoor unit is `ON`, status polling runs every `15` seconds
- if the indoor unit is `OFF`, status polling slows to every `60` seconds
- if HomeKit or the WebUI sends a `POWER ON` command, the transport immediately
  returns to fast polling before the next state roundtrip completes
- if HomeKit or the WebUI sends a `POWER OFF` command, the transport stays
  conservative and only falls back to the slow interval after a later
  `0x02 settings` response confirms the indoor unit is really `OFF`

The defaults are seeded into NVS by `device_settings` on first boot, and existing
NVS values are not overwritten by later firmware boots:

- active polling: `15000 ms`
- off polling: `60000 ms`

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

For the standalone installer/probe firmware, use:

```bash
./build.py --app installer build
./build.py --app installer flash-auto --monitor
```

Each successful build exports a flash-ready package into:

```text
firmware_exports/<version>/
```

with four images named like:

```text
<project_name>_<version>_<0xOFFSET>.bin
```

The installer/probe firmware has its own port-80 WebUI after BLE Wi-Fi
provisioning and uses the same OTA partition table as the formal firmware. Its
job is to detect CN105 hardware settings, write `device_cfg` NVS with a full
overwrite strategy, then OTA-upload the formal app binary:

```text
firmware_exports/<version>/mitsubishi_heatpump_homekit_<version>_0x20000.bin
```

Or use the project-specific auto-flash helper:

```bash
./build.py flash-auto --monitor
./build.py flash-auto --port /dev/cu.usbserial-xxxx --monitor
./build.py flash-auto --no-build
./build.py serial-log --seconds 15
```

For quieter Codex/tool runs, use `--quiet-first`. It captures the first `idf.py` run and only reruns with verbose output if the quiet attempt fails:

```bash
./build.py --quiet-first build
./build.py --quiet-first flash-auto --no-build
```

`flash-auto --no-build` flashes the latest exported package for the selected app
instead of reading directly from the live `build/` directory.

The project default flash baud is `115200` for the current M5Stack/ESP32 board.
`serial-log` also overwrites an ignored local copy at `serial_logs/latest-serial.log` by default.

## Runtime Configuration

User-editable settings live in NVS under the `device_cfg` namespace. On first
boot, the firmware writes placeholder/default values only for missing keys; if a
key already exists, it is preserved. The current default Wi-Fi values are
placeholder strings, so a fresh formal firmware flash will stay offline until an
installer/probe firmware or WebUI save writes real Wi-Fi credentials into NVS.

If no real SSID is configured, the device stays offline and does not start a
fallback AP. If STA connection fails, it keeps retrying in STA mode.

Expected serial output:

- custom partition table with `factory` and `spiffs`
- `SPIFFS mounted`
- `Persistent log enabled (async): /boot-...-log.txt` or a timestamped `/YYYY-MM-DD-HH-MM-SS-log.txt`
- `Initializing CN105 UART: uart=1 rx=26 tx=32 baud=2400 format=8E1 rxPull=on txOD=off`
- `WiFi power save disabled`
- `CN105 offline self-test passed: 77F SET roundtrip`
- `HomeKit started: name=Mitsubishi AC setup_code=<random/generated> ...`
- `WebUI ready: http://<esp-ip>:8080/`
- either `Connected to ...` or reconnect/offline STA status
- a repeating platform heartbeat every 5 seconds with Wi-Fi status

## Upstream Reference

- Upstream reference: [echavet/MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome)
- HomeKit SDK target: [espressif/esp-homekit-sdk](https://github.com/espressif/esp-homekit-sdk)
- HomeKit SDK common examples: [esp-homekit-sdk/examples/common](https://github.com/espressif/esp-homekit-sdk/tree/master/examples/common)

## Next Planned Step

Finish the last hardware-oriented checkpoint:

- flash the current build
- open WebUI on `http://<esp-ip>:8080/`
- pair Apple Home using setup code `111-22-333`
- verify Home App changes show in WebUI after refresh
- verify WebUI changes update HomeKit characteristics
- verify real connect/info/set flow with CN105 wiring connected
- keep `mock` transport only as a fallback/dev mode if you need isolated WebUI/HomeKit testing
