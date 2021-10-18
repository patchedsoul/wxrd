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

  struct wlr_backend *backend;
  struct wxrd_xr_backend *xr_backend;

  // used with noop backend
  struct
  {
    struct wlr_backend *libinput_backend;
    struct wlr_output *output;
    struct wlr_keyboard *virtual_kbd;
  } headless;

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

#endif
