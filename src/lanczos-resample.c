// SPDX-License-Identifier: MPL-2.0

#include "lanczos-resample.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LANCZOS_ALPHA_EPSILON 1.0e-6
#define LANCZOS_KAISER_3_BETA 6.5
#define LANCZOS_KAISER_4_BETA 8.0

static int
clamp_int (int value,
           int low,
           int high)
{
  if (value < low)
    return low;
  if (value > high)
    return high;
  return value;
}

static bool
mul_size_overflows (size_t a,
                    size_t b,
                    size_t *result)
{
  if (a != 0 && b > ((size_t) -1) / a)
    return true;

  *result = a * b;
  return false;
}

static double
clamp_unit_double (double value)
{
  if (value < 0.0)
    return 0.0;
  if (value > 1.0)
    return 1.0;
  return value;
}

double
lanczos_sinc (double x)
{
  if (fabs (x) < 1.0e-12)
    return 1.0;

  return sin (M_PI * x) / (M_PI * x);
}

bool
lanczos_kernel_is_valid (LanczosKernel kernel)
{
  return lanczos_kernel_radius (kernel) > 0;
}

int
lanczos_kernel_radius (LanczosKernel kernel)
{
  switch (kernel)
    {
    case LANCZOS_KERNEL_2:
      return 2;

    case LANCZOS_KERNEL_3:
    case LANCZOS_KERNEL_KAISER_3:
      return 3;

    case LANCZOS_KERNEL_KAISER_4:
      return 4;
    }

  return 0;
}

static double
lanczos_bessel_i0 (double x)
{
  double half_x = x * 0.5;
  double term = 1.0;
  double sum = 1.0;

  for (int k = 1; k <= 50; k++)
    {
      double ratio = half_x / (double) k;

      term *= ratio * ratio;
      sum += term;

      if (term <= sum * 1.0e-15)
        break;
    }

  return sum;
}

static double
lanczos_kaiser_beta (LanczosKernel kernel)
{
  switch (kernel)
    {
    case LANCZOS_KERNEL_KAISER_3:
      return LANCZOS_KAISER_3_BETA;

    case LANCZOS_KERNEL_KAISER_4:
      return LANCZOS_KAISER_4_BETA;

    case LANCZOS_KERNEL_2:
    case LANCZOS_KERNEL_3:
      break;
    }

  return 0.0;
}

static double
lanczos_kaiser_window (double x,
                       double radius,
                       double beta)
{
  double ratio = fabs (x) / radius;
  double shape;

  if (ratio >= 1.0)
    return 0.0;

  shape = sqrt (1.0 - (ratio * ratio));

  return lanczos_bessel_i0 (beta * shape) / lanczos_bessel_i0 (beta);
}

double
lanczos_kernel_value (double        x,
                      LanczosKernel kernel)
{
  double radius = (double) lanczos_kernel_radius (kernel);
  double ax     = fabs (x);
  double beta;

  if (radius <= 0.0 || ax >= radius)
    return 0.0;

  beta = lanczos_kaiser_beta (kernel);
  if (beta > 0.0)
    return lanczos_sinc (x) * lanczos_kaiser_window (x, radius, beta);

  return lanczos_sinc (x) * lanczos_sinc (x / radius);
}

static void
contrib_bounds (int     src_size,
                int     dst_size,
                int     dst_pos,
                int     radius,
                int    *raw_start,
                int    *raw_end,
                int    *unique_low,
                int    *unique_high,
                double *center,
                double *filter_scale)
{
  double scale   = (double) dst_size / (double) src_size;
  double support = (double) radius;
  int    start;
  int    end;

  *center = (((double) dst_pos + 0.5) *
             (double) src_size / (double) dst_size) - 0.5;

  if (scale < 1.0)
    {
      *filter_scale = scale;
      support = (double) radius / scale;
    }
  else
    {
      *filter_scale = 1.0;
    }

  start = (int) ceil (*center - support);
  end   = (int) floor (*center + support);

  if (start > end)
    start = end = clamp_int ((int) floor (*center + 0.5), 0, src_size - 1);

  *raw_start   = start;
  *raw_end     = end;
  *unique_low  = clamp_int (start, 0, src_size - 1);
  *unique_high = clamp_int (end, 0, src_size - 1);
}

LanczosContribTable *
lanczos_contrib_table_new (int           src_size,
                           int           dst_size,
                           LanczosKernel kernel)
{
  LanczosContribTable *table;
  size_t               total_taps = 0;
  int                  max_taps   = 0;
  int                  dst;

  if (src_size <= 0 || dst_size <= 0 ||
      ! lanczos_kernel_is_valid (kernel))
    return NULL;

  table = (LanczosContribTable *) calloc (1, sizeof (*table));
  if (! table)
    return NULL;

  table->src_size = src_size;
  table->dst_size = dst_size;
  table->kernel   = kernel;

  {
    size_t items_bytes;

    if (mul_size_overflows ((size_t) dst_size, sizeof (*table->items),
                            &items_bytes))
      {
        lanczos_contrib_table_free (table);
        return NULL;
      }

    table->items = (LanczosContribution *) calloc (1, items_bytes);
  }

  if (! table->items)
    {
      lanczos_contrib_table_free (table);
      return NULL;
    }

  for (dst = 0; dst < dst_size; dst++)
    {
      double center;
      double filter_scale;
      int    raw_start;
      int    raw_end;
      int    unique_low;
      int    unique_high;
      int    taps;

      contrib_bounds (src_size, dst_size, dst, lanczos_kernel_radius (kernel),
                      &raw_start, &raw_end,
                      &unique_low, &unique_high,
                      &center, &filter_scale);
      (void) raw_start;
      (void) raw_end;
      (void) center;
      (void) filter_scale;

      taps = unique_high - unique_low + 1;
      if (taps < 1)
        taps = 1;

      if ((size_t) taps > ((size_t) -1) - total_taps)
        {
          lanczos_contrib_table_free (table);
          return NULL;
        }

      total_taps += (size_t) taps;
      if (taps > max_taps)
        max_taps = taps;
    }

  {
    size_t pixels_bytes;
    size_t weights_bytes;

    if (mul_size_overflows (total_taps, sizeof (*table->pixels),
                            &pixels_bytes) ||
        mul_size_overflows (total_taps, sizeof (*table->weights),
                            &weights_bytes))
      {
        lanczos_contrib_table_free (table);
        return NULL;
      }

    table->pixels = (int *) malloc (pixels_bytes);
    table->weights = (double *) malloc (weights_bytes);
  }

  if (! table->pixels || ! table->weights)
    {
      lanczos_contrib_table_free (table);
      return NULL;
    }

  table->max_taps = max_taps;

  {
    size_t cursor = 0;

    for (dst = 0; dst < dst_size; dst++)
      {
        LanczosContribution *contrib = &table->items[dst];
        double               center;
        double               filter_scale;
        double               weight_sum = 0.0;
        int                  raw_start;
        int                  raw_end;
        int                  unique_low;
        int                  unique_high;
        int                  taps;
        int                  i;

        contrib_bounds (src_size, dst_size, dst, lanczos_kernel_radius (kernel),
                        &raw_start, &raw_end,
                        &unique_low, &unique_high,
                        &center, &filter_scale);

        taps = unique_high - unique_low + 1;

        contrib->n       = taps;
        contrib->pixels  = table->pixels + cursor;
        contrib->weights = table->weights + cursor;

        for (i = 0; i < taps; i++)
          {
            contrib->pixels[i]  = unique_low + i;
            contrib->weights[i] = 0.0;
          }

        for (i = raw_start; i <= raw_end; i++)
          {
            int    clamped = clamp_int (i, 0, src_size - 1);
            double dist    = center - (double) i;
            double weight  = lanczos_kernel_value (dist * filter_scale,
                                                   kernel);

            contrib->weights[clamped - unique_low] += weight;
            weight_sum += weight;
          }

        if (fabs (weight_sum) <= 1.0e-12)
          {
            contrib->n = 1;
            contrib->pixels[0] = clamp_int ((int) floor (center + 0.5),
                                            0, src_size - 1);
            contrib->weights[0] = 1.0;
          }
        else
          {
            for (i = 0; i < contrib->n; i++)
              contrib->weights[i] /= weight_sum;
          }

        cursor += (size_t) contrib->n;
      }
  }

  return table;
}

void
lanczos_contrib_table_free (LanczosContribTable *table)
{
  if (! table)
    return;

  free (table->weights);
  free (table->pixels);
  free (table->items);
  free (table);
}

static void
lanczos_resample_horizontal_row_generic (const float               *LANCZOS_RESTRICT src_row,
                                         float                     *LANCZOS_RESTRICT dst_row,
                                         int                        channels,
                                         int                        alpha_channel,
                                         const LanczosContribTable *LANCZOS_RESTRICT x_table)
{
  int x;

  for (x = 0; x < x_table->dst_size; x++)
    {
      const LanczosContribution *contrib = &x_table->items[x];
      int                        first = contrib->pixels[0];
      int                        c;

      for (c = 0; c < channels; c++)
        {
          double accum = 0.0;
          int    i;

          for (i = 0; i < contrib->n; i++)
            {
              const float *src_px = src_row + (((size_t) first + (size_t) i) *
                                               (size_t) channels);
              double       value  = src_px[c];

              if (alpha_channel >= 0 && c != alpha_channel)
                value *= src_px[alpha_channel];

              accum += value * contrib->weights[i];
            }

          dst_row[((size_t) x * (size_t) channels) + (size_t) c] = (float) accum;
        }
    }
}

static void
lanczos_resample_horizontal_row_y (const float               *LANCZOS_RESTRICT src_row,
                                   float                     *LANCZOS_RESTRICT dst_row,
                                   const LanczosContribTable *LANCZOS_RESTRICT x_table)
{
  int x;

  for (x = 0; x < x_table->dst_size; x++)
    {
      const LanczosContribution *contrib = &x_table->items[x];
      const double              *weights = contrib->weights;
      const float               *src_px = src_row + (size_t) contrib->pixels[0];
      double                     y = 0.0;
      int                        i;

      for (i = 0; i < contrib->n; i++)
        y += (double) src_px[i] * weights[i];

      dst_row[x] = (float) y;
    }
}

static void
lanczos_resample_horizontal_row_ya (const float               *LANCZOS_RESTRICT src_row,
                                    float                     *LANCZOS_RESTRICT dst_row,
                                    const LanczosContribTable *LANCZOS_RESTRICT x_table)
{
  int x;

  for (x = 0; x < x_table->dst_size; x++)
    {
      const LanczosContribution *contrib = &x_table->items[x];
      const double              *weights = contrib->weights;
      const float               *src_px = src_row + ((size_t) contrib->pixels[0] * 2u);
      double                     y = 0.0;
      double                     a = 0.0;
      int                        i;

      for (i = 0; i < contrib->n; i++)
        {
          double alpha = src_px[1];
          double weight = weights[i];

          y += (double) src_px[0] * alpha * weight;
          a += alpha * weight;
          src_px += 2;
        }

      dst_row[((size_t) x * 2u) + 0u] = (float) y;
      dst_row[((size_t) x * 2u) + 1u] = (float) a;
    }
}

static void
lanczos_resample_horizontal_row_rgb (const float               *LANCZOS_RESTRICT src_row,
                                     float                     *LANCZOS_RESTRICT dst_row,
                                     const LanczosContribTable *LANCZOS_RESTRICT x_table)
{
  int x;

  for (x = 0; x < x_table->dst_size; x++)
    {
      const LanczosContribution *contrib = &x_table->items[x];
      const double              *weights = contrib->weights;
      const float               *src_px = src_row + ((size_t) contrib->pixels[0] * 3u);
      double                     r = 0.0;
      double                     g = 0.0;
      double                     b = 0.0;
      int                        i;

      for (i = 0; i < contrib->n; i++)
        {
          double weight = weights[i];

          r += (double) src_px[0] * weight;
          g += (double) src_px[1] * weight;
          b += (double) src_px[2] * weight;
          src_px += 3;
        }

      dst_row[((size_t) x * 3u) + 0u] = (float) r;
      dst_row[((size_t) x * 3u) + 1u] = (float) g;
      dst_row[((size_t) x * 3u) + 2u] = (float) b;
    }
}

static void
lanczos_resample_horizontal_row_rgba (const float               *LANCZOS_RESTRICT src_row,
                                      float                     *LANCZOS_RESTRICT dst_row,
                                      const LanczosContribTable *LANCZOS_RESTRICT x_table)
{
  int x;

  for (x = 0; x < x_table->dst_size; x++)
    {
      const LanczosContribution *contrib = &x_table->items[x];
      const double              *weights = contrib->weights;
      const float               *src_px = src_row + ((size_t) contrib->pixels[0] * 4u);
      double                     r = 0.0;
      double                     g = 0.0;
      double                     b = 0.0;
      double                     a = 0.0;
      int                        i;

      for (i = 0; i < contrib->n; i++)
        {
          double alpha = src_px[3];
          double weight = weights[i];
          double premul_weight = alpha * weight;

          r += (double) src_px[0] * premul_weight;
          g += (double) src_px[1] * premul_weight;
          b += (double) src_px[2] * premul_weight;
          a += alpha * weight;
          src_px += 4;
        }

      dst_row[((size_t) x * 4u) + 0u] = (float) r;
      dst_row[((size_t) x * 4u) + 1u] = (float) g;
      dst_row[((size_t) x * 4u) + 2u] = (float) b;
      dst_row[((size_t) x * 4u) + 3u] = (float) a;
    }
}

void
lanczos_resample_horizontal_row (const float               *LANCZOS_RESTRICT src_row,
                                 float                     *LANCZOS_RESTRICT dst_row,
                                 int                        channels,
                                 int                        alpha_channel,
                                 const LanczosContribTable *LANCZOS_RESTRICT x_table)
{
  if (channels == 1 && alpha_channel < 0)
    {
      lanczos_resample_horizontal_row_y (src_row, dst_row, x_table);
      return;
    }

  if (channels == 2 && alpha_channel == 1)
    {
      lanczos_resample_horizontal_row_ya (src_row, dst_row, x_table);
      return;
    }

  if (channels == 3 && alpha_channel < 0)
    {
      lanczos_resample_horizontal_row_rgb (src_row, dst_row, x_table);
      return;
    }

  if (channels == 4 && alpha_channel == 3)
    {
      lanczos_resample_horizontal_row_rgba (src_row, dst_row, x_table);
      return;
    }

  lanczos_resample_horizontal_row_generic (src_row, dst_row,
                                           channels, alpha_channel, x_table);
}

void
lanczos_resample_store_pixel (const double *LANCZOS_RESTRICT accum,
                              float        *LANCZOS_RESTRICT dst_pixel,
                              int           channels,
                              int           alpha_channel)
{
  int c;

  if (alpha_channel >= 0)
    {
      double alpha = accum[alpha_channel];

      for (c = 0; c < channels; c++)
        {
          double value;

          if (c == alpha_channel)
            {
              value = clamp_unit_double (alpha);
            }
          else if (alpha > LANCZOS_ALPHA_EPSILON)
            {
              /* Keep straight-alpha output bounded after Lanczos ringing. */
              value = clamp_unit_double (accum[c] / alpha);
            }
          else
            {
              value = 0.0;
            }

          dst_pixel[c] = (float) value;
        }
    }
  else
    {
      for (c = 0; c < channels; c++)
        dst_pixel[c] = (float) accum[c];
    }
}

bool
lanczos_resample_float (const float         *LANCZOS_RESTRICT src,
                        int                  src_width,
                        int                  src_height,
                        int                  channels,
                        int                  alpha_channel,
                        float               *LANCZOS_RESTRICT dst,
                        int                  dst_width,
                        int                  dst_height,
                        LanczosKernel        kernel,
                        LanczosProgressFunc  progress,
                        void                *progress_data)
{
  LanczosContribTable *x_table = NULL;
  LanczosContribTable *y_table = NULL;
  float               *tmp     = NULL;
  double              *accum_row = NULL;
  size_t               tmp_count;
  size_t               tmp_bytes;
  size_t               dst_row_values;
  size_t               accum_row_bytes;
  int                  y;

  if (! src || ! dst ||
      src_width <= 0 || src_height <= 0 ||
      dst_width <= 0 || dst_height <= 0 ||
      channels <= 0 || channels > 16 ||
      alpha_channel < -1 || alpha_channel >= channels)
    return false;

  if (src_width == dst_width && src_height == dst_height)
    {
      size_t count;
      size_t bytes;

      if (mul_size_overflows ((size_t) src_width, (size_t) src_height, &count) ||
          mul_size_overflows (count, (size_t) channels, &count) ||
          mul_size_overflows (count, sizeof (*src), &bytes))
        return false;

      memcpy (dst, src, bytes);

      if (progress)
        progress (1.0, progress_data);

      return true;
    }

  if (mul_size_overflows ((size_t) src_height, (size_t) dst_width, &tmp_count) ||
      mul_size_overflows (tmp_count, (size_t) channels, &tmp_count) ||
      mul_size_overflows (tmp_count, sizeof (*tmp), &tmp_bytes) ||
      mul_size_overflows ((size_t) dst_width, (size_t) channels,
                          &dst_row_values) ||
      mul_size_overflows (dst_row_values, sizeof (*accum_row),
                          &accum_row_bytes))
    return false;

  x_table = lanczos_contrib_table_new (src_width, dst_width, kernel);
  y_table = lanczos_contrib_table_new (src_height, dst_height, kernel);
  tmp = (float *) malloc (tmp_bytes);
  accum_row = (double *) malloc (accum_row_bytes);

  if (! x_table || ! y_table || ! tmp || ! accum_row)
    {
      lanczos_contrib_table_free (x_table);
      lanczos_contrib_table_free (y_table);
      free (tmp);
      free (accum_row);
      return false;
    }

  for (y = 0; y < src_height; y++)
    {
      const float *src_row = src + ((size_t) y *
                                    (size_t) src_width *
                                    (size_t) channels);
      float       *tmp_row = tmp + ((size_t) y *
                                    (size_t) dst_width *
                                    (size_t) channels);

      lanczos_resample_horizontal_row (src_row, tmp_row,
                                       channels, alpha_channel, x_table);

      if (progress)
        progress ((double) (y + 1) /
                  (double) (src_height + dst_height),
                  progress_data);
    }

  for (y = 0; y < dst_height; y++)
    {
      const LanczosContribution *contrib = &y_table->items[y];
      float                     *dst_row = dst + ((size_t) y *
                                                  (size_t) dst_width *
                                                  (size_t) channels);
      int                        x;
      int                        i;

      memset (accum_row, 0, accum_row_bytes);

      for (i = 0; i < contrib->n; i++)
        {
          const float *tmp_row = tmp +
                                 ((size_t) contrib->pixels[i] *
                                  (size_t) dst_width *
                                  (size_t) channels);
          double       weight = contrib->weights[i];
          size_t       j;

          for (j = 0; j < dst_row_values; j++)
            accum_row[j] += (double) tmp_row[j] * weight;
        }

      for (x = 0; x < dst_width; x++)
        {
          lanczos_resample_store_pixel (accum_row + ((size_t) x *
                                                     (size_t) channels),
                                        dst_row + ((size_t) x *
                                                   (size_t) channels),
                                        channels,
                                        alpha_channel);
        }

      if (progress)
        progress ((double) (src_height + y + 1) /
                  (double) (src_height + dst_height),
                  progress_data);
    }

  if (progress)
    progress (1.0, progress_data);

  lanczos_contrib_table_free (x_table);
  lanczos_contrib_table_free (y_table);
  free (tmp);
  free (accum_row);

  return true;
}
