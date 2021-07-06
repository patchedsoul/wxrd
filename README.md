# wxrd

A prototype-quality standalone client for xrdesktop based on wlroots and the wxrc codebase.

## wlroots

wxrd depends on wlroots version 0.14.

The correct wlroots version is included in wxrd as a git submodule.

Using this git submodule is optional:

    git submodule update --init

## Build

#### Configure the project
```
$ meson build
```

#### Compile the project
```
$ ninja -C build
```

## Run

#### Run wxrd
```
$ ./build/wxrd
```

#### Preliminary interaction

wxrd does not render on the desktop, but does start an "empty" session on the desktop that takes keyboard input that will be sent to the focused client.
Mouse movement is taken from the window but ignored, see commented code `update_pointer_default (server, time);` in input.c.

TODO: Mouse input needs to be merged with the VR controller input and the VR cursor must be moved according to mouse input.

`Alt + Enter` opens a new `weston-terminal`, or whatever is set in `$TERMINAL`.

Input focus follows the active VR controller. If VR controller doesn't point at a window, `Alt + Right` changes focus to the "next" window.

`Alt + Right Click / Drag` changes the size of the focused window (actual size, not scaling).

TODO: interaction concept that works with both VR controllers and mouse + keyboard, also moving windows in VR with mouse +
keyboard.

`wxrd -s app` immediately starts `app` after wxrd starts up.

#### Other TODOs

* better texture life cycle handling
* disable wlroots' gles renderer completely (it runs but is unused)
* chromium in xwayland doesn't behave properly
* subsurfaces are ignored, only the main surface is rendered

## Code of Conduct

Please note that this project is released with a Contributor Code of Conduct.
By participating in this project you agree to abide by its terms.

We follow the standard freedesktop.org code of conduct,
available at <https://www.freedesktop.org/wiki/CodeOfConduct/>,
which is based on the [Contributor Covenant](https://www.contributor-covenant.org).

Instances of abusive, harassing, or otherwise unacceptable behavior may be
reported by contacting:

* First-line project contacts:
  * Christoph Haag <christoph.haag@collabora.com>
  * Lubosz Sarnecki <lubosz.sarnecki@collabora.com>
* freedesktop.org contacts: see most recent list at <https://www.freedesktop.org/wiki/CodeOfConduct/>
