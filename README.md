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
