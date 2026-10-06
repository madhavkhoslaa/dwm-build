# DWM build

Personal DWM build for Void Linux and X11.

The bar uses five numbered workspaces (`1`–`5`).

- `dwm/` contains the window manager, bar, Eww, and Rofi sources based on
  ChadWM.
- `dwmstatus/` contains the Xlib-only status generator and `bar-power`.

The configured shortcuts are:

- `Super+D`: dmenu
- `Super+B`: Firefox
- `Super+Shift+B`: toggle the bar
- `Super+K`: cycle keyboard-backlight states
- `Super+T`: st terminal

The final clock module is centered. Scrolling over the volume or brightness
module adjusts that value and immediately refreshes `dwmstatus`.

## Network menus

Click the Wi-Fi or Bluetooth module in the status bar to open its terminal
manager in `st`. The Python scripts are in `dwm/scripts/` and use only the
standard library:

- `wifi-cli.py` shows available networks in one table and uses NetworkManager's
  `nmcli` to control power and connect with an inline password prompt.
- `bluetooth-cli.py` shows known devices in one table and uses BlueZ's
  `bluetoothctl` to control power, scan, pair, trust, and connect after an
  inline confirmation prompt.

Both tables use `Up`/`Down` (or `J`/`K` and the mouse wheel) to select a row,
`Space` to connect, `/` to search and jump to an SSID or device name, `P` to
toggle radio power, `R` to refresh or scan, and `Q` to close the terminal.
Actions and prompts stay in the same `st` window.

They can also be used directly with `status`, `on`, `off`, or `toggle`, for
example `wifi-cli.py toggle`. Runtime dependencies are Python 3,
NetworkManager, and BlueZ; no third-party Python packages are required.

See `dwmstatus/README.md` for status, power-menu, build, and signal-refresh
instructions.

## Automatic wallpaper themes

Press `Super+Shift+W` to choose a wallpaper, restore the previous palette, or
pick a random image from `~/Pictures/wall`. The picker uses pywal and
ImageMagick, updates the wallpaper, DWM bar, dwmstatus, st, Rofi, and GTK 3/4,
then restarts DWM without logging out.

Required color-extraction package:

```sh
sudo xbps-install -S ImageMagick
```

The selected wallpaper is saved in `~/.cache/theme-picker/wallpaper` and is
restored by `scripts/run.sh` on the next login.

For new st terminals, add this to the interactive shell configuration:

```sh
[ -r "$HOME/.cache/wal/sequences" ] && cat "$HOME/.cache/wal/sequences"
```

Copy the repository's `gtk-3.0` and `gtk-4.0` directories into `~/.config`
once; the picker updates their managed `wal.css` files afterward.
