/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "input.h"
#include "server.h"
#include "view.h"
#include <assert.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "backend.h"
#include "wxrd-renderer.h"
#include <drm_fourcc.h>

static void
keyboard_handle_modifiers (struct wl_listener *listener, void *data)
{
  struct wxrd_keyboard *keyboard
      = wl_container_of (listener, keyboard, modifiers);
  wlr_seat_set_keyboard (keyboard->server->seat, keyboard->device);
  wlr_seat_keyboard_notify_modifiers (keyboard->server->seat,
                                      &keyboard->device->keyboard->modifiers);
}

static void
spawn_terminal (void)
{
  pid_t pid = fork ();
  if (pid < 0) {
    wlr_log_errno (WLR_ERROR, "fork failed");
  } else if (pid == 0) {
    const char *term = getenv ("TERMINAL");
    if (!term) {
      term = "weston-terminal";
    }
    execl ("/bin/sh", "/bin/sh", "-c", term, (void *)NULL);
    wlr_log_errno (WLR_ERROR, "execl failed");
    exit (1);
  }
}

static bool
handle_keybinding (struct wxrd_server *server, xkb_keysym_t sym)
{
  switch (sym) {
  case XKB_KEY_Escape: wl_display_terminate (server->wl_display); break;
  case XKB_KEY_Right:
    if (wl_list_length (&server->views) < 2) {
      struct wxrd_view *current_view
          = wl_container_of (server->views.next, current_view, link);
      wxrd_set_focus (current_view);
      wlr_log (WLR_DEBUG, "focused current view %s", current_view->title);
      break;
    }
    wxrd_focus_next_view (server);
    break;
  case XKB_KEY_Return: spawn_terminal (); break;
  case XKB_KEY_q:;
    struct wxrd_view *view = wxrd_get_focus (server);
    if (view != NULL) {
      wxrd_view_close (view);
    }
    break;
  default: return false;
  }
  return true;
}

static bool
keyboard_meta_pressed (struct wxrd_keyboard *keyboard)
{
  uint32_t modifiers = wlr_keyboard_get_modifiers (keyboard->device->keyboard);
  return modifiers & WLR_MODIFIER_ALT;
}

static void
keyboard_handle_key (struct wl_listener *listener, void *data)
{
  struct wxrd_keyboard *keyboard = wl_container_of (listener, keyboard, key);
  struct wxrd_server *server = keyboard->server;
  struct wlr_event_keyboard_key *event = data;
  struct wlr_seat *seat = server->seat;

  uint32_t keycode = event->keycode + 8;
  const xkb_keysym_t *syms;
  int nsyms = xkb_state_key_get_syms (keyboard->device->keyboard->xkb_state,
                                      keycode, &syms);

  wlr_log (WLR_DEBUG, "key %d (meta %d state %d)", keycode,
           keyboard_meta_pressed (keyboard), event->state);

  /* TODO: In the future we'll likely want a more sophisticated approach
   */
  bool handled = false;
  if (keyboard_meta_pressed (keyboard)
      && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    for (int i = 0; i < nsyms; i++) {
      handled = handle_keybinding (server, syms[i]);
    }
  }

  if (!handled) {
    wlr_seat_set_keyboard (seat, keyboard->device);
    wlr_seat_keyboard_notify_key (seat, event->time_msec, event->keycode,
                                  event->state);
    wlr_log (WLR_DEBUG, "keyboard input: %d: %d", event->keycode,
             event->state);
  }
}

void
handle_new_keyboard (struct wxrd_server *server,
                     struct wlr_input_device *device)
{
  struct wxrd_keyboard *keyboard = calloc (1, sizeof (struct wxrd_keyboard));
  keyboard->server = server;
  keyboard->device = device;

  /* TODO: Source keymap et al from parent Wayland compositor if possible
   */
  struct xkb_rule_names rules = { 0 };
  struct xkb_context *context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  struct xkb_keymap *keymap
      = xkb_map_new_from_names (context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

  wlr_keyboard_set_keymap (device->keyboard, keymap);
  xkb_keymap_unref (keymap);
  xkb_context_unref (context);
  wlr_keyboard_set_repeat_info (device->keyboard, 25, 600);

  keyboard->modifiers.notify = keyboard_handle_modifiers;
  wl_signal_add (&device->keyboard->events.modifiers, &keyboard->modifiers);
  keyboard->key.notify = keyboard_handle_key;
  wl_signal_add (&device->keyboard->events.key, &keyboard->key);

  wlr_seat_set_keyboard (server->seat, device);

  wl_list_insert (&server->keyboards, &keyboard->link);
}

static void
update_pointer_default (struct wxrd_server *server, uint32_t time)
{
  float sx, sy;
  struct wlr_surface *surface;

  // cached last focused window
  struct wxrd_view *current_focus = wxrd_get_focus (server);
  if (!current_focus || !current_focus->mapped) {
    // wlr_log(WLR_DEBUG, "No focus");
    return;
  }

  surface = view_get_surface (current_focus);

  // TODO proper mapping with aspect ratio etc
  // map the [0,1] mouse coordinates on the wlroots window to the current
  // focused window
  sx = server->xr_backend->pointer_absolute.x
       * surface->buffer->texture->width;
  sy = server->xr_backend->pointer_absolute.y
       * surface->buffer->texture->height;

  // wlr_log(WLR_DEBUG, "update pointer default %f %f", sx, sy);

  if (current_focus != NULL) {
    wlr_seat_pointer_notify_enter (server->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion (server->seat, time, sx, sy);
    wlr_seat_pointer_notify_frame (server->seat);
  } else {
    wlr_seat_pointer_clear_focus (server->seat);
  }
}

static void
update_pointer_resize (struct wxrd_server *server)
{

  struct wxrd_view *view = wxrd_get_focus (server);
  if (view == NULL) {
    wlr_log (WLR_ERROR, "No focused window to resize");
    return;
  }

  float diff_x = (server->xr_backend->pointer_absolute.x
                  - server->seatop_resize.start_absolute_x);
  float diff_y = (server->xr_backend->pointer_absolute.y
                  - server->seatop_resize.start_absolute_y);

  static float FACTOR = 300;
  diff_x *= FACTOR;
  diff_y *= FACTOR;

  wxrd_view_set_size (view, server->seatop_resize.start_w + diff_x,
                      server->seatop_resize.start_h + diff_y);
  wlr_log (WLR_DEBUG, "Set size %d+%f,%d+%f", server->seatop_resize.start_w,
           diff_x, server->seatop_resize.start_h, diff_y);
}

void
wxrd_update_pointer (struct wxrd_server *server, uint32_t time)
{
  switch (server->seatop) {
  case WXRC_SEATOP_DEFAULT:
    // TODO combine with xr controller input
    // update_pointer_default (server, time);
    return;
  case WXRC_SEATOP_MOVE: return; // meaningless in XR
  case WXRC_SEATOP_RESIZE: update_pointer_resize (server); return;
  }
  abort ();
}

static void
pointer_handle_motion (struct wl_listener *listener, void *data)
{
  struct wxrd_pointer *pointer = wl_container_of (listener, pointer, motion);
  struct wlr_event_pointer_motion *event = data;
  wlr_log (WLR_ERROR, "unimplemented: pointer_handle_motion %f,%f",
           event->delta_x, event->delta_y);
}

static void
pointer_handle_motion_absolute (struct wl_listener *listener, void *data)
{
  struct wxrd_pointer *pointer
      = wl_container_of (listener, pointer, motion_absolute);
  struct wxrd_server *server = pointer->server;

  struct wlr_event_pointer_motion_absolute *event = data;

  // wlr_log(WLR_DEBUG, "absolute motion %f, %f", event->x, event->y);

  // set the absolute coordinates [0,1] of the mouse on the wlroots
  // window
  server->xr_backend->pointer_absolute.x = event->x;
  server->xr_backend->pointer_absolute.y = event->y;
}

static void
pointer_handle_button (struct wl_listener *listener, void *data)
{
  struct wxrd_pointer *pointer = wl_container_of (listener, pointer, button);
  struct wxrd_server *server = pointer->server;
  struct wlr_event_pointer_button *event = data;

  switch (event->state) {
  case WLR_BUTTON_PRESSED:;
    bool meta_pressed = false;
    struct wxrd_keyboard *keyboard;
    wl_list_for_each (keyboard, &server->keyboards, link)
    {
      if (keyboard_meta_pressed (keyboard)) {
        meta_pressed = true;
        break;
      }
    }

    if (meta_pressed && event->button == BTN_LEFT) {
      // TODO handle physical mouse window move
    } else if (meta_pressed && event->button == BTN_RIGHT) {

      struct wxrd_view *view = wxrd_get_focus (server);
      wxrd_view_get_size (view, &server->seatop_resize.start_w,
                          &server->seatop_resize.start_h);

      server->seatop_resize.start_absolute_x
          = server->xr_backend->pointer_absolute.x;
      server->seatop_resize.start_absolute_y
          = server->xr_backend->pointer_absolute.y;

      if (server->seatop_resize.start_w != 0) {
        server->seatop = WXRC_SEATOP_RESIZE;
        wlr_log (WLR_DEBUG, "start resize seatop with size %dx%d",
                 server->seatop_resize.start_w, server->seatop_resize.start_h);
      }
      return;
    }
    break;
  case WLR_BUTTON_RELEASED:
    if (server->seatop != WXRC_SEATOP_DEFAULT) {
      server->seatop = WXRC_SEATOP_DEFAULT;
      wlr_log (WLR_DEBUG, "default seatop");
      return;
    }
    break;
  }

  wlr_log (WLR_DEBUG, "button %d: %d", event->button, event->state);
  wlr_seat_pointer_notify_button (server->seat, event->time_msec,
                                  event->button, event->state);
}

static void
pointer_handle_axis (struct wl_listener *listener, void *data)
{
  struct wxrd_pointer *pointer = wl_container_of (listener, pointer, axis);
  struct wlr_event_pointer_axis *event = data;
  struct wxrd_server *server = pointer->server;

  bool meta_pressed = false;
  struct wxrd_keyboard *keyboard;
  wl_list_for_each (keyboard, &server->keyboards, link)
  {
    if (keyboard_meta_pressed (keyboard)) {
      meta_pressed = true;
      break;
    }
  }
  if (meta_pressed) {
    /* Move window towards/away from the camera */
    struct wxrd_view *view = wxrd_get_focus (server);
    if (view == NULL) {
      return;
    }

    wlr_log (WLR_ERROR, "unimplemented: xr window movement");

    return;
  }

  wlr_seat_pointer_notify_axis (pointer->server->seat, event->time_msec,
                                event->orientation, event->delta,
                                event->delta_discrete, event->source);
}

static void
pointer_handle_frame (struct wl_listener *listener, void *data)
{
  struct wxrd_pointer *pointer = wl_container_of (listener, pointer, frame);
  wlr_seat_pointer_notify_frame (pointer->server->seat);
}

static void
handle_new_pointer (struct wxrd_server *server,
                    struct wlr_input_device *device)
{
  struct wxrd_pointer *pointer = calloc (1, sizeof (*pointer));
  pointer->server = server;
  pointer->device = device;

  wl_list_insert (&server->pointers, &pointer->link);

  pointer->motion.notify = pointer_handle_motion;
  wl_signal_add (&device->pointer->events.motion, &pointer->motion);

  pointer->motion_absolute.notify = pointer_handle_motion_absolute;
  wl_signal_add (&device->pointer->events.motion_absolute,
                 &pointer->motion_absolute);

  pointer->button.notify = pointer_handle_button;
  wl_signal_add (&device->pointer->events.button, &pointer->button);

  pointer->axis.notify = pointer_handle_axis;
  wl_signal_add (&device->pointer->events.axis, &pointer->axis);

  pointer->frame.notify = pointer_handle_frame;
  wl_signal_add (&device->pointer->events.frame, &pointer->frame);
}

static void
handle_new_input (struct wl_listener *listener, void *data)
{
  struct wxrd_server *server = wl_container_of (listener, server, new_input);
  struct wlr_input_device *device = data;
  wlr_log (WLR_DEBUG, "New input device '%s'", device->name);
  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD: handle_new_keyboard (server, device); break;
  case WLR_INPUT_DEVICE_POINTER: handle_new_pointer (server, device); break;
  default: break;
  }

  uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
  if (!wl_list_empty (&server->keyboards)) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  wlr_seat_set_capabilities (server->seat, caps);
}

static void
cursor_reset (struct wxrd_cursor *cursor)
{
  wlr_texture_destroy (cursor->xcursor_texture);
  cursor->xcursor_texture = NULL;
  cursor->xcursor_image = NULL;

  wl_list_remove (&cursor->surface_destroy.link);
  wl_list_init (&cursor->surface_destroy.link);
  cursor->surface = NULL;
}

void
wxrd_cursor_set_xcursor (struct wxrd_cursor *cursor,
                         struct wlr_xcursor *xcursor)
{
  cursor_reset (cursor);

  cursor->xcursor_image = xcursor->images[0];

  cursor->hotspot_x = cursor->xcursor_image->hotspot_x;
  cursor->hotspot_y = cursor->xcursor_image->hotspot_y;

  struct wlr_renderer *renderer = cursor->server->xr_backend->renderer;
  // = wlr_backend_get_renderer (cursor->server->backend);

  // TODO: find correct format
  uint32_t drm_format = DRM_FORMAT_ABGR8888;

  wlr_log (WLR_DEBUG, "new xcursor %dx%d, hotspot %dx%d",
           cursor->xcursor_image->width, cursor->xcursor_image->height,
           cursor->hotspot_x, cursor->hotspot_y);
  cursor->xcursor_texture = wlr_texture_from_pixels (
      renderer, drm_format, cursor->xcursor_image->width * 4,
      cursor->xcursor_image->width, cursor->xcursor_image->height,
      cursor->xcursor_image->buffer);

  if (cursor->xcursor_texture == NULL) {
    wlr_log (WLR_ERROR, "xcursor texture is NULL");
    return;
  }

  struct wxrd_texture *t = wxrd_get_texture (cursor->xcursor_texture);

  G3kCursor *xrd_cursor
      = xrd_shell_get_desktop_cursor (cursor->server->xr_backend->xrd_shell);

  // HACK This would normally leak the texture freed by
  // g3k_cursor_set_and_submit_texture. wlroots keeps the
  // wlr_texture around and reuses it. Therefore we have to keep it
  // wxrd_texture->gk around too. wlroots will eventually call
  // wxrd_texture_destroy, where we will free it.
  GulkanTexture *curr_tex = g3k_cursor_get_texture (xrd_cursor);
  if (curr_tex) {
    g_object_ref (curr_tex);
  }

  g3k_cursor_set_and_submit_texture (xrd_cursor, t->gk);

  g3k_cursor_set_hotspot (xrd_cursor, cursor->hotspot_x, cursor->hotspot_y);
  // wlr_log(WLR_DEBUG, "Setting cursor hotspot %d,%d", hotspot_x,
  // hotspot_y);

  wlr_log (WLR_DEBUG, "Setting xcursor texture");
}

static void
cursor_handle_surface_destroy (struct wl_listener *listener, void *data)
{
  struct wxrd_cursor *cursor
      = wl_container_of (listener, cursor, surface_destroy);
  cursor_reset (cursor);
}

void
wxrd_cursor_set_surface (struct wxrd_cursor *cursor,
                         struct wlr_surface *surface,
                         int hotspot_x,
                         int hotspot_y)
{
  cursor_reset (cursor);

  cursor->surface = surface;
  cursor->hotspot_x = hotspot_x;
  cursor->hotspot_y = hotspot_y;

  if (cursor->surface) {
    cursor->surface_destroy.notify = cursor_handle_surface_destroy;
    wl_signal_add (&surface->events.destroy, &cursor->surface_destroy);
  }

  struct wlr_texture *tex = wlr_surface_get_texture (surface);
  if (!tex) {
    wlr_log (WLR_DEBUG, "No cursor texture");
    return;
  }

  struct wxrd_texture *t = wxrd_get_texture (tex);

  wlr_log (WLR_DEBUG, "Setting cursor texture with hotspot %d,%d (%p, %p)",
           hotspot_x, hotspot_y, (void *)t, (void *)t->gk);

  G3kCursor *xrd_cursor
      = xrd_shell_get_desktop_cursor (cursor->server->xr_backend->xrd_shell);

  // HACK This would normally leak the texture freed by
  // g3k_cursor_set_and_submit_texture. wlroots keeps the
  // wlr_texture around and reuses it. Therefore we have to keep it
  // wxrd_texture->gk around too. wlroots will eventually call
  // wxrd_texture_destroy, where we will free it.
  GulkanTexture *curr_tex = g3k_cursor_get_texture (xrd_cursor);
  if (curr_tex) {
    g_object_ref (curr_tex);
  }

  g3k_cursor_set_and_submit_texture (xrd_cursor, t->gk);

  g3k_cursor_set_hotspot (xrd_cursor, hotspot_x, hotspot_y);
}

struct wlr_texture *
wxrd_cursor_get_texture (struct wxrd_cursor *cursor,
                         int *hotspot_x,
                         int *hotspot_y,
                         int *scale)
{
  *hotspot_x = *hotspot_y = *scale = 0;

  if (cursor->surface != NULL && cursor->surface->buffer != NULL
      && cursor->surface->buffer->texture != NULL) {
    *hotspot_x = cursor->hotspot_x + cursor->surface->sx;
    *hotspot_y = cursor->hotspot_y + cursor->surface->sy;
    *scale = cursor->surface->current.scale;
    return cursor->surface->buffer->texture;
  }

  if (cursor->xcursor_texture != NULL) {
    *hotspot_x = cursor->xcursor_image->hotspot_x;
    *hotspot_y = cursor->xcursor_image->hotspot_y;
    *scale = 2;
    return cursor->xcursor_texture;
  }

  return NULL;
}

static void
handle_request_set_cursor (struct wl_listener *listener, void *data)
{
  struct wxrd_server *server
      = wl_container_of (listener, server, request_set_cursor);
  struct wlr_seat_pointer_request_set_cursor_event *event = data;

  struct wl_client *focused_client = NULL;
  struct wlr_surface *focused_surface
      = server->seat->pointer_state.focused_surface;
  if (focused_surface != NULL) {
    focused_client = wl_resource_get_client (focused_surface->resource);
  }

  // TODO: check cursor mode
  if (focused_client == NULL || event->seat_client->client != focused_client) {
    wlr_log (WLR_DEBUG, "Denying request to set cursor from unfocused client");
    return;
  }

  if (!event->surface) {
    wlr_log (WLR_ERROR, "Trying to set NULL surface on cursor");
    return;
  }

  wxrd_cursor_set_surface (&server->cursor, event->surface, event->hotspot_x,
                           event->hotspot_y);
}

static void
handle_request_set_selection (struct wl_listener *listener, void *data)
{
  struct wxrd_server *server
      = wl_container_of (listener, server, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection (server->seat, event->source, event->serial);
}

static void
handle_request_set_primary_selection (struct wl_listener *listener, void *data)
{
  struct wxrd_server *server
      = wl_container_of (listener, server, request_set_primary_selection);
  struct wlr_seat_request_set_primary_selection_event *event = data;
  wlr_seat_set_primary_selection (server->seat, event->source, event->serial);
}

void
wxrd_input_init (struct wxrd_server *server)
{
  wl_list_init (&server->keyboards);
  wl_list_init (&server->pointers);

  server->seat = wlr_seat_create (server->wl_display, "seat0");
  server->cursor_mgr = wlr_xcursor_manager_create (NULL, 24);
  wlr_xcursor_manager_load (server->cursor_mgr, 2);

  server->cursor.server = server;
  wl_list_init (&server->cursor.surface_destroy.link);

  server->new_input.notify = handle_new_input;
  wl_signal_add (&server->backend->events.new_input, &server->new_input);

  server->request_set_cursor.notify = handle_request_set_cursor;
  wl_signal_add (&server->seat->events.request_set_cursor,
                 &server->request_set_cursor);
  server->request_set_selection.notify = handle_request_set_selection;
  wl_signal_add (&server->seat->events.request_set_selection,
                 &server->request_set_selection);
  server->request_set_primary_selection.notify
      = handle_request_set_primary_selection;
  wl_signal_add (&server->seat->events.request_set_primary_selection,
                 &server->request_set_primary_selection);

  struct wlr_xcursor *xcursor
      = wlr_xcursor_manager_get_xcursor (server->cursor_mgr, "left_ptr", 2);
  wxrd_cursor_set_xcursor (&server->cursor, xcursor);
}
