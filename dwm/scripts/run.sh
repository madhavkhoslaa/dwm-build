#!/bin/sh

export PATH="$HOME/.local/bin:$PATH"

command -v xrdb >/dev/null 2>&1 && xrdb merge "$HOME/.Xresources"
if ! "$HOME/.config/chadwm/scripts/theme-picker" --restore startup; then
  feh --bg-fill "$HOME/Pictures/wall/gruv.png" &
fi
xbacklight -set 10 &
xset r rate 200 50 &
picom --backend glx &

dwmstatus &
while type dwm >/dev/null; do dwm && continue || break; done
