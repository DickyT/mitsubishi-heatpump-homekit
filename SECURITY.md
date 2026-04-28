# Security Policy

## Supported Versions

Security fixes are provided through the latest GitHub Release.

Kiri Bridge users should install current release packages from:

```text
https://github.com/DickyT/kiri-homekit/releases
```

## Reporting a Vulnerability

Please do not open a public issue for a security vulnerability.

Email:

```text
hello@kiri.dkt.moe
```

Include:

- affected firmware version
- affected component or page
- reproduction steps
- impact description
- whether the issue requires local network access, Bluetooth proximity, or
  physical access

## Scope

In scope:

- Kiri Bridge firmware
- installer firmware
- hosted setup and flashing pages in `site`
- package validation and update flow

Out of scope:

- vulnerabilities in user WiFi routers
- Apple HomeKit platform issues
- unsupported third-party hardware modifications
- physical attacks requiring device possession unless they affect normal users

## Safety Notes

Kiri Bridge is a local network device that controls HVAC equipment. Do not
expose the device WebUI directly to the public internet.
