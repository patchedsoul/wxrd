/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2016-2017 Drew DeVault
 * Author: Christoph Haag <christoph.haag@collabora.com>
 *
 * Inspired by sway/desktop/xwayland.c
 *
 * SPDX-License-Identifier: MIT
 */

#include "xwayland.h"

#include "input.h"
#include "server.h"
#include "view.h"
#include <wlr/util/log.h>

#if 0
void seat_set_focus_surface(struct wlr_seat *seat,
		struct wlr_surface *surface, bool unfocus) {
	if (seat->has_focus && unfocus) {
		struct sway_node *focus = seat_get_focus(seat);
		seat_send_unfocus(focus, seat);
		seat->has_focus = false;
	}

	if (surface) {
		seat_keyboard_notify_enter(seat, surface);
	} else {
		wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
	}

	sway_input_method_relay_set_focus(&seat->im_relay, surface);
	seat_tablet_pads_notify_enter(seat, surface);
}
#endif

void
view_init (struct wxrd_view *view,
           struct wxrd_server *server,
           enum wxrd_view_type type,
           const struct wxrd_view_interface *impl)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  // TODO view->executed_criteria = create_list();
  // TODO wl_list_init(&view->saved_buffers);
  // TODO view->allow_request_urgent = true;
  // TODO view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DEFAULT;
  wl_signal_init (&view->events.unmap);
}

static void
get_constraints (struct wxrd_view *view,
                 double *min_width,
                 double *max_width,
                 double *min_height,
                 double *max_height)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
  struct wlr_xwayland_surface_size_hints *size_hints = surface->size_hints;

  if (size_hints == NULL) {
    *min_width = DBL_MIN;
    *max_width = DBL_MAX;
    *min_height = DBL_MIN;
    *max_height = DBL_MAX;
    return;
  }

  *min_width = size_hints->min_width > 0 ? size_hints->min_width : DBL_MIN;
  *max_width = size_hints->max_width > 0 ? size_hints->max_width : DBL_MAX;
  *min_height = size_hints->min_height > 0 ? size_hints->min_height : DBL_MIN;
  *max_height = size_hints->max_height > 0 ? size_hints->max_height : DBL_MAX;
}
static struct wxrd_xwayland_view *
xwayland_view_from_view (struct wxrd_view *view)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (view->type != WXRD_VIEW_XWAYLAND) {
    wlr_log (WLR_ERROR, "Expected xwayland view");
    return NULL;
  }
  return (struct wxrd_xwayland_view *)view;
}
static const char *
get_string_prop (struct wxrd_view *view, enum wxrd_view_prop prop)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (view) == NULL) {
    return NULL;
  }
  switch (prop) {
  case VIEW_PROP_TITLE: return view->wlr_xwayland_surface->title;
  case VIEW_PROP_CLASS: return view->wlr_xwayland_surface->class;
  case VIEW_PROP_INSTANCE: return view->wlr_xwayland_surface->instance;
  case VIEW_PROP_WINDOW_ROLE: return view->wlr_xwayland_surface->role;
  default: return NULL;
  }
}
static uint32_t
get_int_prop (struct wxrd_view *view, enum wxrd_view_prop prop)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (view) == NULL) {
    return 0;
  }
  switch (prop) {
  case VIEW_PROP_X11_WINDOW_ID: return view->wlr_xwayland_surface->window_id;
  case VIEW_PROP_X11_PARENT_ID:
    if (view->wlr_xwayland_surface->parent) {
      return view->wlr_xwayland_surface->parent->window_id;
    }
    return 0;
  case VIEW_PROP_WINDOW_TYPE:
    if (view->wlr_xwayland_surface->window_type_len == 0) {
      return 0;
    }
    return view->wlr_xwayland_surface->window_type[0];
  default: return 0;
  }
}
static uint32_t
configure (struct wxrd_view *view, double lx, double ly, int width, int height)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view = xwayland_view_from_view (view);
  if (xwayland_view == NULL) {
    return 0;
  }
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;

  wlr_xwayland_surface_configure (xsurface, lx, ly, width, height);

  // xwayland doesn't give us a serial for the configure
  return 0;
}
static void
set_activated (struct wxrd_view *view, bool activated)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (view) == NULL) {
    return;
  }
  struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;

  if (activated && surface->minimized) {
    wlr_xwayland_surface_set_minimized (surface, false);
  }

  wlr_xwayland_surface_activate (surface, activated);
}

static void
set_tiled (struct wxrd_view *view, bool tiled)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (view) == NULL) {
    return;
  }
  struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
  wlr_xwayland_surface_set_maximized (surface, tiled);
}

static void
set_fullscreen (struct wxrd_view *view, bool fullscreen)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (view) == NULL) {
    return;
  }
  struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
  wlr_xwayland_surface_set_fullscreen (surface, fullscreen);
}

static bool
wants_floating (struct wxrd_view *view)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (view) == NULL) {
    return false;
  }
#if 0 // TODO
	struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
	struct wxrd_xwayland *xwayland = &server.xwayland;

	if (surface->modal) {
		return true;
	}

	for (size_t i = 0; i < surface->window_type_len; ++i) {
		xcb_atom_t type = surface->window_type[i];
		if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_DIALOG] ||
				type == xwayland->atoms[NET_WM_WINDOW_TYPE_UTILITY] ||
				type == xwayland->atoms[NET_WM_WINDOW_TYPE_TOOLBAR] ||
				type == xwayland->atoms[NET_WM_WINDOW_TYPE_SPLASH]) {
			return true;
		}
	}

	struct wlr_xwayland_surface_size_hints *size_hints = surface->size_hints;
	if (size_hints != NULL &&
			size_hints->min_width > 0 && size_hints->min_height > 0 &&
			(size_hints->max_width == size_hints->min_width ||
			size_hints->max_height == size_hints->min_height)) {
		return true;
	}
#endif
  return false;
}

static void
handle_set_decorations (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, set_decorations);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;

  bool csd = xsurface->decorations != WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
  // TODO view_update_csd_from_client(view, csd);
}

static bool
is_transient_for (struct wxrd_view *child, struct wxrd_view *ancestor)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (child) == NULL) {
    return false;
  }
  struct wlr_xwayland_surface *surface = child->wlr_xwayland_surface;
  while (surface) {
    if (surface->parent == ancestor->wlr_xwayland_surface) {
      return true;
    }
    surface = surface->parent;
  }
  return false;
}

static void
_close (struct wxrd_view *view)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  if (xwayland_view_from_view (view) == NULL) {
    return;
  }
  wlr_xwayland_surface_close (view->wlr_xwayland_surface);
}

static void
destroy (struct wxrd_view *view)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view = xwayland_view_from_view (view);
  if (xwayland_view == NULL) {
    return;
  }
  free (xwayland_view);
}
static const struct wxrd_view_interface view_impl = {
  .get_constraints = get_constraints,
  .get_string_prop = get_string_prop,
  .get_int_prop = get_int_prop,
  .configure = configure,
  .set_activated = set_activated,
  .set_tiled = set_tiled,
  .set_fullscreen = set_fullscreen,
  .wants_floating = wants_floating,
  .is_transient_for = is_transient_for,
  .close = _close,
  .destroy = destroy,
};


static void
handle_destroy (struct wl_listener *listener, void *data)
{

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, destroy);
  struct wxrd_view *view = &xwayland_view->view;

  wlr_log (WLR_DEBUG, "%s view %p %s", __FUNCTION__, (void *)view,
           view->title);

  wl_list_remove (&xwayland_view->destroy.link);
  wl_list_remove (&xwayland_view->request_configure.link);
  wl_list_remove (&xwayland_view->request_fullscreen.link);
  wl_list_remove (&xwayland_view->request_minimize.link);
  wl_list_remove (&xwayland_view->request_move.link);
  wl_list_remove (&xwayland_view->request_resize.link);
  wl_list_remove (&xwayland_view->request_activate.link);
  wl_list_remove (&xwayland_view->set_title.link);
  wl_list_remove (&xwayland_view->set_class.link);
  wl_list_remove (&xwayland_view->set_role.link);
  wl_list_remove (&xwayland_view->set_window_type.link);
  wl_list_remove (&xwayland_view->set_hints.link);
  wl_list_remove (&xwayland_view->set_decorations.link);
  wl_list_remove (&xwayland_view->map.link);
  wl_list_remove (&xwayland_view->unmap.link);
  wl_list_remove (&xwayland_view->override_redirect.link);

  // TODO view_begin_destroy(&xwayland_view->view);
  wxrd_view_finish (view);
}
static void
handle_request_configure (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, request_configure);
  struct wlr_xwayland_surface_configure_event *ev = data;
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    wlr_xwayland_surface_configure (xsurface, ev->x, ev->y, ev->width,
                                    ev->height);
    return;
  }
#if 0 // TODO
  if (container_is_floating(view->container)) {
    // Respect minimum and maximum sizes
    view->natural_width = ev->width;
    view->natural_height = ev->height;
    container_floating_resize_and_center(view->container);

    configure(view, view->container->pending.content_x,
              view->container->pending.content_y,
              view->container->pending.content_width,
              view->container->pending.content_height);
    node_set_dirty(&view->container->node);
  } else {
    configure(view, view->container->current.content_x,
              view->container->current.content_y,
              view->container->current.content_width,
              view->container->current.content_height);
  }
#endif
}
static void
handle_request_fullscreen (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, request_fullscreen);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO
  container_set_fullscreen(view->container, xsurface->fullscreen);

  arrange_root();
  transaction_commit_dirty();
#endif
}
static void
handle_request_minimize (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, request_minimize);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO
  struct wlr_xwayland_minimize_event *e = data;
  struct wxrd_seat *seat = input_manager_current_seat();
  bool focused = seat_get_focus(seat) == &view->container->node;
  wlr_xwayland_surface_set_minimized(xsurface, !focused && e->minimize);
#endif
}
static void
handle_request_activate (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, request_activate);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO
  view_request_activate(view);

  transaction_commit_dirty();
#endif
}
static void
handle_request_move (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, request_move);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO
  if (!container_is_floating(view->container)) {
    return;
  }
  struct wxrd_seat *seat = input_manager_current_seat();
  seatop_begin_move_floating(seat, view->container);
#endif
}
static void
handle_request_resize (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, request_resize);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO
  if (!container_is_floating(view->container)) {
    return;
  }
  struct wlr_xwayland_resize_event *e = data;
  struct wxrd_seat *seat = input_manager_current_seat();
  seatop_begin_resize_floating(seat, view->container, e->edges);
#endif
}
static void
handle_set_title (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, set_title);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    wlr_log (WLR_DEBUG, "not setting new title on unmapped window");
    return;
  }

  const char *title = "unknown";
  if (view->impl->get_string_prop) {
    title = view->impl->get_string_prop (view, VIEW_PROP_TITLE);
  }
  wlr_log (WLR_DEBUG, "new title: %s", title);

  view_update_title (view, title);

#if 0 // TODO
  view_execute_criteria(view);
#endif
}
static void
handle_set_class (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, set_class);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO
  view_execute_criteria(view);
#endif
}

static void
handle_set_role (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, set_role);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO

  view_execute_criteria(view);
#endif
}

static void
handle_set_window_type (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, set_window_type);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO

  view_execute_criteria(view);
#endif
}

static void
handle_set_hints (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, set_hints);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  if (!xsurface->mapped) {
    return;
  }
#if 0 // TODO

  if (!xsurface->hints_urgency && view->urgent_timer) {
    // The view is in the timeout period. We'll ignore the request to
    // unset urgency so that the view remains urgent until the timer clears
    // it.
    return;
  }
  if (view->allow_request_urgent) {
    view_set_urgent(view, (bool)xsurface->hints_urgency);
  }
#endif
}
static void
handle_unmap (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, unmap);
  struct wxrd_view *view = &xwayland_view->view;

  if (view->wlr_xwayland_surface) {
    view_unmap (view);
  }
  wl_list_remove (&xwayland_view->commit.link);
}
static void
get_geometry (struct wxrd_view *view, struct wlr_box *box)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wlr_surface *surface = view_get_surface (view);

  box->x = box->y = 0;
  if (surface) {
    box->width = surface->current.width;
    box->height = surface->current.height;
  } else {
    box->width = 0;
    box->height = 0;
  }
}
static void
handle_commit (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, commit);
  struct wxrd_view *view = &xwayland_view->view;
  struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
  struct wlr_surface_state *state = &xsurface->surface->current;

  struct wlr_box new_geo;
  get_geometry (view, &new_geo);
  bool new_size = new_geo.width != view->geometry.width
                  || new_geo.height != view->geometry.height
                  || new_geo.x != view->geometry.x
                  || new_geo.y != view->geometry.y;

#if 0
  if (new_size) {
    // The client changed its surface size in this commit. For floating
    // containers, we resize the container to match. For tiling containers,
    // we only recenter the surface.
    desktop_damage_view(view);
    memcpy(&view->geometry, &new_geo, sizeof(struct wlr_box));
    if (container_is_floating(view->container)) {
      view_update_size(view);
      transaction_commit_dirty_client();
    } else {
      view_center_surface(view);
    }
    desktop_damage_view(view);
  }

  if (view->container->node.instruction) {
    transaction_notify_view_ready_by_geometry(view,
                                              xsurface->x, xsurface->y, state->width, state->height);
  }

  view_damage_from(view);
#endif
}
static void
handle_map (struct wl_listener *listener, void *data)
{

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, map);
  struct wlr_xwayland_surface *xsurface = data;
  struct wxrd_view *view = &xwayland_view->view;
#if 0
  view->natural_width = xsurface->width;
  view->natural_height = xsurface->height;
#endif
  // Wire up the commit listener here, because xwayland map/unmap can change
  // the underlying wlr_surface
  wl_signal_add (&xsurface->surface->events.commit, &xwayland_view->commit);
  xwayland_view->commit.notify = handle_commit;

  view->parent = NULL;
  view_update_title (view, xsurface->title);

  view->geometry.x = 0;
  view->geometry.y = 0;
  view->geometry.width = xsurface->width;
  view->geometry.height = xsurface->height;
  wlr_log (WLR_DEBUG, "xwayland %dx%d", xsurface->width, xsurface->height);

  view_map (view);
  wlr_log (WLR_DEBUG, "%s view %p %s", __FUNCTION__, (void *)view,
           view->title);

#if 0
  // Put it back into the tree
  view_map(view, xsurface->surface, xsurface->fullscreen, NULL, false);

  transaction_commit_dirty();
#endif
}

static void
handle_override_redirect (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_xwayland_view *xwayland_view
      = wl_container_of (listener, xwayland_view, override_redirect);
  struct wlr_xwayland_surface *xsurface = data;
  struct wxrd_view *view = &xwayland_view->view;

  bool mapped = xsurface->mapped;
  if (mapped) {
    handle_unmap (&xwayland_view->unmap, NULL);
  }

  handle_destroy (&xwayland_view->destroy, view);
  xsurface->data = NULL;
  // struct wxrd_xwayland_unmanaged *unmanaged = create_unmanaged
  // (xwayland_view->server, xsurface); if (mapped) {
  //  unmanaged_handle_map (&unmanaged->map, xsurface);
  //}
}
struct wxrd_xwayland_view *
create_xwayland_view (struct wxrd_server *server,
                      struct wlr_xwayland_surface *xsurface)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  wlr_log (WLR_DEBUG, "New xwayland surface title='%s' class='%s'",
           xsurface->title, xsurface->class);

  struct wxrd_xwayland_view *xwayland_view
      = calloc (1, sizeof (struct wxrd_xwayland_view));
  if (!xwayland_view) {
    wlr_log (WLR_ERROR, "Failed to allocate view");
    return NULL;
  }
  xwayland_view->server = server;

  wxrd_view_init (&xwayland_view->view, server, WXRD_VIEW_XWAYLAND,
                  &view_impl);
  xwayland_view->view.wlr_xwayland_surface = xsurface;

  // xwayland specific stuff
  view_init (&xwayland_view->view, server, WXRD_VIEW_XWAYLAND, &view_impl);

  xwayland_view->view.wlr_xwayland_surface = xsurface;

  wl_signal_add (&xsurface->events.destroy, &xwayland_view->destroy);
  xwayland_view->destroy.notify = handle_destroy;

  wl_signal_add (&xsurface->events.request_configure,
                 &xwayland_view->request_configure);
  xwayland_view->request_configure.notify = handle_request_configure;

  wl_signal_add (&xsurface->events.request_fullscreen,
                 &xwayland_view->request_fullscreen);
  xwayland_view->request_fullscreen.notify = handle_request_fullscreen;

  wl_signal_add (&xsurface->events.request_minimize,
                 &xwayland_view->request_minimize);
  xwayland_view->request_minimize.notify = handle_request_minimize;

  wl_signal_add (&xsurface->events.request_activate,
                 &xwayland_view->request_activate);
  xwayland_view->request_activate.notify = handle_request_activate;

  wl_signal_add (&xsurface->events.request_move, &xwayland_view->request_move);
  xwayland_view->request_move.notify = handle_request_move;

  wl_signal_add (&xsurface->events.request_resize,
                 &xwayland_view->request_resize);
  xwayland_view->request_resize.notify = handle_request_resize;

  wl_signal_add (&xsurface->events.set_title, &xwayland_view->set_title);
  xwayland_view->set_title.notify = handle_set_title;

  wl_signal_add (&xsurface->events.set_class, &xwayland_view->set_class);
  xwayland_view->set_class.notify = handle_set_class;

  wl_signal_add (&xsurface->events.set_role, &xwayland_view->set_role);
  xwayland_view->set_role.notify = handle_set_role;

  wl_signal_add (&xsurface->events.set_window_type,
                 &xwayland_view->set_window_type);
  xwayland_view->set_window_type.notify = handle_set_window_type;

  wl_signal_add (&xsurface->events.set_hints, &xwayland_view->set_hints);
  xwayland_view->set_hints.notify = handle_set_hints;

  wl_signal_add (&xsurface->events.set_decorations,
                 &xwayland_view->set_decorations);
  xwayland_view->set_decorations.notify = handle_set_decorations;

  wl_signal_add (&xsurface->events.unmap, &xwayland_view->unmap);
  xwayland_view->unmap.notify = handle_unmap;

  wl_signal_add (&xsurface->events.map, &xwayland_view->map);
  xwayland_view->map.notify = handle_map;

  wl_signal_add (&xsurface->events.set_override_redirect,
                 &xwayland_view->override_redirect);
  xwayland_view->override_redirect.notify = handle_override_redirect;

  xsurface->data = xwayland_view;

  return xwayland_view;
}

void
handle_xwayland_surface (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wlr_xwayland_surface *xsurface = data;

  struct wxrd_server *server
      = wl_container_of (listener, server, xwayland_surface);

  create_xwayland_view (server, xsurface);
}

static const char *atom_map[ATOM_LAST] = {
  [NET_WM_WINDOW_TYPE_NORMAL] = "_NET_WM_WINDOW_TYPE_NORMAL",
  [NET_WM_WINDOW_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
  [NET_WM_WINDOW_TYPE_UTILITY] = "_NET_WM_WINDOW_TYPE_UTILITY",
  [NET_WM_WINDOW_TYPE_TOOLBAR] = "_NET_WM_WINDOW_TYPE_TOOLBAR",
  [NET_WM_WINDOW_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
  [NET_WM_WINDOW_TYPE_MENU] = "_NET_WM_WINDOW_TYPE_MENU",
  [NET_WM_WINDOW_TYPE_DROPDOWN_MENU] = "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
  [NET_WM_WINDOW_TYPE_POPUP_MENU] = "_NET_WM_WINDOW_TYPE_POPUP_MENU",
  [NET_WM_WINDOW_TYPE_TOOLTIP] = "_NET_WM_WINDOW_TYPE_TOOLTIP",
  [NET_WM_WINDOW_TYPE_NOTIFICATION] = "_NET_WM_WINDOW_TYPE_NOTIFICATION",
  [NET_WM_STATE_MODAL] = "_NET_WM_STATE_MODAL",
};
void
handle_xwayland_ready (struct wl_listener *listener, void *data)
{
  wlr_log (WLR_DEBUG, "%s", __FUNCTION__);

  struct wxrd_server *server
      = wl_container_of (listener, server, xwayland_ready);
  struct wxrd_xwayland *xwayland = &server->xwayland;

  xwayland->server = server;

  xcb_connection_t *xcb_conn = xcb_connect (NULL, NULL);
  int err = xcb_connection_has_error (xcb_conn);
  if (err) {
    wlr_log (WLR_ERROR, "XCB connect failed: %d", err);
    return;
  }

  xcb_intern_atom_cookie_t cookies[ATOM_LAST];
  for (size_t i = 0; i < ATOM_LAST; i++) {
    cookies[i]
        = xcb_intern_atom (xcb_conn, 0, strlen (atom_map[i]), atom_map[i]);
  }
  for (size_t i = 0; i < ATOM_LAST; i++) {
    xcb_generic_error_t *error = NULL;
    xcb_intern_atom_reply_t *reply
        = xcb_intern_atom_reply (xcb_conn, cookies[i], &error);
    if (reply != NULL && error == NULL) {
      xwayland->atoms[i] = reply->atom;
    }
    free (reply);

    if (error != NULL) {
      wlr_log (WLR_ERROR, "could not resolve atom %s, X11 error code %d",
               atom_map[i], error->error_code);
      free (error);
      break;
    }
  }

  wlr_xwayland_set_seat (server->xwayland.wlr_xwayland, server->seat);

  xcb_disconnect (xcb_conn);
}
