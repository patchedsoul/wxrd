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

#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_input_device.h>

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


  wlr_keyboard_set_keymap (device->keyboard, server->default_keymap);
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

static void
keyboard_destroy (struct wlr_keyboard *kyeboard)
{}

static const struct wlr_keyboard_impl keyboard_impl = {
  .destroy = keyboard_destroy,
};

static void
keyboard_device_destroy (struct wlr_input_device *dev)
{}

static const struct wlr_input_device_impl keyboard_device_impl = {
  .destroy = keyboard_device_destroy,
};

// utf8 and keymap code borrowed from gamescope ime.cpp

static const uint32_t UTF8_INVALID = 0xFFFD;
static size_t
utf8_size (const char *str)
{
  uint8_t u8 = (uint8_t)str[0];
  if ((u8 & 0x80) == 0) {
    return 1;
  } else if ((u8 & 0xE0) == 0xC0) {
    return 2;
  } else if ((u8 & 0xF0) == 0xE0) {
    return 3;
  } else if ((u8 & 0xF8) == 0xF0) {
    return 4;
  } else {
    return 0;
  }
}

static uint32_t
utf8_decode (const char **str_ptr)
{
  const char *str = *str_ptr;
  size_t size = utf8_size (str);
  if (size == 0) {
    *str_ptr = &str[1];
    return UTF8_INVALID;
  }

  *str_ptr = &str[size];

  const uint32_t masks[] = { 0x7F, 0x1F, 0x0F, 0x07 };
  uint32_t ret = (uint32_t)str[0] & masks[size - 1];
  for (size_t i = 1; i < size; i++) {
    ret <<= 6;
    ret |= str[i] & 0x3F;
  }
  return ret;
}

/* Some clients assume keycodes are coming from evdev and interpret them. Only
 * use keys that would normally produce characters for our emulated events. */
static const uint32_t allow_keycodes[] = {
  KEY_1,     KEY_2,         KEY_3,         KEY_4,          KEY_5,
  KEY_6,     KEY_7,         KEY_8,         KEY_9,          KEY_0,
  KEY_MINUS, KEY_EQUAL,     KEY_Q,         KEY_W,          KEY_E,
  KEY_R,     KEY_T,         KEY_Y,         KEY_U,          KEY_I,
  KEY_O,     KEY_P,         KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_A,
  KEY_S,     KEY_D,         KEY_F,         KEY_G,          KEY_H,
  KEY_J,     KEY_K,         KEY_L,         KEY_SEMICOLON,  KEY_APOSTROPHE,
  KEY_GRAVE, KEY_BACKSLASH, KEY_Z,         KEY_X,          KEY_C,
  KEY_V,     KEY_B,         KEY_N,         KEY_M,          KEY_COMMA,
  KEY_DOT,   KEY_SLASH,
};

static const size_t allow_keycodes_len
    = sizeof (allow_keycodes) / sizeof (allow_keycodes[0]);

struct wlserver_input_method_key
{
  uint32_t ch;
  uint32_t keycode;
  xkb_keysym_t keysym;
};

static uint32_t
keycode_from_ch (struct wxrd_server *server,
                 uint32_t ch,
                 struct wlserver_input_method_key *keys,
                 uint32_t *keys_count)
{
  for (uint32_t i = 0; i < *keys_count; i++) {
    if (keys[i].ch == ch) {
      return keys[i].keycode;
    }
  }

  xkb_keysym_t keysym = xkb_utf32_to_keysym (ch);
  if (keysym == XKB_KEY_NoSymbol) {
    return XKB_KEYCODE_INVALID;
  }

  if (*keys_count >= allow_keycodes_len) {
    // TODO: maybe use keycodes above KEY_MAX?
    wlr_log (WLR_ERROR, "Key codes exhausted!");
    return XKB_KEYCODE_INVALID;
  }

  uint32_t next_index = *keys_count;

  uint32_t keycode = allow_keycodes[next_index];
  keys[next_index] = (struct wlserver_input_method_key){ .ch = ch,
                                                         .keycode = keycode,
                                                         .keysym = keysym };

  *keys_count = *keys_count + 1;

  return keycode;
}

static struct xkb_keymap *
generate_keymap (struct wxrd_server *server,
                 struct wlserver_input_method_key *keys,
                 uint32_t keys_count)
{
  uint32_t keycode_offset = 8;

  char *str = NULL;
  size_t str_size = 0;
  FILE *f = open_memstream (&str, &str_size);

  uint32_t min_keycode = allow_keycodes[0];
  uint32_t max_keycode = allow_keycodes[keys_count];
  fprintf (f,
           "xkb_keymap {\n"
           "\n"
           "xkb_keycodes \"(unnamed)\" {\n"
           "	minimum = %u;\n"
           "	maximum = %u;\n",
           keycode_offset + min_keycode, keycode_offset + max_keycode);

  for (uint32_t i = 0; i < keys_count; i++) {
    uint32_t keycode = keys[i].keycode;
    fprintf (f, "	<K%u> = %u;\n", keycode, keycode + keycode_offset);
  }

  // TODO: should we really be including "complete" here? squeekboard seems
  // to get away with some other workarounds:
  // https://gitlab.gnome.org/World/Phosh/squeekboard/-/blob/fc411d680b0138042b95b8a630401607726113d4/src/keyboard.rs#L180
  fprintf (f,
           "};\n"
           "\n"
           "xkb_types \"(unnamed)\" { include \"complete\" };\n"
           "\n"
           "xkb_compatibility \"(unnamed)\" { include \"complete\" };\n"
           "\n"
           "xkb_symbols \"(unnamed)\" {\n");

  for (uint32_t i = 0; i < keys_count; i++) {
    uint32_t keycode = keys[i].keycode;
    xkb_keysym_t keysym = keys[i].keysym;

    char keysym_name[256];
    int ret = xkb_keysym_get_name (keysym, keysym_name, sizeof (keysym_name));
    if (ret <= 0) {
      wlr_log (WLR_ERROR, "xkb_keysym_get_name failed for keysym %u", keysym);
      return NULL;
    }

    fprintf (f, "	key <K%u> {[ %s ]};\n", keycode, keysym_name);
  }

  fprintf (f,
           "};\n"
           "\n"
           "};\n");

  fclose (f);

  struct xkb_context *context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  struct xkb_keymap *keymap = xkb_keymap_new_from_buffer (
      context, str, str_size, XKB_KEYMAP_FORMAT_TEXT_V1,
      XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb_context_unref (context);

  free (str);

  return keymap;
}

void
type_text (struct wxrd_server *server, const char *text)
{
  if (text == NULL) {
    return;
  }

  xkb_keycode_t keycodes[allow_keycodes_len];
  uint32_t keycode_count = 0;

  struct wlserver_input_method_key keys[allow_keycodes_len];
  uint32_t keys_count = 0;

  while (text[0] != '\0') {
    uint32_t ch = utf8_decode (&text);

    xkb_keycode_t keycode = keycode_from_ch (server, ch, keys, &keys_count);
    if (keycode == XKB_KEYCODE_INVALID) {
      wlr_log (WLR_ERROR, "warning: cannot type character U+%X", ch);
      continue;
    }

    wlr_log (WLR_DEBUG, "ch %s (%d) -> keycode %d", (char *)&ch, ch, keycode);

    keycodes[keycode_count++] = keycode;
  }

  struct xkb_keymap *keymap = generate_keymap (server, keys, keys_count);
  if (keymap == NULL) {
    wlr_log (WLR_ERROR, "failed to generate keymap");
    return;
  }

  struct wlr_seat *seat = server->seat;


  struct wlr_keyboard *keyboard = &server->vr_keyboard;
  struct wlr_input_device *keyboard_device = &server->vr_keyboard_device;

  wlr_keyboard_set_keymap (keyboard, keymap);
  xkb_keymap_unref (keymap);

  wlr_seat_set_keyboard (seat, keyboard_device);


  struct wlr_surface *surface = view_get_surface (wxrd_get_focus (server));
  wlr_seat_keyboard_notify_enter (seat, surface, keyboard->keycodes,
                                  keyboard->num_keycodes,
                                  &keyboard->modifiers);


  for (size_t i = 0; i < keycode_count; i++) {
    wlr_seat_keyboard_notify_key (seat, get_now (), keycodes[i],
                                  WL_KEYBOARD_KEY_STATE_PRESSED);
    wlr_seat_keyboard_notify_key (seat, get_now () + 1, keycodes[i],
                                  WL_KEYBOARD_KEY_STATE_RELEASED);

    wlr_log (WLR_DEBUG, "keycode input: %d", keycodes[i]);
  }
}

void
wxrd_input_init (struct wxrd_server *server)
{
  wl_list_init (&server->keyboards);
  wl_list_init (&server->pointers);

  server->seat = wlr_seat_create (server->wl_display, "seat0");
  wlr_seat_set_capabilities (server->seat, WL_SEAT_CAPABILITY_POINTER
                                               | WL_SEAT_CAPABILITY_KEYBOARD
                                               | WL_SEAT_CAPABILITY_TOUCH);

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

  /* TODO: Source keymap et al from parent Wayland compositor if possible
   */
  struct xkb_rule_names rules = { 0 };
  server->xkb_context = xkb_context_new (XKB_CONTEXT_NO_FLAGS);
  server->default_keymap = xkb_map_new_from_names (
      server->xkb_context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

  wlr_keyboard_init (&server->vr_keyboard, &keyboard_impl);
  wlr_input_device_init (&server->vr_keyboard_device,
                         WLR_INPUT_DEVICE_KEYBOARD, &keyboard_device_impl,
                         "xrdesktop_vr_keyboard", 0, 0);
  server->vr_keyboard_device.keyboard = &server->vr_keyboard;

  wlr_keyboard_set_repeat_info (&server->vr_keyboard, 0, 0);
  // TODO
  // xkb_keymap_unref (server->default_keymap);
  // xkb_context_unref (server->xkb_context);
}
