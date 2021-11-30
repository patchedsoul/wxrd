/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef WXRC_SERVER_H
#define WXRC_SERVER_H
#include "input.h"
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "xwayland.h"

struct wxrd_xr_backend;

struct wxrd_server
{
  struct wl_display *wl_display;

  GMutex render_mutex;

  struct wlr_backend *backend;
  struct wxrd_xr_backend *xr_backend;

  struct wlr_allocator *allocator;

  // used with noop backend
  struct
  {
    struct wlr_backend *libinput_backend;
    struct wlr_output *output;
    struct wlr_keyboard *virtual_kbd;
  } headless;

  struct xkb_context *xkb_context;
  struct xkb_keymap *default_keymap;

  struct wlr_keyboard vr_keyboard;
  struct wlr_input_device vr_keyboard_device;

  struct wlr_xdg_shell *xdg_shell;

  struct wl_seat *remote_seat;
  struct wl_pointer *remote_pointer;
  struct zwp_pointer_constraints_v1 *remote_pointer_constraints;

  struct wl_list views;

  struct wlr_seat *seat;
  struct wlr_xcursor_manager *cursor_mgr;
  struct wxrd_cursor cursor;

  bool rendering;
  bool framecycle;

  enum wxrd_seatop seatop;
  struct
  {
    int start_w;
    int start_h;
    float start_absolute_x;
    float start_absolute_y;
  } seatop_resize;

  struct wl_list keyboards;
  struct wl_list pointers;

  struct wxrd_xwayland xwayland;
  struct wl_listener xwayland_surface;
  struct wl_listener xwayland_ready;

  struct wl_listener new_input;
  struct wl_listener new_output;
  struct wl_listener new_xdg_surface;
  struct wl_listener new_xr_surface;
  struct wl_listener request_set_cursor;
  struct wl_listener request_set_selection;
  struct wl_listener request_set_primary_selection;
};

static inline double
timespec_to_msec_f (const struct timespec *a)
{
  return (double)a->tv_sec * 1000. + (double)a->tv_nsec / 1000000.;
}

static inline int64_t
get_now ()
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return timespec_to_msec_f (&now);
}

#endif
