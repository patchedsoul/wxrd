/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "input.h"
#include "server.h"
#include "view.h"
#include <wlr/util/log.h>

#include "backend.h"

static const struct wxrd_view_interface xdg_shell_view_impl;

struct wxrd_xdg_shell_view *
xdg_shell_view_from_view (struct wxrd_view *view)
{
  assert (view->impl == &xdg_shell_view_impl);
  return (struct wxrd_xdg_shell_view *)view;
}

static struct wxrd_xdg_shell_view *
xdg_shell_view_from_surface (struct wlr_xdg_surface *surf)
{
  struct wxrd_view *view = surf->data;
  if (view == NULL) {
    wlr_log (WLR_ERROR, "parent view was NULL");
    return NULL;
  }

  assert (view->impl == &xdg_shell_view_impl);
  return (struct wxrd_xdg_shell_view *)view;
}

static void
for_each_surface (struct wxrd_view *view,
                  wlr_surface_iterator_func_t iterator,
                  void *user_data)
{
  struct wxrd_xdg_shell_view *xdg_view = xdg_shell_view_from_view (view);
  wlr_xdg_surface_for_each_surface (xdg_view->xdg_surface, iterator,
                                    user_data);
}

static void
set_activated (struct wxrd_view *view, bool activated)
{
  struct wxrd_xdg_shell_view *xdg_view = xdg_shell_view_from_view (view);
  wlr_xdg_toplevel_set_activated (xdg_view->xdg_surface, activated);
}

static void
close_view (struct wxrd_view *view)
{
  struct wxrd_xdg_shell_view *xdg_view = xdg_shell_view_from_view (view);
  wlr_xdg_toplevel_send_close (xdg_view->xdg_surface);
}

static void
get_content_rect_size_from_xdg_surface (struct wlr_xdg_surface *xdg_surface,
                                        int *width,
                                        int *height)
{
  *width = xdg_surface->current.geometry.width;
  *height = xdg_surface->current.geometry.height;
}

static void
get_content_rect_size (struct wxrd_view *view, int *width, int *height)
{
  struct wxrd_xdg_shell_view *xdg_view = xdg_shell_view_from_view (view);
  get_content_rect_size_from_xdg_surface (xdg_view->xdg_surface, width,
                                          height);
}

static void
get_size (struct wxrd_view *view, int *width, int *height)
{
  get_content_rect_size (view, width, height);
}

static void
set_size (struct wxrd_view *view, int width, int height)
{
  struct wxrd_xdg_shell_view *xdg_view = xdg_shell_view_from_view (view);
  if (width < 100) {
    width = 100;
  }
  if (height < 100) {
    height = 100;
  }
  if (width > 8192) {
    width = 8192;
  }
  if (height > 8192) {
    height = 8192;
  }
  wlr_xdg_toplevel_set_size (xdg_view->xdg_surface, width, height);
}

static const struct wxrd_view_interface xdg_shell_view_impl = {
  .for_each_surface = for_each_surface,
  .set_activated = set_activated,
  .close = close_view,
  .get_size = get_size,
  .set_size = set_size,
};

static void
handle_xdg_surface_map (struct wl_listener *listener, void *data)
{
  struct wxrd_xdg_shell_view *view = wl_container_of (listener, view, map);
  // struct wlr_xdg_surface *xdg_surface =
  // wlr_xdg_surface_from_wlr_surface(view->base.surface);


  if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    view->base.parent = NULL;
  } else if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
    struct wlr_surface *wlr_surf_parent = view->xdg_surface->popup->parent;
    struct wlr_xdg_surface *xdg_surf_parent
        = wlr_xdg_surface_from_wlr_surface (wlr_surf_parent);
    struct wxrd_xdg_shell_view *parent_view
        = xdg_shell_view_from_surface (xdg_surf_parent);

    wlr_log (WLR_DEBUG, "parent surf %p", (void *)wlr_surf_parent);
    if (parent_view) {
      struct wlr_xdg_surface *xdg_surf_parent
          = wlr_xdg_surface_from_wlr_surface (wlr_surf_parent);

      // don't push the popup menu inside some
      // constraints like edges on a monitor
      struct wlr_box big_box
          = { .x = -10000, .y = -10000, .width = 20000, .height = 20000 };

      wlr_xdg_popup_unconstrain_from_box (view->xdg_surface->popup, &big_box);
      struct wlr_xdg_positioner *pos = &view->xdg_surface->popup->positioner;

      int content_width, content_height;
      get_content_rect_size_from_xdg_surface (xdg_surf_parent, &content_width,
                                              &content_height);

      int parent_center_x = content_width / 2;
      int parent_center_y = content_height / 2;

      int child_center_x = pos->anchor_rect.x + pos->size.width / 2;
      int child_center_y = pos->anchor_rect.y + pos->size.height / 2;

      graphene_point_t offset = { .x = 0, .y = 0 };
      // TODO what do we need to do for other
      // anchors?
      switch (pos->anchor) {
      case XDG_POSITIONER_ANCHOR_TOP_LEFT:
      default:
        offset.x = child_center_x - parent_center_x;
        offset.y = -(child_center_y - parent_center_y);
      }

      view->base.parent = &parent_view->base;
      view->base.offset_to_parent = offset;

      wlr_log (WLR_DEBUG,
               "Found parent, parent center %d,%d, "
               "child center %d,%d offset %f,%f",
               parent_center_x, parent_center_y, child_center_x,
               child_center_y, offset.x, offset.y);
    }
  } else {
    wlr_log (WLR_DEBUG, "Did not find parent");
  }

  const char *title = NULL;
  if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    title = view->xdg_surface->toplevel->title;
  } else {
    title = "popup window";
  }

  if (!title) {
    title = "unnamed window";
  }

  view_update_title (&view->base, title);

  wlr_xdg_surface_get_geometry (view->xdg_surface, &view->base.geometry);


  view_map (&view->base);

  wlr_log (WLR_DEBUG, "Added window %p", (void *)view->base.window);
}

static void
handle_xdg_surface_unmap (struct wl_listener *listener, void *data)
{
  struct wxrd_xdg_shell_view *view = wl_container_of (listener, view, unmap);
  view_unmap (&view->base);
}

static void
handle_xdg_surface_destroy (struct wl_listener *listener, void *data)
{
  struct wxrd_xdg_shell_view *view = wl_container_of (listener, view, destroy);
  wxrd_view_finish (&view->base);
  free (view);
}

static void
handle_new_xdg_surface (struct wl_listener *listener, void *data)
{
  struct wxrd_server *server
      = wl_container_of (listener, server, new_xdg_surface);
  struct wlr_xdg_surface *xdg_surface = data;

  struct wxrd_xdg_shell_view *view
      = calloc (1, sizeof (struct wxrd_xdg_shell_view));
  wxrd_view_init (&view->base, server, WXRD_VIEW_XDG_SHELL,
                  &xdg_shell_view_impl);
  view->base.wlr_xdg_surface = xdg_surface;

  xdg_surface->data = view;
  view->xdg_surface = xdg_surface;

  view->map.notify = handle_xdg_surface_map;
  wl_signal_add (&xdg_surface->events.map, &view->map);

  view->unmap.notify = handle_xdg_surface_unmap;
  wl_signal_add (&xdg_surface->events.unmap, &view->unmap);

  view->destroy.notify = handle_xdg_surface_destroy;
  wl_signal_add (&xdg_surface->events.destroy, &view->destroy);

  if (view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
    wxrd_set_focus (&view->base);
  }
}

void
wxrd_xdg_shell_init (struct wxrd_server *server)
{
  server->xdg_shell = wlr_xdg_shell_create (server->wl_display);
  server->new_xdg_surface.notify = handle_new_xdg_surface;
  wl_signal_add (&server->xdg_shell->events.new_surface,
                 &server->new_xdg_surface);
}
