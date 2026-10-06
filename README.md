# DWM build

Personal DWM build for Void Linux and X11.

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
