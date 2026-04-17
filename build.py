#!/usr/bin/env python3
"""Project-local ESP-IDF command wrapper.

This keeps the repository clean while using the global EIM-managed ESP-IDF
installation selected for this project.
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent
IDF_VERSION = "v5.4.1"
DEFAULT_FLASH_BAUD = 115200
SERIAL_LOG_DIR = PROJECT_ROOT / "serial_logs"
SERIAL_LOG_PATH = SERIAL_LOG_DIR / "latest-serial.log"
IDF_TOOLS_PATH = Path.home() / ".espressif" / "tools"
IDF_PATH = Path.home() / ".espressif" / IDF_VERSION / "esp-idf"
IDF_PYTHON = IDF_TOOLS_PATH / "python" / IDF_VERSION / "venv" / "bin" / "python"
IDF_SCRIPT = IDF_PATH / "tools" / "idf.py"

TOOL_PATHS = [
    IDF_TOOLS_PATH / "cmake" / "3.30.2" / "CMake.app" / "Contents" / "bin",
    IDF_TOOLS_PATH / "esp-clang" / "esp-18.1.2_20240912" / "esp-clang" / "bin",
    IDF_TOOLS_PATH / "esp-rom-elfs" / "20241011",
    IDF_TOOLS_PATH / "esp32ulp-elf" / "2.38_20240113" / "esp32ulp-elf" / "bin",
    IDF_TOOLS_PATH / "esp32ulp-elf" / "2.38_20240113" / "esp32ulp-elf" / "esp32ulp-elf" / "bin",
    IDF_TOOLS_PATH / "ninja" / "1.12.1",
    IDF_TOOLS_PATH / "openocd-esp32" / "v0.12.0-esp32-20241016" / "openocd-esp32" / "bin",
    IDF_TOOLS_PATH / "xtensa-esp-elf-gdb" / "14.2_20240403" / "xtensa-esp-elf-gdb" / "bin",
    IDF_TOOLS_PATH / "xtensa-esp-elf" / "esp-14.2.0_20241119" / "xtensa-esp-elf" / "bin",
    IDF_TOOLS_PATH / "xtensa-esp-elf" / "esp-14.2.0_20241119" / "xtensa-esp-elf" / "xtensa-esp-elf" / "bin",
    IDF_TOOLS_PATH / "python" / IDF_VERSION / "venv" / "bin",
]

SERIAL_PORT_PATTERNS = [
    "/dev/cu.usbserial*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.SLAB_USBtoUART*",
    "/dev/cu.usbmodem*",
    "/dev/cu.ESP32*",
]


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def build_env() -> dict[str, str]:
    if not IDF_PATH.exists():
        fail(f"ESP-IDF {IDF_VERSION} is missing at {IDF_PATH}")
    if not IDF_PYTHON.exists():
        fail(f"ESP-IDF Python venv is missing at {IDF_PYTHON}")
    if not IDF_SCRIPT.exists():
        fail(f"idf.py is missing at {IDF_SCRIPT}")

    env = os.environ.copy()
    env["ESP_IDF_VERSION"] = "5.4"
    env["IDF_TOOLS_PATH"] = str(IDF_TOOLS_PATH)
    env["IDF_COMPONENT_LOCAL_STORAGE_URL"] = f"file://{IDF_TOOLS_PATH}"
    env["IDF_PATH"] = str(IDF_PATH)
    env["ESP_ROM_ELF_DIR"] = str(IDF_TOOLS_PATH / "esp-rom-elfs" / "20241011")
    env["OPENOCD_SCRIPTS"] = str(
        IDF_TOOLS_PATH
        / "openocd-esp32"
        / "v0.12.0-esp32-20241016"
        / "openocd-esp32"
        / "share"
        / "openocd"
        / "scripts"
    )
    env["IDF_PYTHON_ENV_PATH"] = str(IDF_TOOLS_PATH / "python" / IDF_VERSION / "venv")

    path_prefix = os.pathsep.join(str(path) for path in TOOL_PATHS)
    env["PATH"] = path_prefix + os.pathsep + env.get("PATH", "")
    return env


def ensure_idf_python() -> None:
    current_python = Path(sys.executable)
    target_python = IDF_PYTHON
    if current_python != target_python:
        os.execv(str(IDF_PYTHON), [str(IDF_PYTHON), str(Path(__file__).resolve()), *sys.argv[1:]])


def detect_serial_ports() -> list[str]:
    ports: list[str] = []
    for pattern in SERIAL_PORT_PATTERNS:
        ports.extend(glob.glob(pattern))
    return sorted(set(ports))


def run_idf(args: list[str], quiet_first: bool = False) -> int:
    cmd = [str(IDF_PYTHON), str(IDF_SCRIPT), *args]
    env = build_env()
    if not quiet_first:
        return subprocess.call(cmd, cwd=PROJECT_ROOT, env=env)

    print(f"Running quietly: idf.py {' '.join(args)}", flush=True)
    result = subprocess.run(
        cmd,
        cwd=PROJECT_ROOT,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode == 0:
        print(f"Quiet run succeeded: idf.py {' '.join(args)}", flush=True)
        return 0

    print(
        f"Quiet run failed with exit code {result.returncode}; re-running once with normal verbose output...",
        flush=True,
    )
    return subprocess.call(cmd, cwd=PROJECT_ROOT, env=env)


def flash(args: argparse.Namespace) -> int:
    port = args.port
    if not port:
        ports = detect_serial_ports()
        if len(ports) == 1:
            port = ports[0]
            print(f"Detected ESP32 serial port: {port}")
        elif len(ports) > 1:
            print("Multiple ESP32-like serial ports found:", file=sys.stderr)
            for candidate in ports:
                print(f"  {candidate}", file=sys.stderr)
            print("Please rerun with --port /dev/cu.xxxxx", file=sys.stderr)
            return 2
        else:
            print("No ESP32-like serial port found.", file=sys.stderr)
            print("Checked patterns:", file=sys.stderr)
            for pattern in SERIAL_PORT_PATTERNS:
                print(f"  {pattern}", file=sys.stderr)
            return 2

    idf_args = ["-p", port, "-b", str(args.baud)]
    if args.no_build:
        idf_args.append("flash")
    else:
        idf_args.extend(["build", "flash"])
    if args.monitor:
        idf_args.append("monitor")
    return run_idf(idf_args, quiet_first=args.quiet_first)


def flash_esptool(args: argparse.Namespace) -> int:
    port = args.port
    if not port:
        ports = detect_serial_ports()
        if len(ports) == 1:
            port = ports[0]
            print(f"Detected ESP32 serial port: {port}")
        elif len(ports) > 1:
            print("Multiple ESP32-like serial ports found:", file=sys.stderr)
            for candidate in ports:
                print(f"  {candidate}", file=sys.stderr)
            print("Please rerun with --port /dev/cu.xxxxx", file=sys.stderr)
            return 2
        else:
            print("No ESP32-like serial port found.", file=sys.stderr)
            return 2

    build_dir = PROJECT_ROOT / "build"
    flash_args = build_dir / "flasher_args.json"
    if not flash_args.exists():
        fail(f"No build found at {flash_args}. Run a build first (e.g. via Docker).")

    import json
    with open(flash_args) as f:
        args_data = json.load(f)

    parts = []
    for addr, path in args_data.get("flash_files", {}).items():
        bin_path = build_dir / path
        if not bin_path.exists():
            fail(f"Binary not found: {bin_path}")
        parts.extend([addr, str(bin_path)])

    esptool = "esptool"
    for candidate in ["esptool", "esptool.py"]:
        import shutil
        if shutil.which(candidate):
            esptool = candidate
            break

    cmd = [
        esptool, "--chip", "esp32",
        "-p", port, "-b", str(args.baud),
        "--before", "default-reset", "--after", "hard-reset",
        "write-flash",
        "--flash-mode", args_data.get("flash_settings", {}).get("flash_mode", "dio"),
        "--flash-size", args_data.get("flash_settings", {}).get("flash_size", "4MB"),
        "--flash-freq", args_data.get("flash_settings", {}).get("flash_freq", "40m"),
        *parts,
    ]
    print(f"Flashing: {' '.join(cmd)}", flush=True)
    return subprocess.call(cmd)


def serial_log(args: argparse.Namespace) -> int:
    port = args.port
    if not port:
        ports = detect_serial_ports()
        if len(ports) == 1:
            port = ports[0]
            print(f"Detected ESP32 serial port: {port}", flush=True)
        elif len(ports) > 1:
            print("Multiple ESP32-like serial ports found:", file=sys.stderr)
            for candidate in ports:
                print(f"  {candidate}", file=sys.stderr)
            print("Please rerun with --port /dev/cu.xxxxx", file=sys.stderr)
            return 2
        else:
            print("No ESP32-like serial port found.", file=sys.stderr)
            return 2

    try:
        import serial
    except ImportError:
        fail("pyserial is missing in the current Python environment")

    log_path = None
    log_file = None
    if args.save:
        SERIAL_LOG_DIR.mkdir(exist_ok=True)
        log_path = SERIAL_LOG_PATH
        log_file = log_path.open("w", encoding="utf-8")
        print(f"Saving serial log copy to: {log_path}", flush=True)

    try:
        deadline = time.monotonic() + args.seconds
        with serial.Serial(port, args.baud, timeout=0.5) as ser:
            ser.setDTR(False)
            ser.setRTS(False)
            while time.monotonic() < deadline:
                chunk = ser.readline()
                if chunk:
                    text = chunk.decode("utf-8", errors="replace")
                    print(text, end="", flush=True)
                    if log_file:
                        log_file.write(text)
                        log_file.flush()
    finally:
        if log_file:
            log_file.close()
    return 0


def main() -> int:
    argv = sys.argv[1:]

    if len(argv) > 0 and argv[0] in ("flash-esptool", "serial-log"):
        pass
    else:
        ensure_idf_python()

    quiet_first = False
    if "--quiet-first" in argv:
        quiet_first = True
        argv = [arg for arg in argv if arg != "--quiet-first"]
    if "-q" in argv:
        quiet_first = True
        argv = [arg for arg in argv if arg != "-q"]

    if len(argv) > 0 and argv[0] == "flash-esptool":
        parser = argparse.ArgumentParser(description="Flash using esptool directly (no ESP-IDF toolchain required).")
        parser.add_argument("-p", "--port", help="Serial port, for example /dev/cu.usbserial-xxxx")
        parser.add_argument("-b", "--baud", default=DEFAULT_FLASH_BAUD, type=int, help="Flash baud rate")
        parsed = parser.parse_args(argv[1:])
        return flash_esptool(parsed)

    if len(argv) > 0 and argv[0] == "flash-auto":
        parser = argparse.ArgumentParser(description="Auto-detect ESP32 serial port and flash this project.")
        parser.add_argument("-p", "--port", help="Serial port, for example /dev/cu.usbserial-xxxx")
        parser.add_argument("-b", "--baud", default=DEFAULT_FLASH_BAUD, type=int, help="Flash baud rate")
        parser.add_argument("--monitor", action="store_true", help="Open serial monitor after flashing")
        parser.add_argument("--no-build", action="store_true", help="Skip build and only flash existing binaries")
        parser.add_argument("--quiet-first", action="store_true", help="Capture idf.py output first, rerun verbose only on failure")
        parsed = parser.parse_args(argv[1:])
        parsed.quiet_first = parsed.quiet_first or quiet_first
        return flash(parsed)

    if len(argv) > 0 and argv[0] == "serial-log":
        parser = argparse.ArgumentParser(description="Auto-detect ESP32 serial port and print serial logs briefly.")
        parser.add_argument("-p", "--port", help="Serial port, for example /dev/cu.usbserial-xxxx")
        parser.add_argument("-b", "--baud", default=115200, type=int, help="Serial baud rate")
        parser.add_argument("-s", "--seconds", default=15, type=int, help="How many seconds to read")
        parser.add_argument("--no-save", dest="save", action="store_false", help="Do not save a local log copy")
        parser.set_defaults(save=True)
        return serial_log(parser.parse_args(argv[1:]))

    args = argv or ["build"]
    return run_idf(args, quiet_first=quiet_first)


if __name__ == "__main__":
    raise SystemExit(main())
