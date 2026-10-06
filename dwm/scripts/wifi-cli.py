#!/usr/bin/env python3
"""Single-screen NetworkManager Wi-Fi terminal interface."""

from __future__ import annotations

import getpass
import os
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
PAGE_ROWS = 12


@dataclass
class Network:
    active: bool
    bssid: str
    ssid: str
    signal: int
    security: str


def run(*args: str, timeout: int = 45) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            args, text=True, capture_output=True, timeout=timeout, check=False
        )
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(args, 124, "", "Command timed out")


def split_fields(line: str) -> list[str]:
    fields: list[str] = []
    field: list[str] = []
    escaped = False
    for char in line:
        if escaped:
            field.append(char)
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == ":":
            fields.append("".join(field))
            field = []
        else:
            field.append(char)
    fields.append("".join(field))
    return fields


def powered() -> bool:
    return run("nmcli", "-t", "radio", "wifi").stdout.strip() == "enabled"


def refresh_bar() -> None:
    subprocess.run(
        ["pkill", "-RTMIN+3", "-x", "dwmstatus"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def set_power(enabled: bool) -> tuple[bool, str]:
    result = run("nmcli", "radio", "wifi", "on" if enabled else "off")
    refresh_bar()
    message = result.stdout.strip() or result.stderr.strip()
    return result.returncode == 0, message


def scan_networks(rescan: bool = True) -> tuple[list[Network], str]:
    result = run(
        "nmcli",
        "-t",
        "-e",
        "yes",
        "-f",
        "IN-USE,BSSID,SSID,SIGNAL,SECURITY",
        "device",
        "wifi",
        "list",
        "--rescan",
        "yes" if rescan else "no",
    )
    if result.returncode != 0:
        return [], result.stderr.strip() or "Wi-Fi scan failed"
    unique: dict[str, Network] = {}
    for line in result.stdout.splitlines():
        fields = split_fields(line)
        if len(fields) < 5:
            continue
        active, bssid, ssid, signal, security = fields[:5]
        try:
            strength = int(signal)
        except ValueError:
            strength = 0
        network = Network(active == "*", bssid, ssid, strength, security or "Open")
        key = ssid or bssid
        if key not in unique or network.signal > unique[key].signal:
            unique[key] = network
    return sorted(unique.values(), key=lambda item: (not item.active, -item.signal)), ""


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
    networks: list[Network],
    selected: int,
    message: str = "",
    search_query: str | None = None,
) -> None:
    clear()
    state = f"{GREEN}ON{RESET}" if powered() else f"{RED}OFF{RESET}"
    print(f"{BOLD} Wi-Fi Networks{RESET}                                      Power: {state}")
    print(f"{DIM}{'─' * 78}{RESET}")
    print(f"  {'SSID':<31} {'SIGNAL':>7}  {'SECURITY':<22} STATUS")
    print(f"{DIM}{'─' * 78}{RESET}")
    start = min(
        max(0, selected - PAGE_ROWS // 2),
        max(0, len(networks) - PAGE_ROWS),
    )
    visible = networks[start : start + PAGE_ROWS]
    if not networks:
        print(f"  {DIM}No networks available. Press R to scan.{RESET}")
    for index, network in enumerate(visible, start):
        name = network.ssid or f"Hidden ({network.bssid})"
        status = f"{GREEN}connected{RESET}" if network.active else ""
        row = f"  {name:<31.31} {network.signal:>6}%  {network.security:<22.22} {status}"
        print(f"{REVERSE}{row}{RESET}" if index == selected else row)
    print(f"{DIM}{'─' * 78}{RESET}")
    if len(networks) > PAGE_ROWS:
        print(f" {DIM}Showing {start + 1}-{start + len(visible)} of {len(networks)} networks{RESET}")
    print(
        f" {CYAN}↑/↓{RESET} Select  {CYAN}Space{RESET} Connect  {CYAN}/{RESET} Search  "
        f"{CYAN}P{RESET} Power  {CYAN}R{RESET} Refresh  {CYAN}Q{RESET} Quit"
    )
    if search_query is not None:
        print(f" {CYAN}/{RESET} Search SSID: {BOLD}{search_query}_{RESET}")
    if message:
        print(f" {YELLOW}{message}{RESET}")
    sys.stdout.flush()


def search_networks(networks: list[Network], selected: int) -> int:
    original = selected
    query = ""
    message = "Type an SSID, Enter to accept, Esc to cancel"
    while True:
        draw(networks, selected, message, query)
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
            message = "Type an SSID, Enter to accept, Esc to cancel"
            continue
        match = next(
            (
                index
                for index, network in enumerate(networks)
                if query.casefold() in network.ssid.casefold()
            ),
            None,
        )
        if match is None:
            message = "No matching SSID"
        else:
            selected = match
            message = ""


def password_modal(networks: list[Network], selected: int) -> str | None:
    network = networks[selected]
    draw(networks, selected)
    name = network.ssid or network.bssid
    print(f"\n{BOLD}┌─ Connect to {name} ─────────────────────────────┐{RESET}")
    print("│ Enter the password. Leave blank for open or saved networks. │")
    print(f"{BOLD}└───────────────────────────────────────────────────────────────┘{RESET}")
    try:
        return getpass.getpass(" Password: ")
    except (EOFError, KeyboardInterrupt):
        return None


def connect(network: Network, password: str) -> tuple[bool, str]:
    target = network.ssid or network.bssid
    if password:
        result = run("nmcli", "device", "wifi", "connect", target, "password", password)
    else:
        saved = run("nmcli", "connection", "up", "id", target)
        result = saved if saved.returncode == 0 else run(
            "nmcli", "device", "wifi", "connect", target
        )
    message = result.stdout.strip() or result.stderr.strip()
    refresh_bar()
    return result.returncode == 0, message


def interactive() -> int:
    selected = 0
    message = "Scanning…"
    draw([], selected, message)
    networks, message = scan_networks()
    while True:
        if networks:
            selected = min(selected, len(networks) - 1)
        else:
            selected = 0
        draw(networks, selected, message)
        message = ""
        key = read_key()
        if key.lower() == "q" or key == "\x1b":
            return 0
        if key in {"\x1b[A", "\x1b[5;2~", "\x19", "k"} and networks:
            selected = (selected - 1) % len(networks)
        elif key in {"\x1b[B", "\x1b[6;2~", "\x05", "j"} and networks:
            selected = (selected + 1) % len(networks)
        elif key == "/" and networks:
            selected = search_networks(networks, selected)
        elif key.lower() == "p":
            enable = not powered()
            ok, detail = set_power(enable)
            message = detail or (f"Wi-Fi turned {'on' if enable else 'off'}" if ok else "Power change failed")
            networks, scan_error = scan_networks(False) if enable else ([], "")
            message = scan_error or message
        elif key.lower() == "r":
            draw(networks, selected, "Scanning…")
            networks, message = scan_networks()
        elif key == " " and networks:
            if not powered():
                message = "Turn Wi-Fi on first with P"
                continue
            if networks[selected].active:
                message = "This network is already connected"
                continue
            password = password_modal(networks, selected)
            if password is None:
                message = "Connection cancelled"
                continue
            ok, detail = connect(networks[selected], password)
            message = detail or ("Connected" if ok else "Connection failed")
            networks, scan_error = scan_networks(False)
            message = scan_error or message


def main() -> int:
    if not shutil.which("nmcli"):
        print("wifi-cli: nmcli is required", file=sys.stderr)
        return 1
    if len(sys.argv) == 1:
        if not sys.stdin.isatty():
            print("wifi-cli: interactive mode requires a terminal", file=sys.stderr)
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
