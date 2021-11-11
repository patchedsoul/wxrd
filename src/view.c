/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "view.h"
#include "server.h"
#include "backend.h"
#include <wlr/util/log.h>

#define WXRD_SURFACE_SCALE 200.0

void
wxrd_view_init (struct wxrd_view *view,
                struct wxrd_server *server,
                enum wxrd_view_type type,
                const struct wxrd_view_interface *impl)
{
  view->type = type;
  view->server = server;
  view->impl = impl;

  wl_list_insert (server->views.prev, &view->link);
}

void
wxrd_view_finish (struct wxrd_view *view)
{
  wlr_log (WLR_DEBUG, "finish view %p on thread %p", (void *)view,
           (void *)g_thread_self ());

  if (view->window) {
    wlr_log (WLR_DEBUG, "Closing window %p", (void *)view->window);

    xrd_shell_remove_window (view->server->xr_backend->xrd_shell,
                             view->window);
    xrd_window_close (view->window);
    g_object_unref (view->window);
  } else {
    wlr_log (WLR_ERROR, "Can't close window %p", (void *)view->window);
  }

  view->server->xr_backend->num_windows--;

  if (view == wxrd_get_focus (view->server)) {
    wlr_log (WLR_DEBUG, "Closed focused window, focusing next");
    wxrd_focus_next_view (view->server);
  }

  free (view->title);

  wl_list_remove (&view->link);
}

struct wxrd_view *
wxrd_get_focus (struct wxrd_server *server)
{
  if (wl_list_empty (&server->views)) {
    return NULL;
  }
  struct wxrd_view *view = wl_container_of (server->views.next, view, link);
  if (!view->mapped) {
    return NULL;
  }
  return view;
}

void
wxrd_set_focus (struct wxrd_view *view)
{
  if (view == NULL) {
    return;
  }

  struct wxrd_server *server = view->server;

  struct wxrd_view *prev_view = wxrd_get_focus (server);
  if (prev_view == view) {
    wlr_log (WLR_DEBUG, "refocusing %s", view->title);
    // return;
  }
  if (prev_view != NULL && prev_view->impl->set_activated) {
    prev_view->impl->set_activated (prev_view, false);
  }

  wl_list_remove (&view->link);
  wl_list_insert (&server->views, &view->link);

  struct wlr_surface *surface = view_get_surface (view);
  if (!surface) {
    wlr_log (WLR_ERROR, "can't set focus on NULL surface");
    return;
  }

  struct wlr_seat *seat = server->seat;
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard (seat);
  if (keyboard == NULL) {
    wlr_log (WLR_ERROR, "keyboard notify not possible on NULL keyboard");
  } else {
    wlr_seat_keyboard_notify_enter (seat, surface, keyboard->keycodes,
                                    keyboard->num_keycodes,
                                    &keyboard->modifiers);
  }

  if (view->impl->set_activated) {
    view->impl->set_activated (view, true);
  }
}

void
wxrd_focus_next_view (struct wxrd_server *server)
{
  struct wxrd_view *current_view
      = wl_container_of (server->views.next, current_view, link);
  struct wxrd_view *next_view
      = wl_container_of (current_view->link.next, next_view, link);
  wxrd_set_focus (next_view);
  wlr_log (WLR_DEBUG, "focused next view %s", next_view->title);
  wl_list_remove (&current_view->link);
  wl_list_insert (server->views.prev, &current_view->link);
}

void
wxrd_view_begin_move (struct wxrd_view *view)
{
  struct wxrd_server *server = view->server;
  if (wxrd_get_focus (server) != view
      || server->seatop != WXRC_SEATOP_DEFAULT) {
    return;
  }
  wlr_seat_pointer_clear_focus (server->seat);
  server->seatop = WXRC_SEATOP_MOVE;
}

void
wxrd_view_close (struct wxrd_view *view)
{
  if (view->impl->close) {
    view->impl->close (view);
  }
}

void
wxrd_view_get_size (struct wxrd_view *view, int *width, int *height)
{
  if (view->impl->get_size) {
    view->impl->get_size (view, width, height);
  } else {
    *width = *height = 0;
  }
}

void
wxrd_view_set_size (struct wxrd_view *view, int width, int height)
{
  if (view->impl->set_size) {
    view->impl->set_size (view, width, height);
  }
}

void
wxrd_view_for_each_surface (struct wxrd_view *view,
                            wlr_surface_iterator_func_t iterator,
                            void *user_data)
{
  if (view->impl->for_each_surface) {
    view->impl->for_each_surface (view, iterator, user_data);
  } else {
    struct wlr_surface *surface = view_get_surface (view);
    iterator (surface, 0, 0, user_data);
  }
}

// geometry is for window content, excluding decoration/shadow etc.
void
view_map (struct wxrd_view *view)
{
  // window size is based on
  uint32_t surface_width = view->geometry.width;
  uint32_t surface_height = view->geometry.height;

  XrdShell *xrd_shell = view->server->xr_backend->xrd_shell;
  G3kContext *g3k = xrd_shell_get_g3k (xrd_shell);

  XrdWindow *win
      = xrd_window_new_from_native (g3k, view->title, view, surface_width,
                                    surface_height, WXRD_SURFACE_SCALE);
  wlr_log (WLR_DEBUG, "New %dx%d window %s %p", surface_width, surface_height,
           view->title, (void *)win);

  if (win) {
    wlr_log (WLR_DEBUG, "New %dx%d window %s %p", surface_width,
             surface_height, view->title, (void *)win);
    view->mapped = true;

    float z_offset = (float)(view->server->xr_backend->num_windows++) / 10.;
    wlr_log (WLR_DEBUG, "z offset %f", z_offset);

    if (view->parent == NULL) {
      wlr_log (WLR_DEBUG, "is top level window");
      wxrd_set_focus (view);

      graphene_point3d_t p = { 0, 1, -2.5 + z_offset };
      graphene_matrix_t t;
      graphene_matrix_init_identity (&t);
      graphene_matrix_translate (&t, &p);

      xrd_window_set_transformation (win, &t);
      xrd_window_set_reset_transformation (win, &t);

    } else {
      wlr_log (WLR_DEBUG, "is child window of xrd parent %p",
               (void *)view->parent->window);
      if (view->parent) {
        XrdWindow *xrd_window_parent = view->parent->window;
        xrd_window_add_child (xrd_window_parent, win, &view->offset_to_parent);
        wlr_log (WLR_DEBUG, "Set xrd child %p for xrd parent %p", (void *)win,
                 (void *)view->parent->window);
      } else {
        wlr_log (WLR_DEBUG, "Did not find parent");
      }
    }
  }

  view->window = win;

  xrd_shell_add_window (view->server->xr_backend->xrd_shell, view->window,
                        view->parent == NULL, view);

  wlr_log (WLR_DEBUG, "Added window %p", (void *)view->window);
}

void
view_unmap (struct wxrd_view *view)
{
  wlr_log (WLR_DEBUG, "unmap view %p", (void *)view);
  view->mapped = false;

  struct wxrd_view *wview;
  wl_list_for_each (wview, &view->server->views, link)
  {
    if (wview->mapped) {
      wxrd_set_focus (wview);
      break;
    }
  }
}

struct wlr_surface *
view_get_surface (struct wxrd_view *view)
{
  switch (view->type) {
  case WXRD_VIEW_XDG_SHELL:
    if (view && view->wlr_xdg_surface) {
      return view->wlr_xdg_surface->surface;
    } else {
      return NULL;
    }
  case WXRD_VIEW_XWAYLAND:
    if (view && view->wlr_xwayland_surface) {
      return view->wlr_xwayland_surface->surface;
    } else {
      return NULL;
    }
  }
  return NULL;
}

void
view_update_title (struct wxrd_view *view, const char *title)
{
  view->title = strdup(title);
}
