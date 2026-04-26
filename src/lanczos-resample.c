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
#define LANCZOS_EWA_JINC_BETA 6.5

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

static double
lanczos_bessel_j1 (double x)
{
  double half_x = x * 0.5;
  double term = half_x;
  double sum = term;

  for (int k = 1; k <= 80; k++)
    {
      term *= -((half_x * half_x) /
                ((double) k * (double) (k + 1)));
      sum += term;

      if (fabs (term) <= fabs (sum) * 1.0e-15)
        break;
    }

  return sum;
}

double
lanczos_jinc (double x)
{
  double ax = fabs (x);
  double z;

  if (ax < 1.0e-12)
    return 1.0;

  z = M_PI * ax;

  return 2.0 * lanczos_bessel_j1 (z) / z;
}

bool
lanczos_kernel_is_valid (LanczosKernel kernel)
{
  return lanczos_kernel_is_separable (kernel) || lanczos_kernel_is_ewa (kernel);
}

bool
lanczos_kernel_is_separable (LanczosKernel kernel)
{
  switch (kernel)
    {
    case LANCZOS_KERNEL_2:
    case LANCZOS_KERNEL_3:
    case LANCZOS_KERNEL_KAISER_3:
    case LANCZOS_KERNEL_KAISER_4:
      return true;

    case LANCZOS_KERNEL_EWA_JINC:
      return false;
    }

  return false;
}

bool
lanczos_kernel_is_ewa (LanczosKernel kernel)
{
  return kernel == LANCZOS_KERNEL_EWA_JINC;
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

    case LANCZOS_KERNEL_EWA_JINC:
      return 3;
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
    case LANCZOS_KERNEL_EWA_JINC:
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

  if (kernel == LANCZOS_KERNEL_EWA_JINC)
    return lanczos_jinc (x) *
           lanczos_kaiser_window (x, radius, LANCZOS_EWA_JINC_BETA);

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
      ! lanczos_kernel_is_separable (kernel))
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
lanczos_ewa_axis_bounds (int     src_size,
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
  double scale = (double) dst_size / (double) src_size;
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
  end = (int) floor (*center + support);

  if (start > end)
    start = end = clamp_int ((int) floor (*center + 0.5), 0, src_size - 1);

  *raw_start = start;
  *raw_end = end;
  *unique_low = clamp_int (start, 0, src_size - 1);
  *unique_high = clamp_int (end, 0, src_size - 1);
}

LanczosEwaAxisTable *
lanczos_ewa_axis_table_new (int           src_size,
                            int           dst_size,
                            LanczosKernel kernel)
{
  LanczosEwaAxisTable *table;
  size_t               items_bytes;
  int                  radius = lanczos_kernel_radius (kernel);

  if (src_size <= 0 || dst_size <= 0 || ! lanczos_kernel_is_ewa (kernel))
    return NULL;

  if (mul_size_overflows ((size_t) dst_size, sizeof (*table->items),
                          &items_bytes))
    return NULL;

  table = (LanczosEwaAxisTable *) calloc (1, sizeof (*table));
  if (! table)
    return NULL;

  table->items = (LanczosEwaAxisItem *) malloc (items_bytes);
  if (! table->items)
    {
      lanczos_ewa_axis_table_free (table);
      return NULL;
    }

  table->src_size = src_size;
  table->dst_size = dst_size;
  table->kernel = kernel;
  table->radius = radius;

  for (int dst = 0; dst < dst_size; dst++)
    {
      LanczosEwaAxisItem *item = &table->items[dst];
      int                 unique_low;
      int                 unique_high;
      int                 taps;

      lanczos_ewa_axis_bounds (src_size, dst_size, dst, radius,
                               &item->raw_start, &item->raw_end,
                               &unique_low, &unique_high,
                               &item->center, &item->filter_scale);

      taps = unique_high - unique_low + 1;
      if (taps < 1)
        taps = 1;

      if (taps > table->max_taps)
        table->max_taps = taps;
    }

  return table;
}

void
lanczos_ewa_axis_table_free (LanczosEwaAxisTable *table)
{
  if (! table)
    return;

  free (table->items);
  free (table);
}

LanczosEwaWeightLut *
lanczos_ewa_weight_lut_new (LanczosKernel kernel,
                            int           size)
{
  LanczosEwaWeightLut *lut;
  double               radius = (double) lanczos_kernel_radius (kernel);
  size_t               weights_bytes;

  if (! lanczos_kernel_is_ewa (kernel) || size < 2)
    return NULL;

  if (mul_size_overflows ((size_t) size, sizeof (*lut->weights),
                          &weights_bytes))
    return NULL;

  lut = (LanczosEwaWeightLut *) calloc (1, sizeof (*lut));
  if (! lut)
    return NULL;

  lut->weights = (double *) malloc (weights_bytes);
  if (! lut->weights)
    {
      lanczos_ewa_weight_lut_free (lut);
      return NULL;
    }

  lut->kernel = kernel;
  lut->size = size;
  lut->radius2 = radius * radius;

  for (int i = 0; i < size; i++)
    {
      double t = (double) i / (double) (size - 1);
      double r = radius * sqrt (t);

      lut->weights[i] = lanczos_kernel_value (r, kernel);
    }

  return lut;
}

void
lanczos_ewa_weight_lut_free (LanczosEwaWeightLut *lut)
{
  if (! lut)
    return;

  free (lut->weights);
  free (lut);
}

double
lanczos_ewa_weight_lut_lookup (const LanczosEwaWeightLut *lut,
                               double                     r2)
{
  double pos;
  int    index;
  double frac;

  if (! lut || r2 >= lut->radius2)
    return 0.0;

  if (r2 <= 0.0)
    return lut->weights[0];

  pos = (r2 / lut->radius2) * (double) (lut->size - 1);
  index = (int) pos;

  if (index >= lut->size - 1)
    return 0.0;

  frac = pos - (double) index;

  return (lut->weights[index] * (1.0 - frac)) +
         (lut->weights[index + 1] * frac);
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

static void
lanczos_ewa_accumulate_pixel (double      *LANCZOS_RESTRICT accum,
                              const float *LANCZOS_RESTRICT src_pixel,
                              int          channels,
                              int          alpha_channel,
                              double       weight)
{
  if (alpha_channel >= 0)
    {
      double alpha = src_pixel[alpha_channel];

      for (int c = 0; c < channels; c++)
        {
          double value = src_pixel[c];

          if (c != alpha_channel)
            value *= alpha;

          accum[c] += value * weight;
        }
    }
  else
    {
      for (int c = 0; c < channels; c++)
        accum[c] += (double) src_pixel[c] * weight;
    }
}

static bool
lanczos_resample_ewa_float (const float         *LANCZOS_RESTRICT src,
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
  LanczosEwaAxisTable *x_table = NULL;
  LanczosEwaAxisTable *y_table = NULL;
  LanczosEwaWeightLut *weight_lut = NULL;
  double              *accum = NULL;
  size_t               accum_bytes;

  if (mul_size_overflows ((size_t) channels, sizeof (*accum), &accum_bytes))
    return false;

  x_table = lanczos_ewa_axis_table_new (src_width, dst_width, kernel);
  y_table = lanczos_ewa_axis_table_new (src_height, dst_height, kernel);
  weight_lut = lanczos_ewa_weight_lut_new (kernel,
                                           LANCZOS_EWA_WEIGHT_LUT_SIZE);
  accum = (double *) malloc (accum_bytes);
  if (! x_table || ! y_table || ! weight_lut || ! accum)
    {
      lanczos_ewa_axis_table_free (x_table);
      lanczos_ewa_axis_table_free (y_table);
      lanczos_ewa_weight_lut_free (weight_lut);
      free (accum);
      return false;
    }

  for (int y = 0; y < dst_height; y++)
    {
      const LanczosEwaAxisItem *y_axis = &y_table->items[y];

      for (int x = 0; x < dst_width; x++)
        {
          const LanczosEwaAxisItem *x_axis = &x_table->items[x];
          float                    *dst_pixel = dst +
                                                (((size_t) y *
                                                  (size_t) dst_width +
                                                  (size_t) x) *
                                                 (size_t) channels);
          double                    weight_sum = 0.0;

          memset (accum, 0, accum_bytes);

          for (int sy = y_axis->raw_start; sy <= y_axis->raw_end; sy++)
            {
              int    src_y = clamp_int (sy, 0, src_height - 1);
              double dy = (y_axis->center - (double) sy) *
                          y_axis->filter_scale;
              double dy2 = dy * dy;

              for (int sx = x_axis->raw_start; sx <= x_axis->raw_end; sx++)
                {
                  int          src_x = clamp_int (sx, 0, src_width - 1);
                  double       dx = (x_axis->center - (double) sx) *
                                    x_axis->filter_scale;
                  double       weight = lanczos_ewa_weight_lut_lookup (weight_lut,
                                                                       (dx * dx) + dy2);
                  const float *src_pixel;

                  if (weight == 0.0)
                    continue;

                  src_pixel = src +
                              (((size_t) src_y * (size_t) src_width +
                                (size_t) src_x) * (size_t) channels);

                  lanczos_ewa_accumulate_pixel (accum, src_pixel,
                                                channels, alpha_channel,
                                                weight);
                  weight_sum += weight;
                }
            }

          if (fabs (weight_sum) <= 1.0e-12)
            {
              int          src_x = clamp_int ((int) floor (x_axis->center + 0.5),
                                              0, src_width - 1);
              int          src_y = clamp_int ((int) floor (y_axis->center + 0.5),
                                              0, src_height - 1);
              const float *src_pixel = src +
                                       (((size_t) src_y * (size_t) src_width +
                                         (size_t) src_x) * (size_t) channels);

              memset (accum, 0, accum_bytes);
              lanczos_ewa_accumulate_pixel (accum, src_pixel,
                                            channels, alpha_channel, 1.0);
            }
          else
            {
              for (int c = 0; c < channels; c++)
                accum[c] /= weight_sum;
            }

          lanczos_resample_store_pixel (accum, dst_pixel,
                                        channels, alpha_channel);
        }

      if (progress)
        progress ((double) (y + 1) / (double) dst_height, progress_data);
    }

  if (progress)
    progress (1.0, progress_data);

  lanczos_ewa_axis_table_free (x_table);
  lanczos_ewa_axis_table_free (y_table);
  lanczos_ewa_weight_lut_free (weight_lut);
  free (accum);

  return true;
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
      alpha_channel < -1 || alpha_channel >= channels ||
      ! lanczos_kernel_is_valid (kernel))
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

  if (lanczos_kernel_is_ewa (kernel))
    return lanczos_resample_ewa_float (src,
                                       src_width, src_height,
                                       channels, alpha_channel,
                                       dst,
                                       dst_width, dst_height,
                                       kernel,
                                       progress, progress_data);

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
