// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GIMP_IO_H
#define GIMP_IO_H

#include <libgimp/gimp.h>

#include "lanczos-resample.h"

typedef struct
{
  const Babl *format;
  gint        channels;
  gint        alpha_channel;
} LanczosGimpFormat;

typedef void (*LanczosGimpProgressFunc) (gdouble  fraction,
                                         gpointer data);

gboolean lanczos_gimp_format_for_drawable (GimpDrawable             *drawable,
                                           gboolean                  linear_light,
                                           LanczosGimpFormat        *format_info,
                                           GError                  **error);

gboolean lanczos_gegl_resample           (GeglBuffer               *src_buffer,
                                          GeglBuffer               *dst_buffer,
                                          gint                      src_width,
                                          gint                      src_height,
                                          gint                      dst_width,
                                          gint                      dst_height,
                                          const LanczosGimpFormat  *format_info,
                                          LanczosKernel             kernel,
                                          LanczosGimpProgressFunc   progress,
                                          gpointer                  progress_data,
                                          GError                  **error);

#endif
