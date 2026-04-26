// SPDX-License-Identifier: GPL-3.0-or-later

#include "gimp-io.h"

#include <math.h>
#include <string.h>

typedef struct
{
  gint    row;
  guint64 used_at;
  gfloat *pixels;
} RowCacheSlot;

static void
set_error (GError      **error,
           const gchar  *message)
{
  if (error)
    g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0, message);
}

static gboolean
mul_gsize_overflows (gsize  a,
                     gsize  b,
                     gsize *result)
{
  if (a != 0 && b > ((gsize) -1) / a)
    return TRUE;

  *result = a * b;
  return FALSE;
}

static gint
clamp_gint (gint value,
            gint low,
            gint high)
{
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

gboolean
lanczos_gimp_format_for_drawable (GimpDrawable       *drawable,
                                  gboolean            linear_light,
                                  LanczosGimpFormat  *format_info,
                                  GError            **error)
{
  const Babl    *drawable_format;
  const Babl    *space;
  const gchar   *encoding = NULL;
  GimpImageType  type;
  gboolean       has_alpha;

  g_return_val_if_fail (format_info != NULL, FALSE);

  type = gimp_drawable_type (drawable);
  has_alpha = gimp_drawable_has_alpha (drawable);

  switch (type)
    {
    case GIMP_RGB_IMAGE:
    case GIMP_RGBA_IMAGE:
      encoding = linear_light ?
                 (has_alpha ? "RGBA float" : "RGB float") :
                 (has_alpha ? "R'G'B'A float" : "R'G'B' float");
      format_info->channels = has_alpha ? 4 : 3;
      format_info->alpha_channel = has_alpha ? 3 : -1;
      break;

    case GIMP_GRAY_IMAGE:
    case GIMP_GRAYA_IMAGE:
      encoding = linear_light ?
                 (has_alpha ? "YA float" : "Y float") :
                 (has_alpha ? "Y'A float" : "Y' float");
      format_info->channels = has_alpha ? 2 : 1;
      format_info->alpha_channel = has_alpha ? 1 : -1;
      break;

    case GIMP_INDEXED_IMAGE:
    case GIMP_INDEXEDA_IMAGE:
      set_error (error, "Indexed drawables are not supported by Lanczos Scale.");
      return FALSE;
    }

  drawable_format = gimp_drawable_get_format (drawable);
  space = babl_format_get_space (drawable_format);

  format_info->format = babl_format_with_space (encoding, space);

  if (! format_info->format)
    {
      set_error (error, "Could not create a floating-point GEGL format.");
      return FALSE;
    }

  return TRUE;
}

static RowCacheSlot *
find_cache_slot (RowCacheSlot *cache,
                 gint          cache_size,
                 gint          row)
{
  gint i;

  for (i = 0; i < cache_size; i++)
    {
      if (cache[i].row == row)
        return &cache[i];
    }

  return NULL;
}

static RowCacheSlot *
choose_cache_slot (RowCacheSlot *cache,
                   gint          cache_size)
{
  RowCacheSlot *slot = &cache[0];
  gint          i;

  for (i = 0; i < cache_size; i++)
    {
      if (cache[i].row < 0)
        return &cache[i];

      if (cache[i].used_at < slot->used_at)
        slot = &cache[i];
    }

  return slot;
}

static void
row_cache_free (RowCacheSlot *cache,
                gint          cache_size)
{
  if (! cache)
    return;

  for (gint i = 0; i < cache_size; i++)
    g_free (cache[i].pixels);

  g_free (cache);
}

static void
load_horizontal_row (GeglBuffer                *src_buffer,
                     gint                       src_width,
                     gint                       src_row,
                     const LanczosGimpFormat   *format_info,
                     const LanczosContribTable *x_table,
                     gfloat                    *src_pixels,
                     RowCacheSlot              *slot)
{
  gegl_buffer_get (src_buffer,
                   GEGL_RECTANGLE (0, src_row, src_width, 1),
                   1.0,
                   format_info->format,
                   src_pixels,
                   GEGL_AUTO_ROWSTRIDE,
                   GEGL_ABYSS_NONE);

  lanczos_resample_horizontal_row (src_pixels,
                                   slot->pixels,
                                   format_info->channels,
                                   format_info->alpha_channel,
                                   x_table);

  slot->row = src_row;
}

static void
load_source_row (GeglBuffer              *src_buffer,
                 gint                     src_width,
                 gint                     src_row,
                 const LanczosGimpFormat *format_info,
                 RowCacheSlot            *slot)
{
  gegl_buffer_get (src_buffer,
                   GEGL_RECTANGLE (0, src_row, src_width, 1),
                   1.0,
                   format_info->format,
                   slot->pixels,
                   GEGL_AUTO_ROWSTRIDE,
                   GEGL_ABYSS_NONE);

  slot->row = src_row;
}

static const gfloat *
get_source_row (GeglBuffer              *src_buffer,
                gint                     src_width,
                gint                     src_row,
                const LanczosGimpFormat *format_info,
                RowCacheSlot            *cache,
                gint                     cache_size,
                guint64                 *use_counter)
{
  RowCacheSlot *slot = find_cache_slot (cache, cache_size, src_row);

  if (! slot)
    {
      slot = choose_cache_slot (cache, cache_size);
      load_source_row (src_buffer, src_width, src_row, format_info, slot);
    }

  slot->used_at = (*use_counter)++;

  return slot->pixels;
}

static void
ewa_accumulate_pixel (gdouble      *accum,
                      const gfloat *src_pixel,
                      gint          channels,
                      gint          alpha_channel,
                      gdouble       weight)
{
  if (alpha_channel >= 0)
    {
      gdouble alpha = src_pixel[alpha_channel];

      for (gint c = 0; c < channels; c++)
        {
          gdouble value = src_pixel[c];

          if (c != alpha_channel)
            value *= alpha;

          accum[c] += value * weight;
        }
    }
  else
    {
      for (gint c = 0; c < channels; c++)
        accum[c] += (gdouble) src_pixel[c] * weight;
    }
}

typedef enum
{
  EWA_LAYOUT_GENERIC,
  EWA_LAYOUT_Y,
  EWA_LAYOUT_YA,
  EWA_LAYOUT_RGB,
  EWA_LAYOUT_RGBA,
} EwaLayout;

static EwaLayout
ewa_layout_for_format (gint channels,
                       gint alpha_channel)
{
  if (channels == 1 && alpha_channel < 0)
    return EWA_LAYOUT_Y;

  if (channels == 2 && alpha_channel == 1)
    return EWA_LAYOUT_YA;

  if (channels == 3 && alpha_channel < 0)
    return EWA_LAYOUT_RGB;

  if (channels == 4 && alpha_channel == 3)
    return EWA_LAYOUT_RGBA;

  return EWA_LAYOUT_GENERIC;
}

static const gfloat *
ewa_nearest_pixel (GeglBuffer              *src_buffer,
                   gint                     src_width,
                   gint                     src_height,
                   const LanczosGimpFormat *format_info,
                   RowCacheSlot            *cache,
                   gint                     cache_size,
                   guint64                 *use_counter,
                   gint                     channels,
                   const LanczosEwaAxisItem *x_axis,
                   const LanczosEwaAxisItem *y_axis)
{
  gint          src_x = clamp_gint ((gint) floor (x_axis->center + 0.5),
                                    0, src_width - 1);
  gint          src_y = clamp_gint ((gint) floor (y_axis->center + 0.5),
                                    0, src_height - 1);
  const gfloat *src_row;

  src_row = get_source_row (src_buffer,
                            src_width,
                            src_y,
                            format_info,
                            cache,
                            cache_size,
                            use_counter);

  return src_row + ((gsize) src_x * (gsize) channels);
}

static void
ewa_pixel_y (GeglBuffer               *src_buffer,
             gint                      src_width,
             gint                      src_height,
             const LanczosGimpFormat  *format_info,
             RowCacheSlot             *cache,
             gint                      cache_size,
             guint64                  *use_counter,
             const LanczosEwaAxisItem *x_axis,
             const LanczosEwaAxisItem *y_axis,
             const LanczosEwaWeightLut *weight_lut,
             gfloat                   *dst_pixel)
{
  gdouble y = 0.0;
  gdouble weight_sum = 0.0;

  for (gint sy = y_axis->raw_start; sy <= y_axis->raw_end; sy++)
    {
      gint          src_y = clamp_gint (sy, 0, src_height - 1);
      gdouble       dist_y = (y_axis->center - (gdouble) sy) *
                             y_axis->filter_scale;
      gdouble       dist_y2 = dist_y * dist_y;
      const gfloat *src_row;

      src_row = get_source_row (src_buffer,
                                src_width,
                                src_y,
                                format_info,
                                cache,
                                cache_size,
                                use_counter);

      for (gint sx = x_axis->raw_start; sx <= x_axis->raw_end; sx++)
        {
          gint    src_x = clamp_gint (sx, 0, src_width - 1);
          gdouble dist_x = (x_axis->center - (gdouble) sx) *
                           x_axis->filter_scale;
          gdouble weight = lanczos_ewa_weight_lut_lookup (weight_lut,
                                                          (dist_x * dist_x) + dist_y2);

          if (weight == 0.0)
            continue;

          y += (gdouble) src_row[src_x] * weight;
          weight_sum += weight;
        }
    }

  if (fabs (weight_sum) <= 1.0e-12)
    dst_pixel[0] = ewa_nearest_pixel (src_buffer, src_width, src_height,
                                      format_info, cache, cache_size,
                                      use_counter, 1, x_axis, y_axis)[0];
  else
    dst_pixel[0] = (gfloat) (y / weight_sum);
}

static void
ewa_pixel_ya (GeglBuffer               *src_buffer,
              gint                      src_width,
              gint                      src_height,
              const LanczosGimpFormat  *format_info,
              RowCacheSlot             *cache,
              gint                      cache_size,
              guint64                  *use_counter,
              const LanczosEwaAxisItem *x_axis,
              const LanczosEwaAxisItem *y_axis,
              const LanczosEwaWeightLut *weight_lut,
              gfloat                   *dst_pixel)
{
  gdouble y = 0.0;
  gdouble a = 0.0;
  gdouble weight_sum = 0.0;
  gdouble accum[2];

  for (gint sy = y_axis->raw_start; sy <= y_axis->raw_end; sy++)
    {
      gint          src_y = clamp_gint (sy, 0, src_height - 1);
      gdouble       dist_y = (y_axis->center - (gdouble) sy) *
                             y_axis->filter_scale;
      gdouble       dist_y2 = dist_y * dist_y;
      const gfloat *src_row;

      src_row = get_source_row (src_buffer,
                                src_width,
                                src_y,
                                format_info,
                                cache,
                                cache_size,
                                use_counter);

      for (gint sx = x_axis->raw_start; sx <= x_axis->raw_end; sx++)
        {
          gint          src_x = clamp_gint (sx, 0, src_width - 1);
          gdouble       dist_x = (x_axis->center - (gdouble) sx) *
                                 x_axis->filter_scale;
          gdouble       weight = lanczos_ewa_weight_lut_lookup (weight_lut,
                                                                (dist_x * dist_x) + dist_y2);
          const gfloat *src_pixel;
          gdouble       alpha;

          if (weight == 0.0)
            continue;

          src_pixel = src_row + ((gsize) src_x * 2u);
          alpha = src_pixel[1];

          y += (gdouble) src_pixel[0] * alpha * weight;
          a += alpha * weight;
          weight_sum += weight;
        }
    }

  if (fabs (weight_sum) <= 1.0e-12)
    {
      const gfloat *src_pixel;
      gdouble       alpha;

      src_pixel = ewa_nearest_pixel (src_buffer, src_width, src_height,
                                     format_info, cache, cache_size,
                                     use_counter, 2, x_axis, y_axis);
      alpha = src_pixel[1];

      accum[0] = (gdouble) src_pixel[0] * alpha;
      accum[1] = alpha;
    }
  else
    {
      accum[0] = y / weight_sum;
      accum[1] = a / weight_sum;
    }

  lanczos_resample_store_pixel (accum, dst_pixel, 2, 1);
}

static void
ewa_pixel_rgb (GeglBuffer               *src_buffer,
               gint                      src_width,
               gint                      src_height,
               const LanczosGimpFormat  *format_info,
               RowCacheSlot             *cache,
               gint                      cache_size,
               guint64                  *use_counter,
               const LanczosEwaAxisItem *x_axis,
               const LanczosEwaAxisItem *y_axis,
               const LanczosEwaWeightLut *weight_lut,
               gfloat                   *dst_pixel)
{
  gdouble r = 0.0;
  gdouble g = 0.0;
  gdouble b = 0.0;
  gdouble weight_sum = 0.0;

  for (gint sy = y_axis->raw_start; sy <= y_axis->raw_end; sy++)
    {
      gint          src_y = clamp_gint (sy, 0, src_height - 1);
      gdouble       dist_y = (y_axis->center - (gdouble) sy) *
                             y_axis->filter_scale;
      gdouble       dist_y2 = dist_y * dist_y;
      const gfloat *src_row;

      src_row = get_source_row (src_buffer,
                                src_width,
                                src_y,
                                format_info,
                                cache,
                                cache_size,
                                use_counter);

      for (gint sx = x_axis->raw_start; sx <= x_axis->raw_end; sx++)
        {
          gint          src_x = clamp_gint (sx, 0, src_width - 1);
          gdouble       dist_x = (x_axis->center - (gdouble) sx) *
                                 x_axis->filter_scale;
          gdouble       weight = lanczos_ewa_weight_lut_lookup (weight_lut,
                                                                (dist_x * dist_x) + dist_y2);
          const gfloat *src_pixel;

          if (weight == 0.0)
            continue;

          src_pixel = src_row + ((gsize) src_x * 3u);

          r += (gdouble) src_pixel[0] * weight;
          g += (gdouble) src_pixel[1] * weight;
          b += (gdouble) src_pixel[2] * weight;
          weight_sum += weight;
        }
    }

  if (fabs (weight_sum) <= 1.0e-12)
    {
      const gfloat *src_pixel;

      src_pixel = ewa_nearest_pixel (src_buffer, src_width, src_height,
                                     format_info, cache, cache_size,
                                     use_counter, 3, x_axis, y_axis);

      dst_pixel[0] = src_pixel[0];
      dst_pixel[1] = src_pixel[1];
      dst_pixel[2] = src_pixel[2];
    }
  else
    {
      dst_pixel[0] = (gfloat) (r / weight_sum);
      dst_pixel[1] = (gfloat) (g / weight_sum);
      dst_pixel[2] = (gfloat) (b / weight_sum);
    }
}

static void
ewa_pixel_rgba (GeglBuffer               *src_buffer,
                gint                      src_width,
                gint                      src_height,
                const LanczosGimpFormat  *format_info,
                RowCacheSlot             *cache,
                gint                      cache_size,
                guint64                  *use_counter,
                const LanczosEwaAxisItem *x_axis,
                const LanczosEwaAxisItem *y_axis,
                const LanczosEwaWeightLut *weight_lut,
                gfloat                   *dst_pixel)
{
  gdouble r = 0.0;
  gdouble g = 0.0;
  gdouble b = 0.0;
  gdouble a = 0.0;
  gdouble weight_sum = 0.0;
  gdouble accum[4];

  for (gint sy = y_axis->raw_start; sy <= y_axis->raw_end; sy++)
    {
      gint          src_y = clamp_gint (sy, 0, src_height - 1);
      gdouble       dist_y = (y_axis->center - (gdouble) sy) *
                             y_axis->filter_scale;
      gdouble       dist_y2 = dist_y * dist_y;
      const gfloat *src_row;

      src_row = get_source_row (src_buffer,
                                src_width,
                                src_y,
                                format_info,
                                cache,
                                cache_size,
                                use_counter);

      for (gint sx = x_axis->raw_start; sx <= x_axis->raw_end; sx++)
        {
          gint          src_x = clamp_gint (sx, 0, src_width - 1);
          gdouble       dist_x = (x_axis->center - (gdouble) sx) *
                                 x_axis->filter_scale;
          gdouble       weight = lanczos_ewa_weight_lut_lookup (weight_lut,
                                                                (dist_x * dist_x) + dist_y2);
          const gfloat *src_pixel;
          gdouble       alpha;
          gdouble       premul_weight;

          if (weight == 0.0)
            continue;

          src_pixel = src_row + ((gsize) src_x * 4u);
          alpha = src_pixel[3];
          premul_weight = alpha * weight;

          r += (gdouble) src_pixel[0] * premul_weight;
          g += (gdouble) src_pixel[1] * premul_weight;
          b += (gdouble) src_pixel[2] * premul_weight;
          a += alpha * weight;
          weight_sum += weight;
        }
    }

  if (fabs (weight_sum) <= 1.0e-12)
    {
      const gfloat *src_pixel;
      gdouble       alpha;

      src_pixel = ewa_nearest_pixel (src_buffer, src_width, src_height,
                                     format_info, cache, cache_size,
                                     use_counter, 4, x_axis, y_axis);
      alpha = src_pixel[3];

      accum[0] = (gdouble) src_pixel[0] * alpha;
      accum[1] = (gdouble) src_pixel[1] * alpha;
      accum[2] = (gdouble) src_pixel[2] * alpha;
      accum[3] = alpha;
    }
  else
    {
      accum[0] = r / weight_sum;
      accum[1] = g / weight_sum;
      accum[2] = b / weight_sum;
      accum[3] = a / weight_sum;
    }

  lanczos_resample_store_pixel (accum, dst_pixel, 4, 3);
}

static void
ewa_pixel_generic (GeglBuffer               *src_buffer,
                   gint                      src_width,
                   gint                      src_height,
                   const LanczosGimpFormat  *format_info,
                   RowCacheSlot             *cache,
                   gint                      cache_size,
                   guint64                  *use_counter,
                   gint                      channels,
                   gint                      alpha_channel,
                   const LanczosEwaAxisItem *x_axis,
                   const LanczosEwaAxisItem *y_axis,
                   const LanczosEwaWeightLut *weight_lut,
                   gdouble                  *accum,
                   gsize                     accum_bytes,
                   gfloat                   *dst_pixel)
{
  gdouble weight_sum = 0.0;

  memset (accum, 0, accum_bytes);

  for (gint sy = y_axis->raw_start; sy <= y_axis->raw_end; sy++)
    {
      gint          src_y = clamp_gint (sy, 0, src_height - 1);
      gdouble       dist_y = (y_axis->center - (gdouble) sy) *
                             y_axis->filter_scale;
      gdouble       dist_y2 = dist_y * dist_y;
      const gfloat *src_row;

      src_row = get_source_row (src_buffer,
                                src_width,
                                src_y,
                                format_info,
                                cache,
                                cache_size,
                                use_counter);

      for (gint sx = x_axis->raw_start; sx <= x_axis->raw_end; sx++)
        {
          gint          src_x = clamp_gint (sx, 0, src_width - 1);
          gdouble       dist_x = (x_axis->center - (gdouble) sx) *
                                 x_axis->filter_scale;
          gdouble       weight = lanczos_ewa_weight_lut_lookup (weight_lut,
                                                                (dist_x * dist_x) + dist_y2);
          const gfloat *src_pixel;

          if (weight == 0.0)
            continue;

          src_pixel = src_row + ((gsize) src_x * (gsize) channels);

          ewa_accumulate_pixel (accum,
                                src_pixel,
                                channels,
                                alpha_channel,
                                weight);
          weight_sum += weight;
        }
    }

  if (fabs (weight_sum) <= 1.0e-12)
    {
      const gfloat *src_pixel;

      src_pixel = ewa_nearest_pixel (src_buffer, src_width, src_height,
                                     format_info, cache, cache_size,
                                     use_counter, channels, x_axis, y_axis);

      memset (accum, 0, accum_bytes);
      ewa_accumulate_pixel (accum,
                            src_pixel,
                            channels,
                            alpha_channel,
                            1.0);
    }
  else
    {
      for (gint c = 0; c < channels; c++)
        accum[c] /= weight_sum;
    }

  lanczos_resample_store_pixel (accum,
                                dst_pixel,
                                channels,
                                alpha_channel);
}

static gboolean
lanczos_gegl_resample_ewa (GeglBuffer               *src_buffer,
                           GeglBuffer               *dst_buffer,
                           gint                      src_width,
                           gint                      src_height,
                           gint                      dst_width,
                           gint                      dst_height,
                           const LanczosGimpFormat  *format_info,
                           LanczosKernel             kernel,
                           LanczosGimpProgressFunc   progress,
                           gpointer                  progress_data,
                           GError                  **error)
{
  LanczosEwaAxisTable *x_table = NULL;
  LanczosEwaAxisTable *y_table = NULL;
  LanczosEwaWeightLut *weight_lut = NULL;
  RowCacheSlot        *cache = NULL;
  gfloat              *dst_row = NULL;
  gdouble             *accum = NULL;
  gsize                src_row_values = 0;
  gsize                dst_row_values = 0;
  gsize                accum_bytes = 0;
  gint                 cache_size = 0;
  gint                 channels = format_info->channels;
  gint                 alpha_channel = format_info->alpha_channel;
  EwaLayout            layout;
  guint64              use_counter = 1;

  layout = ewa_layout_for_format (channels, alpha_channel);

  if (mul_gsize_overflows ((gsize) src_width,
                           (gsize) channels,
                           &src_row_values) ||
      mul_gsize_overflows ((gsize) dst_width,
                           (gsize) channels,
                           &dst_row_values) ||
      mul_gsize_overflows ((gsize) channels,
                           sizeof (*accum),
                           &accum_bytes))
    {
      set_error (error, "Image dimensions are too large.");
      goto fail;
    }

  x_table = lanczos_ewa_axis_table_new (src_width, dst_width, kernel);
  y_table = lanczos_ewa_axis_table_new (src_height, dst_height, kernel);
  weight_lut = lanczos_ewa_weight_lut_new (kernel,
                                           LANCZOS_EWA_WEIGHT_LUT_SIZE);
  if (! x_table || ! y_table || ! weight_lut)
    {
      set_error (error, "Could not allocate EWA filter tables.");
      goto fail;
    }

  cache_size = y_table->max_taps + 2;
  cache = g_try_new0 (RowCacheSlot, (gsize) cache_size);
  dst_row = g_try_new (gfloat, dst_row_values);
  accum = g_try_new (gdouble, (gsize) channels);

  if (! cache || ! dst_row || ! accum)
    {
      set_error (error, "Could not allocate EWA row buffers.");
      goto fail;
    }

  for (gint i = 0; i < cache_size; i++)
    {
      cache[i].row = -1;
      cache[i].pixels = g_try_new (gfloat, src_row_values);
      if (! cache[i].pixels)
        {
          set_error (error, "Could not allocate EWA row cache.");
          goto fail;
        }
    }

  for (gint dy = 0; dy < dst_height; dy++)
    {
      const LanczosEwaAxisItem *y_axis = &y_table->items[dy];

      for (gint dx_out = 0; dx_out < dst_width; dx_out++)
        {
          const LanczosEwaAxisItem *x_axis = &x_table->items[dx_out];
          gfloat                   *dst_pixel = dst_row +
                                                ((gsize) dx_out *
                                                 (gsize) channels);

          switch (layout)
            {
            case EWA_LAYOUT_Y:
              ewa_pixel_y (src_buffer, src_width, src_height,
                           format_info, cache, cache_size,
                           &use_counter, x_axis, y_axis,
                           weight_lut, dst_pixel);
              break;

            case EWA_LAYOUT_YA:
              ewa_pixel_ya (src_buffer, src_width, src_height,
                            format_info, cache, cache_size,
                            &use_counter, x_axis, y_axis,
                            weight_lut, dst_pixel);
              break;

            case EWA_LAYOUT_RGB:
              ewa_pixel_rgb (src_buffer, src_width, src_height,
                             format_info, cache, cache_size,
                             &use_counter, x_axis, y_axis,
                             weight_lut, dst_pixel);
              break;

            case EWA_LAYOUT_RGBA:
              ewa_pixel_rgba (src_buffer, src_width, src_height,
                              format_info, cache, cache_size,
                              &use_counter, x_axis, y_axis,
                              weight_lut, dst_pixel);
              break;

            case EWA_LAYOUT_GENERIC:
              ewa_pixel_generic (src_buffer, src_width, src_height,
                                 format_info, cache, cache_size,
                                 &use_counter, channels, alpha_channel,
                                 x_axis, y_axis, weight_lut,
                                 accum, accum_bytes, dst_pixel);
              break;
            }
        }

      gegl_buffer_set (dst_buffer,
                       GEGL_RECTANGLE (0, dy, dst_width, 1),
                       0,
                       format_info->format,
                       dst_row,
                       GEGL_AUTO_ROWSTRIDE);

      if (progress)
        progress ((gdouble) (dy + 1) / (gdouble) dst_height, progress_data);
    }

  if (progress)
    progress (1.0, progress_data);

  gegl_buffer_flush (dst_buffer);

  row_cache_free (cache, cache_size);
  g_free (dst_row);
  g_free (accum);
  lanczos_ewa_axis_table_free (x_table);
  lanczos_ewa_axis_table_free (y_table);
  lanczos_ewa_weight_lut_free (weight_lut);

  return TRUE;

fail:
  row_cache_free (cache, cache_size);
  g_free (dst_row);
  g_free (accum);
  lanczos_ewa_axis_table_free (x_table);
  lanczos_ewa_axis_table_free (y_table);
  lanczos_ewa_weight_lut_free (weight_lut);

  return FALSE;
}

gboolean
lanczos_gegl_resample (GeglBuffer               *src_buffer,
                       GeglBuffer               *dst_buffer,
                       gint                      src_width,
                       gint                      src_height,
                       gint                      dst_width,
                       gint                      dst_height,
                       const LanczosGimpFormat  *format_info,
                       LanczosKernel             kernel,
                       LanczosGimpProgressFunc   progress,
                       gpointer                  progress_data,
                       GError                  **error)
{
  LanczosContribTable *x_table = NULL;
  LanczosContribTable *y_table = NULL;
  RowCacheSlot        *cache   = NULL;
  const gfloat       **tap_rows = NULL;
  gfloat              *src_row = NULL;
  gfloat              *dst_row = NULL;
  gdouble             *accum_row = NULL;
  gsize                src_row_values = 0;
  gsize                dst_row_values = 0;
  gsize                accum_row_bytes = 0;
  gint                 cache_size = 0;
  gint                 channels;
  gint                 alpha_channel;
  gint                 dy;
  guint64              use_counter = 1;

  g_return_val_if_fail (src_buffer != NULL, FALSE);
  g_return_val_if_fail (dst_buffer != NULL, FALSE);
  g_return_val_if_fail (format_info != NULL, FALSE);

  if (src_width <= 0 || src_height <= 0 ||
      dst_width <= 0 || dst_height <= 0 ||
      format_info->channels <= 0 || format_info->channels > LANCZOS_MAX_CHANNELS ||
      format_info->alpha_channel < -1 ||
      format_info->alpha_channel >= format_info->channels ||
      ! lanczos_kernel_is_valid (kernel))
    {
      set_error (error, "Invalid image dimensions or channel count.");
      return FALSE;
    }

  channels = format_info->channels;
  alpha_channel = format_info->alpha_channel;

  if (src_width == dst_width && src_height == dst_height)
    {
      gegl_buffer_copy (src_buffer,
                        GEGL_RECTANGLE (0, 0, src_width, src_height),
                        GEGL_ABYSS_NONE,
                        dst_buffer,
                        GEGL_RECTANGLE (0, 0, dst_width, dst_height));
      gegl_buffer_flush (dst_buffer);

      if (progress)
        progress (1.0, progress_data);

      return TRUE;
    }

  if (lanczos_kernel_is_ewa (kernel))
    return lanczos_gegl_resample_ewa (src_buffer,
                                      dst_buffer,
                                      src_width,
                                      src_height,
                                      dst_width,
                                      dst_height,
                                      format_info,
                                      kernel,
                                      progress,
                                      progress_data,
                                      error);

  x_table = lanczos_contrib_table_new (src_width, dst_width, kernel);
  y_table = lanczos_contrib_table_new (src_height, dst_height, kernel);
  if (! x_table || ! y_table)
    {
      set_error (error, "Could not allocate Lanczos contribution tables.");
      goto fail;
    }

  cache_size = MAX (1, y_table->max_taps + 2);
  if (mul_gsize_overflows ((gsize) src_width,
                           (gsize) format_info->channels,
                           &src_row_values) ||
      mul_gsize_overflows ((gsize) dst_width,
                           (gsize) format_info->channels,
                           &dst_row_values) ||
      mul_gsize_overflows (dst_row_values,
                           sizeof (*accum_row),
                           &accum_row_bytes))
    {
      set_error (error, "Image dimensions are too large.");
      goto fail;
    }

  cache = g_try_new0 (RowCacheSlot, (gsize) cache_size);
  tap_rows = g_try_new (const gfloat *, (gsize) y_table->max_taps);
  src_row = g_try_new (gfloat, src_row_values);
  dst_row = g_try_new (gfloat, dst_row_values);
  accum_row = g_try_new (gdouble, dst_row_values);

  if (! cache || ! tap_rows || ! src_row || ! dst_row || ! accum_row)
    {
      set_error (error, "Could not allocate row buffers.");
      goto fail;
    }

  for (gint i = 0; i < cache_size; i++)
    {
      cache[i].row = -1;
      cache[i].pixels = g_try_new (gfloat, dst_row_values);
      if (! cache[i].pixels)
        {
          set_error (error, "Could not allocate row cache.");
          goto fail;
        }
    }

  for (dy = 0; dy < dst_height; dy++)
    {
      const LanczosContribution *y_contrib = &y_table->items[dy];
      gint                       dx;

      for (gint i = 0; i < y_contrib->n; i++)
        {
          RowCacheSlot *slot = find_cache_slot (cache, cache_size,
                                                y_contrib->pixels[i]);

          if (! slot)
            {
              slot = choose_cache_slot (cache, cache_size);
              load_horizontal_row (src_buffer,
                                   src_width,
                                   y_contrib->pixels[i],
                                   format_info,
                                   x_table,
                                   src_row,
                                   slot);
            }

          slot->used_at = use_counter++;
          tap_rows[i] = slot->pixels;
        }

      memset (accum_row, 0, accum_row_bytes);

      for (gint i = 0; i < y_contrib->n; i++)
        {
          const gfloat *row = tap_rows[i];
          gdouble       weight = y_contrib->weights[i];

          for (gsize j = 0; j < dst_row_values; j++)
            accum_row[j] += (gdouble) row[j] * weight;
        }

      for (dx = 0; dx < dst_width; dx++)
        {
          lanczos_resample_store_pixel (accum_row + ((size_t) dx *
                                                     (size_t) channels),
                                        dst_row + ((size_t) dx *
                                                   (size_t) channels),
                                        channels,
                                        alpha_channel);
        }

      gegl_buffer_set (dst_buffer,
                       GEGL_RECTANGLE (0, dy, dst_width, 1),
                       0,
                       format_info->format,
                       dst_row,
                       GEGL_AUTO_ROWSTRIDE);

      if (progress)
        progress ((gdouble) (dy + 1) / (gdouble) dst_height, progress_data);
    }

  if (progress)
    progress (1.0, progress_data);

  gegl_buffer_flush (dst_buffer);

  row_cache_free (cache, cache_size);
  g_free (tap_rows);
  g_free (src_row);
  g_free (dst_row);
  g_free (accum_row);
  lanczos_contrib_table_free (x_table);
  lanczos_contrib_table_free (y_table);

  return TRUE;

fail:
  row_cache_free (cache, cache_size);
  g_free (tap_rows);
  g_free (src_row);
  g_free (dst_row);
  g_free (accum_row);
  lanczos_contrib_table_free (x_table);
  lanczos_contrib_table_free (y_table);

  return FALSE;
}
