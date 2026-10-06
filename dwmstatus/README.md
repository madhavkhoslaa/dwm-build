# dwmstatus for Void Linux

Small Xlib-only status generator for dwm, plus a dmenu power script.

## Dependencies

```sh
sudo xbps-install -S git gcc make libX11-devel iw wireplumber dmenu slock brightnessctl
```

Install a Nerd Font package as well so the status icons render correctly.

## Build and install

```sh
git clone https://git.suckless.org/dwmstatus
cd dwmstatus
make clean all
make install
install -m755 bar-power ~/.local/bin/bar-power
```

`config.mk` installs `dwmstatus` to `~/.local/bin` and links only Xlib.

Start it from `~/.xinitrc` before dwm:

```sh
dwmstatus &
exec dwm
```

## Power permissions

With elogind running, `bar-power` uses `loginctl reboot` and
`loginctl poweroff`. Otherwise it tries `doas`, then `sudo`.

For passwordless doas power actions, add these using `doas`'s exact path on
your system (normally `/usr/bin` on Void):

```text
permit nopass :wheel cmd /usr/bin/reboot
permit nopass :wheel cmd /usr/bin/poweroff
```

## dwm key bindings

Include `<X11/XF86keysym.h>` in `config.h`. These examples change the value,
then request an immediate status refresh:

```c
{ 0, XF86XK_AudioRaiseVolume, spawn,
  SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+; pkill -RTMIN+1 dwmstatus") },
{ 0, XF86XK_AudioLowerVolume, spawn,
  SHCMD("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-; pkill -RTMIN+1 dwmstatus") },
{ 0, XF86XK_AudioMute, spawn,
  SHCMD("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle; pkill -RTMIN+1 dwmstatus") },
{ 0, XF86XK_MonBrightnessUp, spawn,
  SHCMD("$HOME/.config/chadwm/scripts/bar-brightness up") },
{ 0, XF86XK_MonBrightnessDown, spawn,
  SHCMD("$HOME/.config/chadwm/scripts/bar-brightness down") },
{ MODKEY|ShiftMask, XK_p, spawn, SHCMD("bar-power") },
{ MODKEY, XK_x, spawn, SHCMD("slock") },
```

The status order is volume, brightness, Wi-Fi, CPU, RAM, root disk, battery,
and local time. Send `SIGTERM` or `SIGINT` for a clean exit that clears the
root window name.

Send `SIGHUP` to reload semantic colors from `~/.cache/wal/colors` without
restarting the process.

Send `SIGRTMIN+3` to refresh Wi-Fi and Bluetooth immediately. The included
terminal network managers send this signal after power or connection changes.

In the accompanying DWM build, the final time module is drawn in the center.
The volume and brightness modules contain invisible status markers used by
DWM to provide module-specific mouse-wheel controls.

Click the Wi-Fi or Bluetooth module to open its table-based manager in st.
Wi-Fi is green while connected, grey while disconnected, and red while its
interface is off. Bluetooth is green while powered and red with an `off`
label while powered down. Battery color is green above 70%, orange from
30–70%, and red below 30%; charging adds a bolt.
