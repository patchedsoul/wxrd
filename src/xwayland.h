#ifndef WXRD_XWAYLAND_H
#define WXRD_XWAYLAND_H

#include <wlr/xwayland.h>
#include <xcb/xproto.h>
#include "view.h"

struct wxrd_xwayland_view
{
  struct wxrd_view view;

  struct wxrd_server *server;

  struct wl_listener commit;
  struct wl_listener request_move;
  struct wl_listener request_resize;
  struct wl_listener request_maximize;
  struct wl_listener request_minimize;
  struct wl_listener request_configure;
  struct wl_listener request_fullscreen;
  struct wl_listener request_activate;
  struct wl_listener set_title;
  struct wl_listener set_class;
  struct wl_listener set_role;
  struct wl_listener set_window_type;
  struct wl_listener set_hints;
  struct wl_listener set_decorations;
  struct wl_listener map;
  struct wl_listener unmap;
  struct wl_listener destroy;
  struct wl_listener override_redirect;
};

enum atom_name
{
  NET_WM_WINDOW_TYPE_NORMAL,
  NET_WM_WINDOW_TYPE_DIALOG,
  NET_WM_WINDOW_TYPE_UTILITY,
  NET_WM_WINDOW_TYPE_TOOLBAR,
  NET_WM_WINDOW_TYPE_SPLASH,
  NET_WM_WINDOW_TYPE_MENU,
  NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
  NET_WM_WINDOW_TYPE_POPUP_MENU,
  NET_WM_WINDOW_TYPE_TOOLTIP,
  NET_WM_WINDOW_TYPE_NOTIFICATION,
  NET_WM_STATE_MODAL,
  ATOM_LAST,
};

struct wxrd_xwayland
{
  struct wlr_xwayland *wlr_xwayland;
  struct wlr_xcursor_manager *xcursor_manager;

  struct wxrd_server *server;

  xcb_atom_t atoms[ATOM_LAST];
};

void
handle_xwayland_ready (struct wl_listener *listener, void *data);
void
handle_xwayland_surface (struct wl_listener *listener, void *data);

#endif
