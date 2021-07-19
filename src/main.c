/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

// setenv
#define __USE_XOPEN2K
#define _DEFAULT_SOURCE
#include <stdlib.h>

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>

#include <wayland-server.h>

#include <wlr/backend/interface.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "xwayland.h"

#include <wxrd-renderer.h>

#include <wlr/util/log.h>

#include <cglm/cglm.h>

#include <GLES2/gl2.h>

#include "backend.h"
#include "input.h"
#include "output.h"
#include "server.h"
#include "view.h"

// input codes like BTN_LEFT
#include <linux/input.h>

#define USE_SHARED_GLES_TEX 0
#define USE_DMABUF_TEX 1

static int
handle_signal (int sig, void *data)
{
  bool *running_ptr = data;
  *running_ptr = false;
  return 0;
}

static void
send_geometry (struct wl_resource *resource)
{
  wl_output_send_geometry (resource, 0, 0, 1200, 1200,
                           WL_OUTPUT_SUBPIXEL_UNKNOWN, "wxrd", "wxrd",
                           WL_OUTPUT_TRANSFORM_NORMAL);
}

static void
send_all_modes (struct wl_resource *resource)
{
  wl_output_send_mode (resource, WL_OUTPUT_MODE_CURRENT, 1920, 1080, 144000);
}

static void
send_scale (struct wl_resource *resource)
{
  uint32_t version = wl_resource_get_version (resource);
  if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
    wl_output_send_scale (resource, 1);
  }
}

static void
send_done (struct wl_resource *resource)
{
  uint32_t version = wl_resource_get_version (resource);
  if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
    wl_output_send_done (resource);
  }
}

static void
output_handle_resource_destroy (struct wl_resource *resource)
{
  // This space deliberately left blank
}

static void
output_handle_release (struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy (resource);
}

static const struct wl_output_interface output_impl = {
  .release = output_handle_release,
};

static void
output_bind (struct wl_client *wl_client,
             void *data,
             uint32_t version,
             uint32_t id)
{
  struct wlr_output *output = data;

  struct wl_resource *resource
      = wl_resource_create (wl_client, &wl_output_interface, version, id);
  if (resource == NULL) {
    wl_client_post_no_memory (wl_client);
    return;
  }
  wl_resource_set_implementation (resource, &output_impl, output,
                                  output_handle_resource_destroy);

  send_geometry (resource);
  send_all_modes (resource);
  send_scale (resource);
  send_done (resource);
}

static void
send_frame_done_iterator (struct wlr_surface *surface,
                          int sx,
                          int sy,
                          void *data)
{
  struct timespec *t = data;
  wlr_surface_send_frame_done (surface, t);
  // wlr_log(WLR_ERROR, "send frame done");
}

static int64_t
timespec_to_msec (const struct timespec *a)
{
  return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

static double
timespec_to_msec_f (const struct timespec *a)
{
  return (double)a->tv_sec * 1000. + (double)a->tv_nsec / 1000000.;
}

int64_t
get_now ()
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return timespec_to_msec (&now);
}

double
get_now_f ()
{
  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);
  return timespec_to_msec_f (&now);
}



static void
wxrd_submit_view_textures (struct wxrd_server *server,
                           struct wxrd_xr_view *view)
{
  if (!server->rendering) {
    wlr_log (WLR_DEBUG, "skip rendering views...");
    return;
  }

  struct timespec now;
  clock_gettime (CLOCK_MONOTONIC, &now);

  struct wxrd_view *wxrd_view;
  wl_list_for_each_reverse (wxrd_view, &server->views, link)
  {


    if (!wxrd_view->mapped) {
      wlr_log (WLR_ERROR, "skip rendering xrd window, not mapped");
      continue;
    }

    struct wlr_surface *surface = view_get_surface (wxrd_view);

    if (surface == NULL) {
      wlr_log (WLR_DEBUG, "skip rendering window, don't have surface");
      continue;
    }

    if (!wlr_surface_has_buffer (surface)) {
      wlr_log (WLR_DEBUG, "skip rendering window, don't have buffer");
      continue;
    }

    struct wlr_texture *tex = surface->buffer->texture;
    struct wxrd_texture *wxrd_tex = wxrd_get_texture (tex);

    if (wxrd_tex->gk == NULL) {
      wlr_log (WLR_ERROR, "skip rendering xrd window, texture is NULL");
      continue;
    }

    if (xrd_window_get_texture (wxrd_view->window) != wxrd_tex->gk) {
      // TODO is this the right condition?
      bool has_rect = false;
      struct XrdWindowRect rect;


      if (wxrd_view->type == WXRD_VIEW_XDG_SHELL) {
        struct wxrd_xdg_shell_view *shell_view
            = xdg_shell_view_from_view (wxrd_view);
        has_rect
            = shell_view->xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL;

#if 0
        struct wlr_fbox src_box;
        wlr_surface_get_buffer_source_box(surface, &src_box);

        wlr_log (WLR_DEBUG, "source box %f,%f %fx%f", src_box.x, src_box.y, src_box.width, src_box.height);

        struct wlr_fbox buffer_source_box;
        wlr_surface_get_buffer_source_box(surface, &buffer_source_box);
        wlr_log (WLR_DEBUG, "buffer source box %f,%f %fx%f", buffer_source_box.x, buffer_source_box.y, buffer_source_box.width, buffer_source_box.height);
        wlr_log (WLR_DEBUG, "buffer position %d,%d", surface->sx, surface->sy);

        struct wlr_subsurface *subsurface;
        wl_list_for_each (subsurface, &surface->subsurfaces_below, parent_link)
        {
          wlr_log (WLR_DEBUG, "subsurface %dx%d below at %d,%d", subsurface->surface->current.width, subsurface->surface->current.height, subsurface->current.x, subsurface->current.y);
        }
        wl_list_for_each (subsurface, &surface->subsurfaces_above, parent_link)
        {
          wlr_log (WLR_DEBUG, "subsurface %dx%d above at %d,%d", subsurface->surface->current.width, subsurface->surface->current.height, subsurface->current.x, subsurface->current.y);
        }
#endif

        struct wlr_box geometry;
        wlr_xdg_surface_get_geometry (shell_view->xdg_surface, &geometry);

        struct wlr_box *wlr_rect = &geometry;

        // HACK (weston-simple-damage)
        if (wlr_rect->width == 0 && wlr_rect->height == 0) {
          wlr_log (WLR_ERROR,
                   "geometry wlr_rect is all zero, not using geometry");
          has_rect = false;
        }

        rect.bl.x = wlr_rect->x;
        rect.bl.y = wlr_rect->y;
        rect.tr.x = wlr_rect->x + wlr_rect->width;
        rect.tr.y = wlr_rect->y + wlr_rect->height;

        // if the client set geometry, it is probably on this surface.
        // if the client did not set geometry, it defaults to a bounding box
        // around all subsurfaces. either way, if the geometry is bigger than
        // the texture, we don't use it.
        // TODO: more advanced subsurface handling?
        if (geometry.x < 0 || geometry.y < 0
            || geometry.x + geometry.width
                   > (int)surface->buffer->texture->width
            || geometry.y + geometry.height
                   > (int)surface->buffer->texture->height) {
          has_rect = false;
          wlr_log (
              WLR_ERROR,
              "geometry wlr_rect is bigger than texture, not using geometry");
        }

        wlr_log (WLR_DEBUG,
                 "submit %dx%d tex %p gk %p buf %p [%zu] %s using %dx%d rect "
                 "at %d,%d: %dx%d->%dx%d",
                 tex->width, tex->height, (void *)wxrd_tex,
                 (void *)wxrd_tex->gk, wxrd_tex->buffer,
                 wxrd_tex->buffer ? wxrd_tex->buffer->n_locks : 0,
                 has_rect ? "" : "NOT", wlr_rect->width, wlr_rect->height,
                 wlr_rect->x, wlr_rect->y, rect.bl.x, rect.bl.y, rect.tr.x,
                 rect.tr.y);
      }

      // HACK on top of a HACK: ref the texture because on circular submits it
      // gets unrefed
      xrd_window_set_and_submit_texture_with_rect (wxrd_view->window,
                                                   g_object_ref (wxrd_tex->gk),
                                                   has_rect ? &rect : NULL);
    }

    wxrd_view_for_each_surface (wxrd_view, send_frame_done_iterator, &now);
  }


  static double last_f = 0;
  double now_f = get_now_f ();
  // wlr_log (WLR_DEBUG, "frametime %f", now_f - last_f);
  last_f = now_f;
}

static void
output_handle_frame (struct wl_listener *listener, void *data)
{
  struct wxrd_output *output = wl_container_of (listener, output, frame);
  struct wxrd_server *server = output->server;
  struct wlr_renderer *renderer = wlr_backend_get_renderer (server->backend);

  if (!wlr_output_attach_render (output->output, NULL)) {
    return;
  }

  // our implementations are empty for now but call begin/end anyway
  wlr_renderer_begin (renderer, 0, 0);

  wxrd_submit_view_textures (server, &server->xr_backend->views[0]);

  wlr_renderer_end (renderer);
  wlr_output_commit (output->output);
}

static void
output_handle_destroy (struct wl_listener *listener, void *data)
{
  struct wxrd_output *output = wl_container_of (listener, output, destroy);
  wl_list_remove (&output->frame.link);
  wl_list_remove (&output->destroy.link);
  free (output);
}

static void
handle_new_output (struct wl_listener *listener, void *data)
{

  struct wxrd_server *server = wl_container_of (listener, server, new_output);
  struct wlr_output *wlr_output = data;

  struct wxrd_output *output = calloc (1, sizeof (*output));
  output->output = wlr_output;
  output->server = server;

  output->frame.notify = output_handle_frame;
  wl_signal_add (&wlr_output->events.frame, &output->frame);
  output->destroy.notify = output_handle_destroy;
  wl_signal_add (&wlr_output->events.destroy, &output->destroy);

  if (wlr_output_is_wl (wlr_output)
      && server->remote_pointer_constraints != NULL) {
    wlr_log (WLR_ERROR, "unimplemented: pointer constraints");
    // struct wl_surface *surface =
    // wlr_wl_output_get_surface(wlr_output);
    // zwp_pointer_constraints_v1_lock_pointer(server->remote_pointer_constraints,
    // surface, server->remote_pointer, 			NULL,
    // ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
  }

  wlr_log (WLR_INFO, "New Output with refresh %d", output->output->refresh);
  wlr_output_set_custom_mode (output->output, 1000, 1000, 144000);
  // output->output->refresh = 144000;
}

static void
remote_handle_global (void *data,
                      struct wl_registry *registry,
                      uint32_t name,
                      const char *interface,
                      uint32_t version)
{
  struct wxrd_server *server = data;

  // TODO: multi-seat support
  if (strcmp (interface, wl_seat_interface.name) == 0
      && server->remote_seat == NULL) {
    server->remote_seat
        = wl_registry_bind (registry, name, &wl_seat_interface, 1);
    server->remote_pointer = wl_seat_get_pointer (server->remote_seat);
  }

  // XXX TODO
  // 	else if (strcmp(interface,
  // zwp_pointer_constraints_v1_interface.name) == 0) {
  // 		server->remote_pointer_constraints =
  // wl_registry_bind(registry, name,
  // 			&zwp_pointer_constraints_v1_interface, 1);
  // 	}
}

static void
remote_handle_global_remove (void *data,
                             struct wl_registry *registry,
                             uint32_t name)
{
  // This space is intentionally left blank
}

static const struct wl_registry_listener registry_listener = {
  .global = remote_handle_global,
  .global_remove = remote_handle_global_remove,
};

static void
backend_iterator (struct wlr_backend *backend, void *data)
{
  struct wxrd_server *server = data;

  if (!wlr_backend_is_wl (backend)) {
    return;
  }

  struct wl_display *remote_display
      = wlr_wl_backend_get_remote_display (backend);
  struct wl_registry *registry = wl_display_get_registry (remote_display);
  wl_registry_add_listener (registry, &registry_listener, server);

  wl_display_roundtrip (remote_display);
}

void
MessageCallback (GLenum source,
                 GLenum type,
                 GLuint id,
                 GLenum severity,
                 GLsizei length,
                 const GLchar *message,
                 const void *userParam)
{
  wlr_log (WLR_DEBUG,
           "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           (type == GL_DEBUG_TYPE_ERROR_KHR ? "** GL ERROR **" : ""), type,
           severity, message);
}

static void
_click_cb (XrdShell *xrd_shell,
           XrdClickEvent *event,
           struct wxrd_server *server)
{
  (void)xrd_shell;

  uint32_t wlr_button = 0;
  switch (event->button) {
  case LEFT_BUTTON: wlr_button = BTN_LEFT; break;
  case RIGHT_BUTTON: wlr_button = BTN_RIGHT; break;
  case MIDDLE_BUTTON: wlr_button = BTN_MIDDLE; break;
  default: wlr_log (WLR_DEBUG, "Unhandled button %d", event->button); return;
  }

  wlr_log (WLR_DEBUG, "button %d: %d", wlr_button, event->state);
  wlr_seat_pointer_notify_button (server->seat, get_now (), wlr_button,
                                  event->state);
}

static void
_move_cursor_cb (XrdShell *xrd_shell,
                 XrdMoveCursorEvent *event,
                 struct wxrd_server *server)
{
  (void)event;

  struct wxrd_view *xrd_focus = NULL;

  // if hovering a window, then focus that window, but only if it is a
  // top level window
  XrdWindow *focus_win = xrd_shell_get_synth_hovered (xrd_shell);
  if (focus_win) {
    g_object_get (focus_win, "native", &xrd_focus, NULL);

    struct wlr_surface *surface = view_get_surface (xrd_focus);

    bool should_focus = true;

    if (xrd_focus->type == WXRD_VIEW_XDG_SHELL) {
      struct wlr_xdg_surface *xdg_surf
          = wlr_xdg_surface_from_wlr_surface (surface);

      //  e.g. xdg popup windows should not be focused
      should_focus = xdg_surf->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL;
    }

    if (should_focus && wxrd_get_focus (server) != xrd_focus) {
      // we only refocus another window when we focus a new
      // window
      wlr_seat_pointer_notify_clear_focus (server->seat);

      wxrd_set_focus (xrd_focus);
      wlr_log (WLR_DEBUG, "Focus new window");
    }
  }

  if (!xrd_focus || !xrd_focus->mapped) {
    // wlr_log(WLR_DEBUG, "No focus");
    return;
  }

  // wlr_log(WLR_DEBUG, "move: %f, %f", event->position->x,
  // event->position->y);

  struct wlr_surface *surface = view_get_surface (xrd_focus);
  if (!surface) {
    wlr_log (WLR_ERROR, "no surface for focused window");
    return;
  }
  wlr_seat_pointer_notify_enter (server->seat, surface, event->position->x,
                                 event->position->y);
  wlr_seat_pointer_notify_motion (server->seat, get_now (), event->position->x,
                                  event->position->y);
  wlr_seat_pointer_notify_frame (server->seat);
}

static void
_keyboard_press_cb (XrdShell *xrd_shell,
                    GdkEventKey *event,
                    struct wxrd_server *server)
{
  (void)xrd_shell;
  wlr_log (WLR_ERROR, "Unimplemented: key (%d): %s\n", event->length,
           event->string);
}

static void
_state_change_cb (XrdShell *xrd_shell,
                  GxrStateChangeEvent *event,
                  struct wxrd_server *server)
{
  switch (event->state_change) {
  case GXR_STATE_SHUTDOWN:
    server->framecycle = FALSE;
    server->rendering = FALSE;
    // TODO shut down
    wlr_log (WLR_DEBUG, "Shutting down...");
    break;
  case GXR_STATE_FRAMECYCLE_START: server->framecycle = TRUE; break;
  case GXR_STATE_FRAMECYCLE_STOP: server->framecycle = FALSE; break;
  case GXR_STATE_RENDERING_START:
    server->rendering = TRUE;
    wlr_log (WLR_DEBUG, "Start rendering...");
    break;
  case GXR_STATE_RENDERING_STOP:
    server->rendering = TRUE;
    wlr_log (WLR_DEBUG, "Stop rendering...");
    break;
  }
}

static void disconnect_cb_sources (struct wxrd_xr_backend *xr_backend)
{
  g_signal_handler_disconnect(xr_backend->xrd_shell, xr_backend->click_source);
  g_signal_handler_disconnect(xr_backend->xrd_shell, xr_backend->move_source);
  g_signal_handler_disconnect(xr_backend->xrd_shell, xr_backend->quit_source);
  xr_backend->click_source = 0;
  xr_backend->move_source = 0;
  xr_backend->quit_source = 0;
}

int
main (int argc, char *argv[])
{
  struct wxrd_server server = { 0 };

  wlr_log_init (WLR_DEBUG, NULL);

  const char *startup_cmd = NULL;
  int opt;
  while ((opt = getopt (argc, argv, "s:h")) != -1) {
    switch (opt) {
    case 's': startup_cmd = optarg; break;
    default:
      fprintf (stderr, "usage: %s [-s startup-cmd]\n", argv[0]);
      return 1;
    }
  }

  server.wl_display = wl_display_create ();
  if (server.wl_display == NULL) {
    wlr_log (WLR_ERROR, "wl_display_create failed");
    return 1;
  }
  struct wl_event_loop *wl_event_loop
      = wl_display_get_event_loop (server.wl_display);

  bool running = true;
  struct wl_event_source *signals[] = {
    wl_event_loop_add_signal (wl_event_loop, SIGTERM, handle_signal, &running),
    wl_event_loop_add_signal (wl_event_loop, SIGINT, handle_signal, &running),
  };
  if (signals[0] == NULL || signals[1] == NULL) {
    wlr_log (WLR_ERROR, "wl_event_loop_add_signal failed");
    return 1;
  }

  server.backend = wlr_backend_autocreate (server.wl_display);
  if (server.backend == NULL) {
    wlr_log (WLR_ERROR, "Failed to create native backend");
    return 1;
  }
  wlr_multi_for_each_backend (server.backend, backend_iterator, &server);

  server.new_output.notify = handle_new_output;
  wl_signal_add (&server.backend->events.new_output, &server.new_output);

  // struct wlr_renderer *renderer = wlr_backend_get_renderer (server.backend);
  struct wlr_renderer *backend_renderer
      = wlr_backend_get_renderer (server.backend);
  struct wlr_renderer *renderer = wxrd_renderer_create (backend_renderer);

  server.xr_backend = wxrd_xr_backend_create (server.wl_display, renderer);
  if (server.xr_backend == NULL || server.backend == NULL) {
    wlr_log (WLR_ERROR, "backend creation failed");
    return 1;
  }
  struct wxrd_xr_backend *xr_backend = server.xr_backend;
  wlr_multi_backend_add (server.backend, &xr_backend->base);


  xr_backend->click_source = g_signal_connect (
      xr_backend->xrd_shell, "click-event", (GCallback)_click_cb, &server);
  xr_backend->move_source
      = g_signal_connect (xr_backend->xrd_shell, "move-cursor-event",
                          (GCallback)_move_cursor_cb, &server);
  /* TODO: new keyboard event
  xr_backend->keyboard_source
  = g_signal_connect (xr_backend->xrd_shell, "keyboard-press-event",
                          (GCallback)_keyboard_press_cb, &server);
  */
  xr_backend->quit_source
      = g_signal_connect (xr_backend->xrd_shell, "state-change-event",
                          (GCallback)_state_change_cb, &server);

  wlr_renderer_init_wl_display (renderer, server.wl_display);

  struct wlr_compositor *compositor
      = wlr_compositor_create (server.wl_display, renderer);
  wlr_data_device_manager_create (server.wl_display);
  wlr_data_control_manager_v1_create (server.wl_display);
  wlr_primary_selection_v1_device_manager_create (server.wl_display);

  wxrd_input_init (&server);

  wl_list_init (&server.views);
  wxrd_xdg_shell_init (&server);

  const char *wl_socket = wl_display_add_socket_auto (server.wl_display);
  if (wl_socket == NULL) {
    wlr_log (WLR_ERROR, "wl_display_add_socket_auto failed");
    return 1;
  }
  wlr_log (WLR_INFO, "Wayland compositor listening on WAYLAND_DISPLAY=%s",
           wl_socket);

  if (!wlr_backend_start (server.backend)) {
    wlr_log (WLR_ERROR, "wlr_backend_start failed");
    return 1;
  }

  wl_global_create (server.wl_display, &wl_output_interface, 3, NULL,
                    output_bind);

  wlr_log (WLR_DEBUG, "initializing xwayland");
  server.xwayland.wlr_xwayland
      = wlr_xwayland_create (server.wl_display, compositor, true);
  if (server.xwayland.wlr_xwayland) {
    wl_signal_add (&server.xwayland.wlr_xwayland->events.new_surface,
                   &server.xwayland_surface);
    server.xwayland_surface.notify = handle_xwayland_surface;
    wl_signal_add (&server.xwayland.wlr_xwayland->events.ready,
                   &server.xwayland_ready);
    server.xwayland_ready.notify = handle_xwayland_ready;

    setenv ("DISPLAY", server.xwayland.wlr_xwayland->display_name, true);
    wlr_log (WLR_DEBUG, "initialized xwayland on %s",
             server.xwayland.wlr_xwayland->display_name);
  } else {
    wlr_log (WLR_ERROR, "Failed to start Xwayland");
    unsetenv ("DISPLAY");
    wlr_log (WLR_DEBUG, "Failed to initialize xwayland");
  }

  setenv ("WAYLAND_DISPLAY", wl_socket, true);
  if (startup_cmd != NULL) {
    pid_t pid = fork ();
    if (pid < 0) {
      wlr_log_errno (WLR_ERROR, "fork failed");
      return 1;
    } else if (pid == 0) {
      execl ("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
      wlr_log_errno (WLR_ERROR, "execl failed");
      exit (1);
    }
  }

  wlr_log (WLR_DEBUG, "Starting XR main loop");
  while (running) {
    wl_display_flush_clients (server.wl_display);
    int ret = wl_event_loop_dispatch (wl_event_loop, 1);
    if (ret < 0) {
      wlr_log (WLR_ERROR, "wl_event_loop_dispatch failed");
      return 1;
    }

    while (g_main_context_pending (NULL)) {
      g_main_context_iteration (NULL, FALSE);
    }

    // TODO Combine (not overwrite) mouse input with XR input and
    // move XrdDesktopCursor
    wxrd_update_pointer (&server, 0);

    if (!running) {
      break;
    }
  }

  wlr_log (WLR_DEBUG, "Tearing down XR instance");

  GSList *windows = xrd_shell_get_windows (server.xr_backend->xrd_shell);
  for (GSList *l = windows; l; l = l->next) {
    XrdWindow *xrdWin = XRD_WINDOW (l->data);
    xrd_window_close (xrdWin);
    /* shell unref will do it anyway
     *   xrd_shell_remove_window(xrdShell, xrdWin); */
  }
  disconnect_cb_sources (server.xr_backend);
  g_object_unref (server.xr_backend->xrd_shell);

  wl_event_source_remove (signals[0]);
  wl_event_source_remove (signals[1]);
  wl_display_destroy_clients (server.wl_display);

  // wl_display_destroy (server.wl_display);

  return 0;
}
