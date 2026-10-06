#!/bin/sh

xrdb merge ~/.Xresources
if ! "$HOME/.config/chadwm/scripts/theme-picker" --restore startup; then
  feh --bg-fill "$HOME/Pictures/wall/gruv.png" &
fi
xbacklight -set 10 &
xset r rate 200 50 &
picom &

dwmstatus &
while type dwm >/dev/null; do dwm && continue || break; done
