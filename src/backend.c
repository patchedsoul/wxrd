/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include "backend.h"
#include <assert.h>
#include <stdlib.h>
#include <wayland-client.h>
#include <wlr/render/wlr_renderer.h>
#include "wxrd-renderer.h"
#include <wlr/util/log.h>

#include <xrd.h>

static struct wxrd_xr_backend *
get_xr_backend_from_backend (struct wlr_backend *wlr_backend)
{
  assert (wxrd_backend_is_xr (wlr_backend));
  return (struct wxrd_xr_backend *)wlr_backend;
}

static bool
backend_start (struct wlr_backend *wlr_backend)
{
  struct wxrd_xr_backend *backend = get_xr_backend_from_backend (wlr_backend);
  assert (!backend->started);

  wlr_log (WLR_DEBUG, "Starting wlroots XR backend");

  backend->started = true;

  return true;
}

static void
backend_destroy (struct wlr_backend *wlr_backend)
{
  if (!wlr_backend) {
    return;
  }

  struct wxrd_xr_backend *backend = get_xr_backend_from_backend (wlr_backend);

  wl_signal_emit (&backend->base.events.destroy, &backend->base);

  wl_list_remove (&backend->local_display_destroy.link);

  free (backend);
}

static struct wlr_renderer *
backend_get_renderer (struct wlr_backend *wlr_backend)
{
  // since 0.13 we can't pass our renderer to the backend anymore
  // and backends with different renderers are not supported.
  // Luckily no renderer is actually needed specifically here.

  // struct wxrd_xr_backend *backend = get_xr_backend_from_backend
  // (wlr_backend); return backend->renderer;
  return NULL;
}

static struct wlr_backend_impl backend_impl = {
  .start = backend_start,
  .destroy = backend_destroy,
  .get_drm_fd = NULL, // TODO
};

bool
wxrd_backend_is_xr (struct wlr_backend *wlr_backend)
{
  return wlr_backend->impl == &backend_impl;
}

static void
handle_display_destroy (struct wl_listener *listener, void *data)
{
  struct wxrd_xr_backend *backend
      = wl_container_of (listener, backend, local_display_destroy);
  backend_destroy (&backend->base);
}

static bool
xrdesktop_init (struct wxrd_xr_backend *backend)
{
  if (!xrd_settings_is_schema_installed ()) {
    wlr_log (WLR_ERROR,
             "GSettings schema not found. Check xrdesktop installation!");
    return false;
  }

  GSList *device_exts = NULL;
  device_exts = g_slist_append (
      device_exts, VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
  device_exts
      = g_slist_append (device_exts, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
  device_exts
      = g_slist_append (device_exts, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
  device_exts = g_slist_append (
      device_exts, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
  device_exts
      = g_slist_append (device_exts, VK_KHR_MAINTENANCE1_EXTENSION_NAME);

  backend->xrd_shell
      = xrd_shell_new_from_vulkan_extensions (NULL, device_exts);

  g_slist_free (device_exts);

  if (backend->xrd_shell == NULL) {
    return false;
  }
  return true;
}

struct wxrd_xr_backend *
wxrd_xr_backend_create (struct wl_display *display)
{
  struct wxrd_xr_backend *backend = calloc (1, sizeof (*backend));
  if (backend == NULL) {
    wlr_log_errno (WLR_ERROR, "calloc failed");
    return NULL;
  }
  wlr_backend_init (&backend->base, &backend_impl);

  backend->local_display_destroy.notify = handle_display_destroy;
  wl_display_add_destroy_listener (display, &backend->local_display_destroy);

  if (!xrdesktop_init (backend)) {
    wlr_log (WLR_ERROR, "xrdesktop init failed");
    backend_destroy (&backend->base);
    return NULL;
  }

  GulkanClient *gc = xrd_shell_get_gulkan (backend->xrd_shell);
  backend->renderer = wxrd_renderer_create (gc);

  struct wxrd_renderer *wxrd_r = wxrd_get_renderer (backend->renderer);
  wxrd_r->xrd_shell = backend->xrd_shell;

  return backend;
}
