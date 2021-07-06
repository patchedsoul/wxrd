/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef WXRC_VIEW_H
#define WXRC_VIEW_H
#include <cglm/cglm.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <wlr/xwayland.h>

#include <xrd.h>

enum wxrd_view_type
{
  WXRD_VIEW_XDG_SHELL,
  WXRD_VIEW_XWAYLAND,
};

enum wxrd_view_prop
{
  VIEW_PROP_TITLE,
  VIEW_PROP_APP_ID,
  VIEW_PROP_CLASS,
  VIEW_PROP_INSTANCE,
  VIEW_PROP_WINDOW_TYPE,
  VIEW_PROP_WINDOW_ROLE,
  VIEW_PROP_X11_WINDOW_ID,
  VIEW_PROP_X11_PARENT_ID,
};

struct wxrd_server;

struct wxrd_view;

struct wxrd_xdg_shell_view *
xdg_shell_view_from_view (struct wxrd_view *view);

struct wxrd_view_interface
{
  void (*get_constraints) (struct wxrd_view *view,
                           double *min_width,
                           double *max_width,
                           double *min_height,
                           double *max_height);
  const char *(*get_string_prop) (struct wxrd_view *view,
                                  enum wxrd_view_prop prop);
  uint32_t (*get_int_prop) (struct wxrd_view *view, enum wxrd_view_prop prop);
  uint32_t (*configure) (
      struct wxrd_view *view, double lx, double ly, int width, int height);
  void (*set_activated) (struct wxrd_view *view, bool activated);
  void (*set_tiled) (struct wxrd_view *view, bool tiled);
  void (*set_fullscreen) (struct wxrd_view *view, bool fullscreen);
  void (*set_resizing) (struct wxrd_view *view, bool resizing);
  bool (*wants_floating) (struct wxrd_view *view);
  void (*for_each_surface) (struct wxrd_view *view,
                            wlr_surface_iterator_func_t iterator,
                            void *user_data);
  void (*for_each_popup_surface) (struct wxrd_view *view,
                                  wlr_surface_iterator_func_t iterator,
                                  void *user_data);
  bool (*is_transient_for) (struct wxrd_view *child,
                            struct wxrd_view *ancestor);
  void (*close) (struct wxrd_view *view);
  void (*close_popups) (struct wxrd_view *view);
  void (*destroy) (struct wxrd_view *view);

  void (*get_size) (struct wxrd_view *view, int *width, int *height);
  void (*set_size) (struct wxrd_view *view, int width, int height);
};

struct wxrd_view
{
  struct wxrd_server *server;
  const struct wxrd_view_interface *impl;

  // wxrd_view_type
  struct wlr_xdg_surface *wlr_xdg_surface;
  struct wlr_xwayland_surface *wlr_xwayland_surface;

  bool mapped;
  XrdWindow *window;

  const char *title;

  // must be set before calling view_map()
  struct wlr_box geometry;

  // null if there is no parent
  // must be set before calling view_map()
  struct wxrd_view *parent;
  graphene_point_t offset_to_parent;

  struct wl_list link;

  enum wxrd_view_type type;
  struct
  {
    struct wl_signal unmap;
  } events;
};

struct wxrd_xdg_shell_view
{
  struct wxrd_view base;
  struct wxrd_server *server;
  struct wlr_xdg_surface *xdg_surface;

  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener request_move;
  struct wl_listener request_resize;
};


void
wxrd_xdg_shell_init (struct wxrd_server *server);
void
wxrd_view_init (struct wxrd_view *view,
                struct wxrd_server *server,
                enum wxrd_view_type type,
                const struct wxrd_view_interface *impl);
void
wxrd_view_finish (struct wxrd_view *view);

struct wxrd_view *
wxrd_get_focus (struct wxrd_server *server);

void
wxrd_set_focus (struct wxrd_view *view);

void
wxrd_focus_next_view (struct wxrd_server *server);

void
wxrd_view_begin_move (struct wxrd_view *view);

void
wxrd_view_close (struct wxrd_view *view);

/* Sets to zero if surface is not two dimensional */
/* TODO: 3D resize? */
void
wxrd_view_get_size (struct wxrd_view *view, int *width, int *height);
void
wxrd_view_set_size (struct wxrd_view *view, int width, int height);

void
wxrd_view_for_each_surface (struct wxrd_view *view,
                            wlr_surface_iterator_func_t iterator,
                            void *user_data);

void
view_map (struct wxrd_view *view);

void
view_unmap (struct wxrd_view *view);

struct wlr_surface *
view_get_surface (struct wxrd_view *view);

void
view_update_title (struct wxrd_view *view, const char *title);

#endif
