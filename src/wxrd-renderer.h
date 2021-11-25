/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright (c) 2017, 2018 Drew DeV*ault
 * Copyright (c) 2014 Jari Vetoniemi
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef WXRD_RENDER_H
#define WXRD_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/log.h>

#include "GLES2/gl2ext.h"

#include <xrd.h>

// VkFormat
#include "vulkan/vulkan_core.h"

struct wxrd_pixel_format
{
  uint32_t drm_format;
  VkFormat vk_format;
  int depth, bpp;
  bool has_alpha;
};

struct wxrd_renderer
{
  struct wlr_renderer base;

  // required to deal with wayland egl buffers
  // struct wlr_egl *egl;

  struct wl_list buffers;  // wlr_gles2_buffer.link
  struct wl_list textures; // wlr_gles2_texture.link

  uint32_t viewport_width, viewport_height;
  XrdShell *xrd_shell;

  int drm_fd;
};

struct wxrd_texture
{
  struct wlr_texture wlr_texture;
  struct wxrd_renderer *renderer;

  bool has_alpha;

  uint32_t drm_format; // used to interpret upload data
  GulkanTexture *gk;

  // temporary storage for cropped region, same size as texture size.
  uint8_t *region_data;

  // If imported from a wlr_buffer
  struct wlr_buffer *buffer;
  struct wl_listener buffer_destroy;

  struct wl_list link; // wlr_gles2_renderer.textures
};

const struct wxrd_pixel_format *
get_wxrd_format_from_drm (uint32_t fmt);
const struct wxrd_pixel_format *
get_wxrd_format_from_gl (GLint gl_format, GLint gl_type, bool alpha);
const uint32_t *
get_wxrd_shm_formats (size_t *len);

struct wxrd_renderer *
wxrd_get_renderer (struct wlr_renderer *wlr_renderer);

struct wxrd_texture *
wxrd_get_texture (struct wlr_texture *wlr_texture);

struct wlr_texture *
wxrd_texture_from_pixels (struct wlr_renderer *wlr_renderer,
                          uint32_t drm_fmt,
                          uint32_t stride,
                          uint32_t width,
                          uint32_t height,
                          const void *data);
struct wlr_texture *
wxrd_texture_from_wl_drm (struct wlr_renderer *wlr_renderer,
                          struct wl_resource *data);
struct wlr_texture *
wxrd_texture_from_dmabuf (struct wlr_renderer *wlr_renderer,
                          struct wlr_dmabuf_attributes *attribs);

struct wlr_renderer *
wxrd_renderer_create (GulkanClient *gc);

#endif
