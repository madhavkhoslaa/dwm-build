#!/usr/bin/env python3
"""Single-screen BlueZ Bluetooth terminal interface."""

from __future__ import annotations

import os
import re
import select
import shutil
import subprocess
import sys
import termios
import tty
from dataclasses import dataclass


RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"
CYAN = "\033[36m"
REVERSE = "\033[7m"
DEVICE_RE = re.compile(r"^Device ([0-9A-Fa-f:]{17}) (.+)$")
PAGE_ROWS = 12


@dataclass
class Device:
    address: str
    name: str
    paired: bool = False
    trusted: bool = False
    connected: bool = False


def run(*args: str, timeout: int = 45) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            args, text=True, capture_output=True, timeout=timeout, check=False
        )
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(args, 124, "", "Command timed out")


def properties(output: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in output.splitlines():
        if ":" in line:
            key, value = line.strip().split(":", 1)
            result[key] = value.strip()
    return result


def adapter_info() -> dict[str, str]:
    return properties(run("bluetoothctl", "show").stdout)


def powered() -> bool:
    return adapter_info().get("Powered") == "yes"


def refresh_bar() -> None:
    subprocess.run(
        ["pkill", "-RTMIN+3", "-x", "dwmstatus"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def set_power(enabled: bool) -> tuple[bool, str]:
    result = run("bluetoothctl", "power", "on" if enabled else "off")
    refresh_bar()
    message = result.stdout.strip() or result.stderr.strip()
    ok = result.returncode == 0 and "Failed" not in message
    return ok, message


def get_devices() -> list[Device]:
    devices: list[Device] = []
    for line in run("bluetoothctl", "devices").stdout.splitlines():
        match = DEVICE_RE.match(line.strip())
        if not match:
            continue
        address, fallback_name = match.groups()
        info = properties(run("bluetoothctl", "info", address).stdout)
        devices.append(
            Device(
                address.upper(),
                info.get("Name", info.get("Alias", fallback_name)),
                info.get("Paired") == "yes",
                info.get("Trusted") == "yes",
                info.get("Connected") == "yes",
            )
        )
    return sorted(devices, key=lambda item: (not item.connected, not item.paired, item.name.lower()))


def scan_devices() -> tuple[list[Device], str]:
    result = run("bluetoothctl", "--timeout", "8", "scan", "on", timeout=12)
    message = "" if result.returncode == 0 else result.stderr.strip() or "Bluetooth scan failed"
    return get_devices(), message


def read_key() -> str:
    fd = sys.stdin.fileno()
    previous = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        key = os.read(fd, 1).decode(errors="ignore")
        if key == "\x1b":
            while len(key) < 16 and select.select([fd], [], [], 0.04)[0]:
                part = os.read(fd, 1).decode(errors="ignore")
                key += part
                if len(key) >= 3 and part and "@" <= part <= "~":
                    break
        return key
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, previous)


def clear() -> None:
    print("\033[2J\033[H", end="")


def draw(
    devices: list[Device],
    selected: int,
    message: str = "",
    search_query: str | None = None,
) -> None:
    clear()
    state = f"{GREEN}ON{RESET}" if powered() else f"{RED}OFF{RESET}"
    print(f"{BOLD} Bluetooth Devices{RESET}                                  Power: {state}")
    print(f"{DIM}{'─' * 78}{RESET}")
    print(f"  {'DEVICE':<29} {'ADDRESS':<18} {'PAIRED':<8} STATUS")
    print(f"{DIM}{'─' * 78}{RESET}")
    start = min(
        max(0, selected - PAGE_ROWS // 2),
        max(0, len(devices) - PAGE_ROWS),
    )
    visible = devices[start : start + PAGE_ROWS]
    if not devices:
        print(f"  {DIM}No devices found. Press R to scan.{RESET}")
    for index, device in enumerate(visible, start):
        paired = "yes" if device.paired else "no"
        status = f"{GREEN}connected{RESET}" if device.connected else ""
        row = f"  {device.name:<29.29} {device.address:<18} {paired:<8} {status}"
        print(f"{REVERSE}{row}{RESET}" if index == selected else row)
    print(f"{DIM}{'─' * 78}{RESET}")
    if len(devices) > PAGE_ROWS:
        print(f" {DIM}Showing {start + 1}-{start + len(visible)} of {len(devices)} devices{RESET}")
    print(
        f" {CYAN}↑/↓{RESET} Select  {CYAN}Space{RESET} Connect  {CYAN}/{RESET} Search  "
        f"{CYAN}P{RESET} Power  {CYAN}R{RESET} Scan  {CYAN}Q{RESET} Quit"
    )
    if search_query is not None:
        print(f" {CYAN}/{RESET} Search device: {BOLD}{search_query}_{RESET}")
    if message:
        print(f" {YELLOW}{message}{RESET}")
    sys.stdout.flush()


def search_devices(devices: list[Device], selected: int) -> int:
    original = selected
    query = ""
    message = "Type a device name, Enter to accept, Esc to cancel"
    while True:
        draw(devices, selected, message, query)
        key = read_key()
        if key in {"\r", "\n"}:
            return selected
        if key == "\x1b":
            return original
        if key in {"\x7f", "\b"}:
            query = query[:-1]
        elif len(key) == 1 and key.isprintable():
            query += key
        else:
            continue
        if not query:
            selected = original
            message = "Type a device name, Enter to accept, Esc to cancel"
            continue
        match = next(
            (
                index
                for index, device in enumerate(devices)
                if query.casefold() in device.name.casefold()
            ),
            None,
        )
        if match is None:
            message = "No matching device"
        else:
            selected = match
            message = ""


def confirm_modal(devices: list[Device], selected: int) -> bool:
    device = devices[selected]
    draw(devices, selected)
    action = "Connect" if device.paired else "Pair and connect"
    print(f"\n{BOLD}┌─ {action} ───────────────────────────────────────────┐{RESET}")
    print(f"│ {device.name:<57.57} │")
    print(f"│ Continue? [y/N]                                            │")
    print(f"{BOLD}└──────────────────────────────────────────────────────────────┘{RESET}")
    try:
        return input(" Choice: ").strip().lower() == "y"
    except (EOFError, KeyboardInterrupt):
        return False


def pair_and_connect(device: Device) -> tuple[bool, str]:
    messages: list[str] = []
    if not device.paired:
        result = run("bluetoothctl", "pair", device.address, timeout=60)
        messages.append(result.stdout.strip() or result.stderr.strip())
        if result.returncode != 0 or "Failed" in messages[-1]:
            return False, messages[-1] or "Pairing failed"
    if not device.trusted:
        result = run("bluetoothctl", "trust", device.address)
        messages.append(result.stdout.strip() or result.stderr.strip())
    result = run("bluetoothctl", "connect", device.address, timeout=45)
    refresh_bar()
    messages.append(result.stdout.strip() or result.stderr.strip())
    message = next((item for item in reversed(messages) if item), "Connected")
    return result.returncode == 0 and "Failed" not in message, message


def interactive() -> int:
    selected = 0
    devices = get_devices()
    message = "Press R to scan for nearby devices"
    while True:
        if devices:
            selected = min(selected, len(devices) - 1)
        else:
            selected = 0
        draw(devices, selected, message)
        message = ""
        key = read_key()
        if key.lower() == "q" or key == "\x1b":
            return 0
        if key in {"\x1b[A", "\x1b[5;2~", "\x19", "k"} and devices:
            selected = (selected - 1) % len(devices)
        elif key in {"\x1b[B", "\x1b[6;2~", "\x05", "j"} and devices:
            selected = (selected + 1) % len(devices)
        elif key == "/" and devices:
            selected = search_devices(devices, selected)
        elif key.lower() == "p":
            enable = not powered()
            ok, detail = set_power(enable)
            message = detail or (
                f"Bluetooth turned {'on' if enable else 'off'}" if ok else "Power change failed"
            )
            devices = get_devices()
        elif key.lower() == "r":
            if not powered():
                message = "Turn Bluetooth on first with P"
                continue
            draw(devices, selected, "Scanning for 8 seconds…")
            devices, message = scan_devices()
        elif key == " " and devices:
            if not powered():
                message = "Turn Bluetooth on first with P"
                continue
            if devices[selected].connected:
                message = "This device is already connected"
                continue
            if not confirm_modal(devices, selected):
                message = "Connection cancelled"
                continue
            ok, message = pair_and_connect(devices[selected])
            if not message:
                message = "Connected" if ok else "Connection failed"
            devices = get_devices()


def main() -> int:
    if not shutil.which("bluetoothctl"):
        print("bluetooth-cli: bluetoothctl is required", file=sys.stderr)
        return 1
    if len(sys.argv) == 1:
        if not sys.stdin.isatty():
            print("bluetooth-cli: interactive mode requires a terminal", file=sys.stderr)
            return 1
        return interactive()
    command = sys.argv[1].lower()
    if command == "status":
        print("on" if powered() else "off")
        return 0
    if command in {"on", "off", "toggle"}:
        enable = not powered() if command == "toggle" else command == "on"
        ok, message = set_power(enable)
        if message:
            print(message)
        return 0 if ok else 1
    print(f"Usage: {sys.argv[0]} [status|on|off|toggle]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
