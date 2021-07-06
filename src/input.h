/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef _WXRC_INPUT_H
#define _WXRC_INPUT_H

#include <cglm/cglm.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_xcursor_manager.h>

struct wxrd_server;

enum wxrd_seatop
{
  WXRC_SEATOP_DEFAULT,
  WXRC_SEATOP_MOVE,
  WXRC_SEATOP_RESIZE,
};

struct wxrd_keyboard
{
  struct wl_list link;
  struct wxrd_server *server;
  struct wlr_input_device *device;

  struct wl_listener modifiers;
  struct wl_listener key;
};

struct wxrd_pointer
{
  struct wl_list link;
  struct wxrd_server *server;
  struct wlr_input_device *device;

  struct wl_listener motion;
  struct wl_listener motion_absolute;
  struct wl_listener button;
  struct wl_listener axis;
  struct wl_listener frame;
};

struct wxrd_cursor
{
  struct wxrd_server *server;

  struct wlr_xcursor_image *xcursor_image;
  struct wlr_texture *xcursor_texture;

  struct wlr_surface *surface;
  int hotspot_x, hotspot_y;
  struct wl_listener surface_destroy;
};

void
wxrd_input_init (struct wxrd_server *server);

void
wxrd_update_pointer (struct wxrd_server *server, uint32_t time);

void
wxrd_cursor_set_xcursor (struct wxrd_cursor *cursor,
                         struct wlr_xcursor *xcursor);

void
wxrd_cursor_set_surface (struct wxrd_cursor *cursor,
                         struct wlr_surface *surface,
                         int hotspot_x,
                         int hotspot_y);

struct wlr_texture *
wxrd_cursor_get_texture (struct wxrd_cursor *cursor,
                         int *hotspot_x,
                         int *hotspot_y,
                         int *scale);

#endif
