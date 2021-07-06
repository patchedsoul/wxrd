/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef _WXRC_BACKEND_H
#define _WXRC_BACKEND_H

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <wlr/backend/interface.h>
#include <wlr/render/wlr_renderer.h>

#include <xrd.h>

struct wxrd_xr_view
{
  GLuint *framebuffers;
  GLuint depth_buffer;

  uint32_t width;
  uint32_t height;

  struct wxrd_zxr_view_v1 *wl_view;
};

struct wxrd_xr_backend
{
  struct wlr_backend base;

  bool started;

  struct wlr_egl *egl;
  struct wlr_renderer *renderer;

  uint32_t nviews;
  struct wxrd_xr_view *views;

  struct wl_listener local_display_destroy;

  XrdShell *xrd_shell;

  int num_windows;

  GulkanTexture *cursor_texture;
  guint64 click_source;
  guint64 move_source;
  guint64 keyboard_source;
  guint64 quit_source;

  // absolute position of the pointer in [0,1]
  struct
  {
    float x;
    float y;
  } pointer_absolute;
};

bool
wxrd_backend_is_xr (struct wlr_backend *wlr_backend);

struct wxrd_xr_backend *
wxrd_xr_backend_create (struct wl_display *display,
                        struct wlr_renderer *renderer);

#endif
