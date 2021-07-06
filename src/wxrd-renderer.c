/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/log.h>
#include <types/wlr_buffer.h>

#include <drm_fourcc.h>

#include "wxrd-renderer.h"

#define ALWAYS_UPLOAD_FULL_TEXTURES false
//#define DEBUG_BUFFER_LOCKS

// save full shm textures as /tmp/updated_texture-i.png
// #define SAVE_UPDATED_TEXTURE

// save updated region on shm textures as /tmp/updated_texture_region-i.png
// #define SAVE_UPDATED_TEXTURE_REGION

#if defined(SAVE_UPDATED_TEXTURE) || defined(SAVE_UPDATED_TEXTURE_REGION)
#include <gdk/gdk.h>
static void
save_texture (char *fn_base,
              int i,
              uint8_t *data,
              uint32_t width,
              uint32_t height,
              uint32_t stride)
{
  char fn[100];
  snprintf (fn, 100, "/tmp/%s-%d.png", fn_base, i++);
  GdkPixbuf *pb = gdk_pixbuf_new_from_data (data, GDK_COLORSPACE_RGB, true, 8,
                                            width, height, stride, NULL, NULL);
  gdk_pixbuf_savev (pb, fn, "png", NULL, NULL, NULL);
}
#endif


static const struct wlr_renderer_impl renderer_impl;

struct wxrd_renderer *
wxrd_get_renderer (struct wlr_renderer *wlr_renderer)
{
  assert (wlr_renderer->impl == &renderer_impl);
  return (struct wxrd_renderer *)wlr_renderer;
}


/*
 * The wayland formats are little endian while the GL formats are big endian,
 * so WL_SHM_FORMAT_ARGB8888 is actually compatible with GL_BGRA_EXT.
 */
static const struct wxrd_pixel_format formats[] = {
  {
      .drm_format = DRM_FORMAT_ARGB8888,
      .depth = 32,
      .bpp = 32,
      .vk_format = VK_FORMAT_B8G8R8A8_UNORM,
      .has_alpha = true,
  },
  {
      .drm_format = DRM_FORMAT_XRGB8888,
      .depth = 24,
      .bpp = 32,
      .vk_format = VK_FORMAT_B8G8R8A8_UNORM,
      .has_alpha = false,
  },
  {
      .drm_format = DRM_FORMAT_XBGR8888,
      .depth = 24,
      .bpp = 32,
      .vk_format = VK_FORMAT_R8G8B8A8_UNORM,
      .has_alpha = false,
  },
  {
      .drm_format = DRM_FORMAT_ABGR8888,
      .depth = 32,
      .bpp = 32,
      .vk_format = VK_FORMAT_R8G8B8A8_UNORM,
      .has_alpha = true,
  },
};

struct
{
  uint32_t drm_format;
  VkFormat vk_format;
  bool has_alpha;
} format_table[] = {
  {
      DRM_FORMAT_ABGR8888,
      VK_FORMAT_R8G8B8A8_UNORM,
      true,
  },
  {
      DRM_FORMAT_ARGB8888,
      VK_FORMAT_B8G8R8A8_UNORM,
      true,
  },

  {
      DRM_FORMAT_BGRA8888,
      VK_FORMAT_A8B8G8R8_UNORM_PACK32,
      true,
  }, // TODO
  {
      DRM_FORMAT_RGBA8888,
      VK_FORMAT_A8B8G8R8_UNORM_PACK32,
      true,
  }, // TODO

  {
      DRM_FORMAT_XBGR8888,
      VK_FORMAT_R8G8B8A8_UNORM,
      false,
  },
  {
      DRM_FORMAT_XRGB8888,
      VK_FORMAT_B8G8R8A8_UNORM,
      false,
  },

  {
      DRM_FORMAT_RGBX8888,
      VK_FORMAT_A8B8G8R8_UNORM_PACK32,
      false,
  }, // TODO
  {
      DRM_FORMAT_BGRX8888,
      VK_FORMAT_A8B8G8R8_UNORM_PACK32,
      false,
  }, // TODO
     //  { DRM_FORMAT_NV12, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, false, },
  {
      DRM_FORMAT_INVALID,
      VK_FORMAT_UNDEFINED,
      false,
  },
};

const uint32_t *
get_wxrd_shm_formats (size_t *len)
{
  static uint32_t shm_formats[sizeof (formats) / sizeof (formats[0])];
  *len = sizeof (formats) / sizeof (formats[0]);
  for (size_t i = 0; i < sizeof (formats) / sizeof (formats[0]); i++) {
    shm_formats[i] = formats[i].drm_format;
  }
  return shm_formats;
}

const struct wxrd_pixel_format *
get_wxrd_format_from_drm (uint32_t fmt)
{
  for (size_t i = 0; i < sizeof (formats) / sizeof (*formats); ++i) {
    if (formats[i].drm_format == fmt) {
      return &formats[i];
    }
  }
  return NULL;
}

const struct wxrd_pixel_format *
get_wxrd_format_from_vk (VkFormat vk_format, bool alpha)
{
  for (size_t i = 0; i < sizeof (formats) / sizeof (*formats); ++i) {
    if (formats[i].vk_format == vk_format) {
      return &formats[i];
    }
  }
  return NULL;
}

static void
wxrd_render_begin (struct wlr_renderer *wlr_renderer,
                   uint32_t width,
                   uint32_t height)
{}

static void
wxrd_render_end (struct wlr_renderer *wlr_renderer)
{}

static void
wxrd_render_clear (struct wlr_renderer *wlr_renderer,
                   const float color[static 4])
{}

static void
wxrd_render_scissor (struct wlr_renderer *wlr_renderer, struct wlr_box *box)
{}

static bool
wxrd_render_subtexture_with_matrix (struct wlr_renderer *wlr_renderer,
                                    struct wlr_texture *wlr_texture,
                                    const struct wlr_fbox *box,
                                    const float matrix[static 9],
                                    float alpha)
{
  wlr_log (WLR_ERROR, "unimplemented render sub texture");
  return true;
}

static void
wxrd_render_quad_with_matrix (struct wlr_renderer *wlr_renderer,
                              const float color[static 4],
                              const float matrix[static 9])
{
  wlr_log (WLR_ERROR, "unimplemented render quat");
}

static const uint32_t *
wxrd_renderer_formats (struct wlr_renderer *wlr_renderer, size_t *len)
{
  return get_wxrd_shm_formats (len);
}

static bool
wxrd_resource_is_wl_drm_buffer (struct wlr_renderer *wlr_renderer,
                                struct wl_resource *resource)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  struct wlr_egl *egl = wxrd_renderer_get_egl (&renderer->wlr_renderer);

  if (!egl->exts.bind_wayland_display_wl) {
    return false;
  }

  EGLint fmt;
  return egl->procs.eglQueryWaylandBufferWL (egl->display, resource,
                                             EGL_TEXTURE_FORMAT, &fmt);
}

static void
wxrd_wl_drm_buffer_get_size (struct wlr_renderer *wlr_renderer,
                             struct wl_resource *buffer,
                             int *width,
                             int *height)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  struct wlr_egl *egl = wxrd_renderer_get_egl (&renderer->wlr_renderer);

  if (!egl->exts.bind_wayland_display_wl) {
    return;
  }

  egl->procs.eglQueryWaylandBufferWL (egl->display, buffer, EGL_WIDTH, width);
  egl->procs.eglQueryWaylandBufferWL (egl->display, buffer, EGL_HEIGHT,
                                      height);
}

static struct wlr_drm_format_set supported_formats = { 0 };

void
init_formats (VkPhysicalDevice physicalDevice);

static const struct wlr_drm_format_set *
wxrd_get_dmabuf_formats (struct wlr_renderer *wlr_renderer)
{
  if (supported_formats.len == 0) {
    struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
    GulkanClient *gulkan = xrd_shell_get_gulkan (renderer->xrd_shell);
    init_formats (gulkan_client_get_physical_device_handle (gulkan));
  }
  return &supported_formats;
}

static const struct wlr_drm_format_set *
wxrd_get_dmabuf_render_formats (struct wlr_renderer *wlr_renderer)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  struct wlr_egl *egl = wxrd_renderer_get_egl (&renderer->wlr_renderer);
  return wlr_egl_get_dmabuf_render_formats (egl);
}

static uint32_t
wxrd_preferred_read_format (struct wlr_renderer *wlr_renderer)
{
  // struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);

  GLint gl_format = -1, gl_type = -1;
  glGetIntegerv (GL_IMPLEMENTATION_COLOR_READ_FORMAT, &gl_format);
  glGetIntegerv (GL_IMPLEMENTATION_COLOR_READ_TYPE, &gl_type);

  EGLint alpha_size = -1;
  alpha_size = 1; // TODO
  /*
  eglGetConfigAttrib (renderer->egl->display, renderer->egl->config,
                      EGL_ALPHA_SIZE, &alpha_size);
  */

  const struct wxrd_pixel_format *fmt
      = get_wxrd_format_from_vk (gl_format, alpha_size > 0);
  if (fmt != NULL) {
    return fmt->drm_format;
  }

  return DRM_FORMAT_BGRX8888;
}

static bool
wxrd_read_pixels (struct wlr_renderer *wlr_renderer,
                  uint32_t drm_format,
                  uint32_t *flags,
                  uint32_t stride,
                  uint32_t width,
                  uint32_t height,
                  uint32_t src_x,
                  uint32_t src_y,
                  uint32_t dst_x,
                  uint32_t dst_y,
                  void *data)
{
  const struct wxrd_pixel_format *fmt = get_wxrd_format_from_drm (drm_format);
  if (fmt == NULL) {
    wlr_log (WLR_ERROR, "Cannot read pixels: unsupported pixel format");
    return false;
  }

  wlr_log (WLR_ERROR, "unimplemented wxrd_read_pixels");
  return true;
}

static int
wxrd_get_drm_fd (struct wlr_renderer *wlr_renderer)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  struct wlr_egl *egl = wxrd_renderer_get_egl (&renderer->wlr_renderer);

  if (renderer->drm_fd < 0) {
    renderer->drm_fd = wlr_egl_dup_drm_fd (egl);
  }

  return renderer->drm_fd;
}

static bool
wxrd_init_wl_display (struct wlr_renderer *wlr_renderer,
                      struct wl_display *wl_display)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  struct wlr_egl *egl = wxrd_renderer_get_egl (&renderer->wlr_renderer);

  if (egl->exts.bind_wayland_display_wl) {
    if (!wlr_egl_bind_display (egl, wl_display)) {
      wlr_log (WLR_ERROR, "Failed to bind wl_display to EGL");
      return false;
    }
  } else {
    wlr_log (WLR_INFO, "EGL_WL_bind_wayland_display is not supported");
  }

  if (egl->exts.image_dmabuf_import_ext) {
    if (wlr_linux_dmabuf_v1_create (wl_display, wlr_renderer) == NULL) {
      return false;
    }
  } else {
    wlr_log (WLR_INFO, "EGL_EXT_image_dma_buf_import is not supported");
  }

  return true;
}

struct wlr_egl *
wxrd_renderer_get_egl (struct wlr_renderer *wlr_renderer)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  struct wlr_egl *egl
      = wlr_gles2_renderer_get_egl (renderer->wlr_backend_renderer);
  return egl;
}

static void
wxrd_render_destroy (struct wlr_renderer *wlr_renderer)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  if (renderer->drm_fd >= 0) {
    close (renderer->drm_fd);
  }
  free (renderer);
}

static bool
wxrd_texture_is_opaque (struct wlr_texture *wlr_texture)
{
  struct wxrd_texture *texture = wxrd_get_texture (wlr_texture);
  return !texture->has_alpha;
}

/* stride: length of a row in bytes
 * width, height: texel extent of the area to copy
 * src_x, src_y: texel coordinates of the rect to copy in the full src texture
 * dst_x, dst_y: texel coordinates of the rect to copy in the full dst texture
 * data: pointer to full source texture
 */
static bool
wxrd_texture_write_pixels (struct wlr_texture *wlr_texture,
                           uint32_t stride,
                           uint32_t width,
                           uint32_t height,
                           uint32_t src_x,
                           uint32_t src_y,
                           uint32_t dst_x,
                           uint32_t dst_y,
                           const void *data)
{
  struct wxrd_texture *texture = wxrd_get_texture (wlr_texture);

  const struct wxrd_pixel_format *fmt
      = get_wxrd_format_from_drm (texture->drm_format);
  assert (fmt);

#ifdef SAVE_UPDATED_TEXTURE
  {
    static int i = 0;
    save_texture ("updated_texture", i++, (uint8_t *)data,
                  texture->wlr_texture.width, texture->wlr_texture.height,
                  stride);
  }
#endif

  // the texel offset of the copied region in the full texture
  VkOffset2D offset = { .x = dst_x, .y = dst_y };
  // texel width and height of the region to copy.
  VkExtent2D extent = { .width = width, .height = height };
  int bytes_per_texel = fmt->bpp / 8;
  G3kContext *g3k = xrd_shell_get_g3k (texture->renderer->xrd_shell);
  VkImageLayout layout = g3k_context_get_upload_layout (g3k);
  gsize full_size = texture->wlr_texture.width * texture->wlr_texture.height
                    * bytes_per_texel;

  // wlr_log(WLR_DEBUG, "Uploading %dx%d src offset %d,%d dst offset
  // %d,%d, extent %d,%d, format %d, Bpp %d", width, height, src_x,
  // src_y, offset.x, offset.y, extent.width, extent.height,
  // fmt->vk_format, bytes_per_texel);

  if ((width == texture->wlr_texture.width
       && height == texture->wlr_texture.height)
      || ALWAYS_UPLOAD_FULL_TEXTURES) {
    gulkan_texture_upload_pixels (texture->gk, (guchar *)data, full_size,
                                  layout);
  } else {
    /* TODO gulkan_texture_upload_pixels_region uses memcpy to copy
     * data into a mapped vk buffer without using stride etc.
     * Therefore we cut & paste the pixel region from data into
     * region_data using several memcpy. Ideally gulkan would
     * create a vk buffer from the region in the full data.
     */

    // size of the region to copy in bytes
    gsize region_size = width * height * bytes_per_texel;

    // avoid allocating a fitting buffer every frame, just allocate
    // one that can hold all data once texture->region_data =
    if (texture->region_data == NULL) {
      texture->region_data = malloc (full_size);
    }
    uint8_t *region_ptr = texture->region_data;

    uint8_t *data_ptr = (uint8_t *)data;
    data_ptr += stride * src_y + src_x * bytes_per_texel;

    uint32_t region_stride = width * bytes_per_texel;

    for (uint32_t i = 0; i < height; i++) {
      memcpy (region_ptr, data_ptr, region_stride);
      data_ptr += stride;
      region_ptr += region_stride;
    }

    gulkan_texture_upload_pixels_region (texture->gk,
                                         (guchar *)texture->region_data,
                                         region_size, layout, offset, extent);

#ifdef SAVE_UPDATED_TEXTURE_REGION
    {
      static int i = 0;
      save_texture ("updated_texture_region", i++,
                    (uint8_t *)texture->region_data, width, height, stride);
    }
#endif
  }

  struct wlr_egl *egl
      = wxrd_renderer_get_egl (&texture->renderer->wlr_renderer);
  wlr_egl_unset_current (egl);
  return true;
}

static void
wxrd_texture_destroy (struct wxrd_texture *texture)
{
  wl_list_remove (&texture->link);
  wl_list_remove (&texture->buffer_destroy.link);
#ifdef DEBUG_BUFFER_LOCKS
  wlr_log (WLR_DEBUG, "destroy texture %p, gk %p, buffer %p [%zu]",
           (void *)texture, (void *)texture->gk, texture->buffer,
           texture->buffer ? texture->buffer->n_locks : 0);
#endif
  // HACK free the textures we extra reffed in wxrd_texture_from_*
#if 1 // gulkan texture is owned and cleared by xrdesktop after
      // xrd_window_set_and_submit
  if (texture->gk) {
    if (G_IS_OBJECT (texture->gk)) {
      wlr_log (WLR_DEBUG, "unref gulkan texture gk %p", (void *)texture->gk);
      g_object_unref (texture->gk);
    } else {
      wlr_log (WLR_ERROR, "Not clearing non object gulkan texture");
    }
  }
#endif

  free (texture->region_data);
  free (texture);

  // wlr_log(WLR_DEBUG, "Destroyed texture");
}

static void
wxrd_texture_unref (struct wlr_texture *wlr_texture)
{
  struct wxrd_texture *texture = wxrd_get_texture (wlr_texture);
  if (texture->buffer != NULL) {
    // Keep the texture around, in case the buffer is re-used later. We're
    // still listening to the buffer's destroy event.
    wlr_buffer_unlock (texture->buffer);
#ifdef DEBUG_BUFFER_LOCKS
    wlr_log (WLR_DEBUG, "texture %p gk %p unlock buffer: %p [%zu]",
             (void *)texture, (void *)texture->gk, texture->buffer,
             texture->buffer->n_locks);
#endif
  } else {
    wlr_log (WLR_DEBUG, "destroy %dx%d texture gk %p",
             texture->wlr_texture.width, texture->wlr_texture.height,
             (void *)texture->gk);
    wxrd_texture_destroy (texture);
  }
}

static const struct wlr_texture_impl texture_impl = {
  .is_opaque = wxrd_texture_is_opaque,
  .write_pixels = wxrd_texture_write_pixels,
  .destroy = wxrd_texture_unref,
};

struct wlr_texture *
wxrd_texture_from_pixels (struct wlr_renderer *wlr_renderer,
                          uint32_t drm_format,
                          uint32_t stride,
                          uint32_t width,
                          uint32_t height,
                          const void *data)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  const struct wxrd_pixel_format *fmt = get_wxrd_format_from_drm (drm_format);
  if (fmt == NULL) {
    wlr_log (WLR_ERROR, "Unsupported pixel format %" PRIu32, drm_format);
    return NULL;
  }

  struct wxrd_texture *texture = calloc (1, sizeof (struct wxrd_texture));
  if (texture == NULL) {
    wlr_log (WLR_ERROR, "Allocation failed");
    return NULL;
  }
  wlr_texture_init (&texture->wlr_texture, &texture_impl, width, height);

  wl_list_insert (&renderer->textures, &texture->link);
  wl_list_init (&texture->buffer_destroy.link);


  texture->renderer = renderer;
  texture->has_alpha = fmt->has_alpha;
  texture->drm_format = fmt->drm_format;

  G3kContext *g3k = xrd_shell_get_g3k (texture->renderer->xrd_shell);
  VkImageLayout layout = g3k_context_get_upload_layout (g3k);
  GulkanClient *client = xrd_shell_get_gulkan (renderer->xrd_shell);
  VkExtent2D extent = (VkExtent2D){ width, height };

  gsize size = width * height * (fmt->bpp / 8);

  // HACK ref texture so the returned wxrd_texture has shared ownership
  // of the texture->gk we will free it in wxrd_texture_destroy
  texture->gk
      = g_object_ref (gulkan_texture_new (client, extent, fmt->vk_format));

  wlr_log (WLR_DEBUG,
           "%dx%d texture stride %d bpp %d size %lu from pixels (%p, %p)",
           width, height, stride, fmt->bpp, size, (void *)texture,
           (void *)texture->gk);

  gulkan_texture_upload_pixels (texture->gk, (guchar *)data, size, layout);

  return &texture->wlr_texture;
}

struct wlr_texture *
wxrd_texture_from_wl_drm (struct wlr_renderer *wlr_renderer,
                          struct wl_resource *resource)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);
  struct wlr_egl *egl = wxrd_renderer_get_egl (&renderer->wlr_renderer);

  // TODO: can this be implemented on vulkan without using EGL as a middleman?
  wlr_log (WLR_ERROR, "unimplemented: wxrd_texture_from_wl_drm");

  if (!renderer->procs.glEGLImageTargetTexture2DOES) {
    return NULL;
  }

  EGLint fmt;
  int width, height;
  bool inverted_y;
  EGLImageKHR image = wlr_egl_create_image_from_wl_drm (
      egl, resource, &fmt, &width, &height, &inverted_y);
  if (image == EGL_NO_IMAGE_KHR) {
    wlr_log (WLR_ERROR, "Failed to create EGL image from wl_drm resource");
    return NULL;
  }

  struct wxrd_texture *texture = calloc (1, sizeof (struct wxrd_texture));
  if (texture == NULL) {
    wlr_log (WLR_ERROR, "Allocation failed");
    wlr_egl_destroy_image (egl, image);
    return NULL;
  }
  wlr_texture_init (&texture->wlr_texture, &texture_impl, width, height);
  texture->renderer = renderer;

  return NULL;
}

static struct GulkanDmabufAttributes
_make_gulkan_attribs (struct wlr_dmabuf_attributes *attrib)
{
  assert (WLR_DMABUF_MAX_PLANES == GULKAN_DMABUF_MAX_PLANES);
  assert (sizeof (struct wlr_dmabuf_attributes)
          == sizeof (struct GulkanDmabufAttributes));

  struct GulkanDmabufAttributes r;
  r.width = attrib->width;
  r.height = attrib->height;
  r.format = attrib->format;
  r.modifier = attrib->modifier;
  r.n_planes = attrib->n_planes;
  for (int i = 0; i < WLR_DMABUF_MAX_PLANES; i++) {
    r.offset[i] = attrib->offset[i];
    r.stride[i] = attrib->stride[i];
    r.fd[i] = attrib->fd[i];
  }
  return r;
}

void
init_formats (VkPhysicalDevice vk_physical_device)
{
  // only handle formats we explicitly know the drm->vk mapping for
  for (size_t i = 0; format_table[i].drm_format != DRM_FORMAT_INVALID; i++) {
    VkFormat format = format_table[i].vk_format;
    uint32_t drm_format = format_table[i].drm_format;

    // First, check whether the Vulkan format is supported
    VkPhysicalDeviceImageFormatInfo2 image_format_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = format,
      .type = VK_IMAGE_TYPE_2D,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
      .flags = 0,
    };
    VkImageFormatProperties2 image_format_props = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
    };
    VkResult res = vkGetPhysicalDeviceImageFormatProperties2 (
        vk_physical_device, &image_format_info, &image_format_props);
    if (res == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      wlr_log (WLR_DEBUG, "skipping init of unsupported format %zu: %d", i,
               format);
      continue;
    } else if (res != VK_SUCCESS) {
      wlr_log (WLR_ERROR,
               "vkGetPhysicalDeviceImageFormatProperties2 failed for DRM "
               "format 0x%" PRIX32 "\n",
               drm_format);
      continue;
    }

    VkDrmFormatModifierPropertiesListEXT modifier_props_list = {
      .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
    };
    VkFormatProperties2 format_props = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
      .pNext = &modifier_props_list,
    };
    vkGetPhysicalDeviceFormatProperties2 (vk_physical_device, format,
                                          &format_props);

    if (modifier_props_list.drmFormatModifierCount == 0) {
      wlr_log (WLR_ERROR,
               "vkGetPhysicalDeviceFormatProperties2 returned zero modifiers "
               "for DRM format 0x%" PRIX32 "\n",
               drm_format);
      continue;
    }

    VkDrmFormatModifierPropertiesEXT *modifier_props
        = malloc (sizeof (VkDrmFormatModifierPropertiesEXT)
                  * modifier_props_list.drmFormatModifierCount);
    modifier_props_list.pDrmFormatModifierProperties = modifier_props;
    vkGetPhysicalDeviceFormatProperties2 (vk_physical_device, format,
                                          &format_props);

    for (size_t j = 0; j < modifier_props_list.drmFormatModifierCount; j++) {
#if 0
      // TODO: what usage do we care about?
      if ((modifierProps[j].drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        continue;
      }
#endif

      // TODO: support drm modifiers with > 1 planes
      if (modifier_props[j].drmFormatModifierPlaneCount > 1) {
        wlr_log (WLR_DEBUG, "skip modifier %lu with %d planes",
                 modifier_props[j].drmFormatModifier,
                 modifier_props[j].drmFormatModifierPlaneCount);
        continue;
      }

      wlr_drm_format_set_add (&supported_formats, drm_format,
                              modifier_props[j].drmFormatModifier);
    }

    free (modifier_props);
  }

  wlr_log (WLR_DEBUG, "Supported DRM formats: ");
  for (size_t i = 0; i < supported_formats.len; i++) {
    uint32_t fmt = supported_formats.formats[i]->format;
    wlr_log (WLR_DEBUG, "0x%" PRIX32, fmt);
  }
}

struct wlr_texture *
wxrd_texture_from_dmabuf (struct wlr_renderer *wlr_renderer,
                          struct wlr_dmabuf_attributes *attribs)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);

  struct wxrd_texture *texture = calloc (1, sizeof (struct wxrd_texture));
  if (texture == NULL) {
    wlr_log (WLR_ERROR, "Allocation failed");
    return NULL;
  }
  wlr_texture_init (&texture->wlr_texture, &texture_impl, attribs->width,
                    attribs->height);

  wl_list_insert (&renderer->textures, &texture->link);
  wl_list_init (&texture->buffer_destroy.link);

  texture->renderer = renderer;
  texture->has_alpha = true;
  texture->drm_format = DRM_FORMAT_INVALID; // texture can't be written
  texture->inverted_y
      = (attribs->flags & WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT) != 0;

  GulkanClient *client = xrd_shell_get_gulkan (renderer->xrd_shell);

  if (supported_formats.len == 0) {
    wlr_log (WLR_DEBUG, "Init formats");
    init_formats (gulkan_client_get_physical_device_handle (client));
  }

  wlr_log (WLR_DEBUG, "creating %dx%d texture from dmabuf", attribs->width,
           attribs->height);

  struct GulkanDmabufAttributes gulkan_attribs
      = _make_gulkan_attribs (attribs);

  // HACK ref texture so the returned wxrd_texture has shared ownership
  // of the texture->gk we will free it in wxrd_texture_destroy
  texture->gk = g_object_ref (
      gulkan_texture_new_from_dmabuf_attribs (client, &gulkan_attribs));
  if (!texture->gk) {
    wlr_log (WLR_ERROR, "Failed to create texture");
    return NULL;
  }
  gulkan_texture_transfer_layout (texture->gk, VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  return &texture->wlr_texture;
}

static uint32_t
wxrd_get_render_buffer_caps (struct wlr_renderer *renderer)
{
  // wlr_log (WLR_ERROR, "wxrd_get_render_buffer_caps: dmabuf");
  return WLR_BUFFER_CAP_DMABUF;
}

static void
texture_handle_buffer_destroy (struct wl_listener *listener, void *data)
{
  struct wxrd_texture *texture
      = wl_container_of (listener, texture, buffer_destroy);
  wlr_log (WLR_DEBUG, "texture_handle_buffer_destroy %p", (void *)texture->gk);
}

static struct wlr_texture *
wxrd_texture_from_dmabuf_buffer (struct wxrd_renderer *renderer,
                                 struct wlr_buffer *buffer,
                                 struct wlr_dmabuf_attributes *dmabuf)
{
  struct wxrd_texture *texture;
  // wlr_log (WLR_DEBUG, "wxrd_texture_from_dmabuf_buffer");

  wl_list_for_each (texture, &renderer->textures, link)
  {
#ifdef DEBUG_BUFFER_LOCKS
    wlr_log (WLR_DEBUG, "Check if we already saw buffer %p [%zu]: %p [%zu]",
             buffer, buffer ? buffer->n_locks : 0, texture->buffer,
             texture->buffer ? texture->buffer->n_locks : 0);
#endif
    if (texture->buffer == buffer) {
      // TODO nothing to do?
      // wlr_log(WLR_ERROR, "invalidate texture");
      wlr_buffer_lock (texture->buffer);
#ifdef DEBUG_BUFFER_LOCKS
      wlr_log (WLR_DEBUG,
               "reused: texture %p gk %p lock & invalidate buffer %p [%zu]",
               (void *)texture, (void *)texture->gk, texture->buffer,
               texture->buffer->n_locks);
#endif
      return &texture->wlr_texture;
    }
  }

  struct wlr_texture *wlr_texture
      = wxrd_texture_from_dmabuf (&renderer->wlr_renderer, dmabuf);
  if (wlr_texture == NULL) {
    return false;
  }

  texture = wxrd_get_texture (wlr_texture);

  texture->buffer = wlr_buffer_lock (buffer);
#ifdef DEBUG_BUFFER_LOCKS
  wlr_log (WLR_DEBUG, "not reused: texture %p gk %p lock buffer %p  [%zu]",
           (void *)texture, (void *)texture->gk, texture->buffer,
           texture->buffer->n_locks);
#endif
  texture->buffer_destroy.notify = texture_handle_buffer_destroy;
  wl_signal_add (&buffer->events.destroy, &texture->buffer_destroy);

  return &texture->wlr_texture;
}

// TODO: these are copied from wlr_buffer.c because they are not exported
static bool
_buffer_begin_data_ptr_access (struct wlr_buffer *buffer,
                               void **data,
                               uint32_t *format,
                               size_t *stride)
{
  assert (!buffer->accessing_data_ptr);
  if (!buffer->impl->begin_data_ptr_access) {
    return false;
  }
  if (!buffer->impl->begin_data_ptr_access (buffer, data, format, stride)) {
    return false;
  }
  buffer->accessing_data_ptr = true;
  return true;
}

static void
_buffer_end_data_ptr_access (struct wlr_buffer *buffer)
{
  assert (buffer->accessing_data_ptr);
  buffer->impl->end_data_ptr_access (buffer);
  buffer->accessing_data_ptr = false;
}

struct wlr_texture *
wxrd_texture_from_buffer (struct wlr_renderer *wlr_renderer,
                          struct wlr_buffer *buffer)
{
  struct wxrd_renderer *renderer = wxrd_get_renderer (wlr_renderer);

  // wlr_log (WLR_DEBUG, "wxrd_texture_from_buffer");
  void *data;
  uint32_t format;
  size_t stride;
  struct wlr_dmabuf_attributes dmabuf;
  if (wlr_buffer_get_dmabuf (buffer, &dmabuf)) {
    return wxrd_texture_from_dmabuf_buffer (renderer, buffer, &dmabuf);
  } else if (_buffer_begin_data_ptr_access (buffer, &data, &format, &stride)) {
    struct wlr_texture *tex = wxrd_texture_from_pixels (
        wlr_renderer, format, stride, buffer->width, buffer->height, data);
    _buffer_end_data_ptr_access (buffer);
    return tex;
  } else {
    return NULL;
  }

  wlr_log (WLR_ERROR, "unimplemented: wxrd_texture_from_buffer");
  return NULL;
}

static bool
wxrd_bind_buffer (struct wlr_renderer *wlr_renderer,
                  struct wlr_buffer *wlr_buffer)
{
  wlr_log (WLR_ERROR, "bind buffer");
  return true;
}

static const struct wlr_renderer_impl renderer_impl = {
  .destroy = wxrd_render_destroy,
  .bind_buffer = wxrd_bind_buffer,
  .begin = wxrd_render_begin,
  .end = wxrd_render_end,
  .clear = wxrd_render_clear,
  .scissor = wxrd_render_scissor,
  .render_subtexture_with_matrix = wxrd_render_subtexture_with_matrix,
  .render_quad_with_matrix = wxrd_render_quad_with_matrix,
  .get_shm_texture_formats = wxrd_renderer_formats,
  .resource_is_wl_drm_buffer = wxrd_resource_is_wl_drm_buffer,
  .wl_drm_buffer_get_size = wxrd_wl_drm_buffer_get_size,
  .get_dmabuf_texture_formats = wxrd_get_dmabuf_formats,
  .get_render_formats = wxrd_get_dmabuf_render_formats,
  .preferred_read_format = wxrd_preferred_read_format,
  .read_pixels = wxrd_read_pixels,
  .texture_from_pixels = wxrd_texture_from_pixels,
  .texture_from_wl_drm = wxrd_texture_from_wl_drm,
  .texture_from_dmabuf = wxrd_texture_from_dmabuf,
  .init_wl_display = wxrd_init_wl_display,
  .get_drm_fd = wxrd_get_drm_fd,
  .get_render_buffer_caps = wxrd_get_render_buffer_caps,
  .texture_from_buffer = wxrd_texture_from_buffer,
};

struct wlr_renderer *
wxrd_renderer_create (struct wlr_renderer *backend_renderer)
{
  struct wxrd_renderer *renderer = calloc (1, sizeof (struct wxrd_renderer));
  if (renderer == NULL) {
    return NULL;
  }
  wlr_renderer_init (&renderer->wlr_renderer, &renderer_impl);

  wl_list_init (&renderer->buffers);
  wl_list_init (&renderer->textures);

  renderer->drm_fd = -1;
  renderer->wlr_backend_renderer = backend_renderer;

  return &renderer->wlr_renderer;
}

bool
wlr_texture_is_wxrd (struct wlr_texture *wlr_texture)
{
  return wlr_texture->impl == &texture_impl;
}

struct wxrd_texture *
wxrd_get_texture (struct wlr_texture *wlr_texture)
{
  assert (wlr_texture_is_wxrd (wlr_texture));
  return (struct wxrd_texture *)wlr_texture;
}
