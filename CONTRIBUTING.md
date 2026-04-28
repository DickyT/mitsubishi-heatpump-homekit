# Contributing

Contributions are welcome.

Kiri Bridge is both an open source firmware project and a hardware kit. Please
keep changes friendly to buyers who use release packages and do not build from
source.

## Before Opening a Pull Request

- Keep buyer-facing flows simple.
- Preserve browser-based setup and recovery where possible.
- Avoid adding cloud dependencies to core control paths.
- Test both production firmware and installer firmware when changing shared
  settings or user-facing behavior.
- Do not commit generated build outputs or private logs.
- Do not include WiFi credentials, HomeKit setup codes, or private device data.

## Areas That Need Extra Care

- CN105 packet handling
- HomeKit state synchronization
- OTA and recovery flashing
- BLE WiFi provisioning
- NVS settings migration
- installer defaults and recovery behavior

## Code Style

- Prefer small, focused changes.
- Keep comments useful and brief.
- Match the surrounding C++ and HTML style.
- Keep user-facing language clear and non-technical when possible.

## Reporting Issues

Use the bug report template and include firmware version, hardware details,
Mitsubishi indoor unit model if known, logs, and exact reproduction steps.
