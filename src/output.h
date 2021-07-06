/*
 * wxrd
 * Copyright 2021 Collabora Ltd.
 * Copyright 2019 Status Research & Development GmbH.
 * Author: Christoph Haag <christoph.haag@collabora.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef _WXRC_OUTPUT_H
#define _WXRC_OUTPUT_H

#include <wayland-server-core.h>

struct wxrd_output
{
  struct wlr_output *output;
  struct wxrd_server *server;

  struct wl_listener frame;
  struct wl_listener destroy;
};

#endif
