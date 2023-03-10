# `xautocfg`

Automatic run-time configuration for X11

Features:
- Automatic keyboard repeat rate configuration.
  When a new keyboard is connected and used by X11, configure its repeat rate to desired values.


## Setup

### Installation

dependencies:
- `C++20`
- `libX11`
- `libXi`

building:
- run `make`


### Running

You should install `xautocfg` through your linux distribution, so that all necessary files are managed and updated by your package manager.

Then:
- place [config](etc/xautocfg.cfg) in ~/.config/xautocfg.cfg
- run `xautocfg` manually or:
- enable/use the systemd user service (which needs `graphical-session.target`!):
  - `systemctl --user enable xautocfg.service`
  - `systemctl --user start xautocfg.service`


### `systemd` setup for window managers

The `systemd` service binds to the `graphical-session.target` which is started by your desktop environment.
Some window managers (e.g. i3 in 2022) don't start `graphical-session.target`, so you need custom user service files.

As an example, these files are needed for [i3](https://i3wm.org/) as long as it doesn't start the target on its own.

`~/.config/systemd/user/i3-session.service`:
```systemd
[Unit]
Description=i3 window manager session
PartOf=i3-session.target
Wants=i3-session.target

# this service is started by i3 in its config file
# it's done like this so i3 and its launched child processes
# are not a service itself, but in the regular session scope.
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/true
Restart=on-failure
```

`~/.config/systemd/user/i3-session.target`:
```
# this target is wanted by i3-session.service (started by i3 config file)
[Unit]
Description=i3 Session
Requires=basic.target
BindsTo=graphical-session.target
Before=graphical-session.target

DefaultDependencies=no
RefuseManualStart=yes
RefuseManualStop=yes
Requires=basic.target
StopWhenUnneeded=yes
```

and finally in your i3 configuration file:

```cfg
exec --no-startup-id systemctl start --user i3-session.service
```

it remains to be solved how we can reliably stop `i3-session.service` once i3 has exited.

## Alternative setup ideas

### XServer startup options
As alternative to this tool, you can set defaults for the X Server at startup.
- For the keyboard repeat rate:
```
X -ardelay 200 -arinterval 20  # (interval is 1000/rate_in_hz)
```

For this to configure, you need the privileges to edit X launch properties (probably in your login tool like `lightdm`).

### XServer configuration file
XServer since version [21.1.0](https://cgit.freedesktop.org/xorg/xserver/commit/?id=4f95d87d66b6a6e11aa8616c9242e0907ffee66b) supports the option `AutoRepeat`.
Basically you need an xorg config section like this (the second value again `1000/rate_in_hz`):

```
Section "InputClass"
    Identifier "system-keyboard"
    MatchIsKeyboard "on"
    Option "AutoRepeat" "200 20"
EndSection
```

## Contact

Please join our matrix room: `#sfttech:matrix.org`!

For bug reports and patches, please create issues and pull requests at https://github.com/SFTtech/xautocfg


## License

Licensed under [GPLv3](LICENSE) or later.
