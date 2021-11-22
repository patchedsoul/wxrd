# wxrd

A prototype-quality standalone client for xrdesktop based on wlroots and the wxrc codebase.

## wlroots

wxrd depends on wlroots master (0.15).

The correct wlroots commit is included in wxrd as a git submodule.

## Build

wxrd requires the `next` branches of gulkan, gxr and xrdesktop. It does not require libinputsynth.

On bleeding edge distributions, wxrd can be built directly

Basic documentation to build wxrd on Ubuntu/Debian [can be found on the wxrd wiki](https://gitlab.freedesktop.org/xrdesktop/wxrd/-/wikis/installation-from-source).

#### Clone the repository recursively

```
git clone --recursive https://gitlab.freedesktop.org/xrdesktop/wxrd.git
cd wxrd
```

#### Configure the project

```
meson -Dexamples=false -Ddefault_library=static build
```

#### Compile the project
```
ninja -C build
```

## Run

wxrd is a single executable that can be run from build/src/wxrd or after ninja install /usr/bin/wxrd.


```
./build/wxrd
```


Without arguments wxrd starts an empty xrdesktop session.

wxrd inherits some arguments and key bindings from wxrc.

A startup application can be specified with the -s switch e.g. `wxrd -s weston-terminal`.

When wxrd is run on drm (without an X11 or wayland session) or with the `WXRD_HEADLESS=1` environment variable, only VR controller input is possible at this time.

When wxrd is run in an X11 or wayland session, an empty window is created by wlroots. This window captures physical keyboard input. While this empty window is focused, keyboard input is forwarded to the VR window that is currently focused, and certain hotkeys are enabled.


Mouse input is currently not forwarded in order not to interfere with VR controller input.
In the future the input from physical mouse and VR s can be combined without interfering, for example with separate `wl_seat`s for physical and VR input.

A window is continuously focused as long as the VR controller that controls the cursor points at it.
Alternatively the "next" window can be focused by pressing alt+right, however as soon as the VR controller points at a window, this focus will be overriden.

alt + enter starts the terminal that is set with the `TERMINAL` environment variable e.g. `TERMINAL=alacritty`. If TERMINAL is not set, `weston-terminal` us used as default terminal.

alt + q quits the focused window.


X11 and wayland application can be started from outside of wxrd by setting the DISPLAY and WAYLAND_DISPLAY environment variables. On startup wxrd prints such messages on startup:

Wayland compositor listening on WAYLAND_DISPLAY=wayland-0
initialized xwayland on :2

This means wayland applications can now be started on wxrd with setting the environment variable `WAYLAND_DISPLAY=wayland-0` and X11 applications with `DISPLAY=:2`.

Many applications can be run as both wayland and X11 applications, often there are additional environment variables to select a wayland backend. For Qt applications that is `QT_QPA_PLATFORM=wayland` and for gtk3 and gtk4 applications `GDK_BACKEND=wayland`.


#### TODOs

* better texture life cycle handling
* disable wlroots' gles renderer completely (it runs but is unused)
* chromium in xwayland doesn't behave properly
* xwayland popup window placement
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
