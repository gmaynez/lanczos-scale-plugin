#include "gimp-io.h"

#include <string.h>

typedef struct
{
  int    row;
  guint  used_at;
  float *pixels;
} RowCacheSlot;

static void
set_error (GError      **error,
           const gchar  *message)
{
  if (error)
    g_set_error_literal (error, GIMP_PLUG_IN_ERROR, 0, message);
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
                 int           cache_size,
                 int           row)
{
  int i;

  for (i = 0; i < cache_size; i++)
    {
      if (cache[i].row == row)
        return &cache[i];
    }

  return NULL;
}

static RowCacheSlot *
choose_cache_slot (RowCacheSlot *cache,
                   int           cache_size)
{
  RowCacheSlot *slot = &cache[0];
  int           i;

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
load_horizontal_row (GeglBuffer                *src_buffer,
                     gint                       src_width,
                     gint                       src_row,
                     const LanczosGimpFormat   *format_info,
                     const LanczosContribTable *x_table,
                     float                     *src_pixels,
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
                       void                     *progress_data,
                       GError                  **error)
{
  LanczosContribTable *x_table = NULL;
  LanczosContribTable *y_table = NULL;
  RowCacheSlot        *cache = NULL;
  float               *src_row = NULL;
  float               *dst_row = NULL;
  int                  cache_size = 0;
  int                  dy;
  guint                use_counter = 1;

  g_return_val_if_fail (src_buffer != NULL, FALSE);
  g_return_val_if_fail (dst_buffer != NULL, FALSE);
  g_return_val_if_fail (format_info != NULL, FALSE);

  if (src_width <= 0 || src_height <= 0 ||
      dst_width <= 0 || dst_height <= 0 ||
      format_info->channels <= 0 || format_info->channels > 16 ||
      format_info->alpha_channel < -1 ||
      format_info->alpha_channel >= format_info->channels)
    {
      set_error (error, "Invalid image dimensions or channel count.");
      return FALSE;
    }

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

  x_table = lanczos_contrib_table_new (src_width, dst_width, kernel);
  y_table = lanczos_contrib_table_new (src_height, dst_height, kernel);
  if (! x_table || ! y_table)
    {
      set_error (error, "Could not allocate Lanczos contribution tables.");
      goto fail;
    }

  cache_size = MAX (1, y_table->max_taps + 2);
  cache = g_new0 (RowCacheSlot, cache_size);
  src_row = g_new (float, (size_t) src_width *
                          (size_t) format_info->channels);
  dst_row = g_new (float, (size_t) dst_width *
                          (size_t) format_info->channels);

  for (int i = 0; i < cache_size; i++)
    {
      cache[i].row = -1;
      cache[i].pixels = g_new (float, (size_t) dst_width *
                                      (size_t) format_info->channels);
    }

  for (dy = 0; dy < dst_height; dy++)
    {
      const LanczosContribution *y_contrib = &y_table->items[dy];
      int                        dx;

      for (int i = 0; i < y_contrib->n; i++)
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
        }

      for (dx = 0; dx < dst_width; dx++)
        {
          double accum[16];

          memset (accum, 0, sizeof (accum));

          for (int i = 0; i < y_contrib->n; i++)
            {
              RowCacheSlot *slot = find_cache_slot (cache, cache_size,
                                                    y_contrib->pixels[i]);
              const float  *pixel;

              if (! slot)
                {
                  set_error (error, "Internal row cache miss.");
                  goto fail;
                }

              pixel = slot->pixels + ((size_t) dx *
                                      (size_t) format_info->channels);

              for (int c = 0; c < format_info->channels; c++)
                accum[c] += (double) pixel[c] * y_contrib->weights[i];
            }

          if (format_info->alpha_channel >= 0)
            {
              double alpha = accum[format_info->alpha_channel];

              for (int c = 0; c < format_info->channels; c++)
                {
                  double value = accum[c];

                  if (c != format_info->alpha_channel)
                    value = (alpha > 1.0e-6) ? value / alpha : 0.0;
                  else if (value < 0.0)
                    value = 0.0;
                  else if (value > 1.0)
                    value = 1.0;

                  dst_row[((size_t) dx *
                           (size_t) format_info->channels) + (size_t) c] =
                    (float) value;
                }
            }
          else
            {
              for (int c = 0; c < format_info->channels; c++)
                dst_row[((size_t) dx *
                         (size_t) format_info->channels) + (size_t) c] =
                  (float) accum[c];
            }
        }

      gegl_buffer_set (dst_buffer,
                       GEGL_RECTANGLE (0, dy, dst_width, 1),
                       0,
                       format_info->format,
                       dst_row,
                       GEGL_AUTO_ROWSTRIDE);

      if (progress)
        progress ((double) (dy + 1) / (double) dst_height, progress_data);
    }

  if (progress)
    progress (1.0, progress_data);

  gegl_buffer_flush (dst_buffer);

  if (cache)
    {
      for (int i = 0; i < cache_size; i++)
        g_free (cache[i].pixels);
    }
  g_free (cache);
  g_free (src_row);
  g_free (dst_row);
  lanczos_contrib_table_free (x_table);
  lanczos_contrib_table_free (y_table);

  return TRUE;

fail:
  if (cache)
    {
      for (int i = 0; i < cache_size; i++)
        g_free (cache[i].pixels);
    }
  g_free (cache);
  g_free (src_row);
  g_free (dst_row);
  lanczos_contrib_table_free (x_table);
  lanczos_contrib_table_free (y_table);

  return FALSE;
}
