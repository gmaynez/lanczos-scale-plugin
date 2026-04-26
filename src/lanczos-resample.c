#include "lanczos-resample.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define LANCZOS_ALPHA_EPSILON 1.0e-6

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

double
lanczos_sinc (double x)
{
  if (fabs (x) < 1.0e-12)
    return 1.0;

  return sin (M_PI * x) / (M_PI * x);
}

double
lanczos_kernel_value (double        x,
                      LanczosKernel kernel)
{
  double radius = (double) kernel;
  double ax     = fabs (x);

  if (ax >= radius)
    return 0.0;

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
      (kernel != LANCZOS_KERNEL_2 && kernel != LANCZOS_KERNEL_3))
    return NULL;

  table = (LanczosContribTable *) calloc (1, sizeof (*table));
  if (! table)
    return NULL;

  table->src_size = src_size;
  table->dst_size = dst_size;
  table->kernel   = kernel;

  table->items = (LanczosContribution *) calloc ((size_t) dst_size,
                                                 sizeof (*table->items));
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

      contrib_bounds (src_size, dst_size, dst, (int) kernel,
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

      total_taps += (size_t) taps;
      if (taps > max_taps)
        max_taps = taps;
    }

  table->pixels = (int *) malloc (total_taps * sizeof (*table->pixels));
  table->weights = (double *) malloc (total_taps * sizeof (*table->weights));
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

        contrib_bounds (src_size, dst_size, dst, (int) kernel,
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

void
lanczos_resample_horizontal_row (const float                *src_row,
                                 float                      *dst_row,
                                 int                         channels,
                                 int                         alpha_channel,
                                 const LanczosContribTable  *x_table)
{
  int x;

  for (x = 0; x < x_table->dst_size; x++)
    {
      const LanczosContribution *contrib = &x_table->items[x];
      int                        c;

      for (c = 0; c < channels; c++)
        {
          double accum = 0.0;
          int    i;

          for (i = 0; i < contrib->n; i++)
            {
              const float *src_px = src_row + ((size_t) contrib->pixels[i] *
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

bool
lanczos_resample_float (const float         *src,
                        int                  src_width,
                        int                  src_height,
                        int                  channels,
                        int                  alpha_channel,
                        float               *dst,
                        int                  dst_width,
                        int                  dst_height,
                        LanczosKernel        kernel,
                        LanczosProgressFunc  progress,
                        void                *progress_data)
{
  LanczosContribTable *x_table = NULL;
  LanczosContribTable *y_table = NULL;
  float               *tmp     = NULL;
  size_t               tmp_count;
  size_t               tmp_bytes;
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
      mul_size_overflows (tmp_count, sizeof (*tmp), &tmp_bytes))
    return false;

  x_table = lanczos_contrib_table_new (src_width, dst_width, kernel);
  y_table = lanczos_contrib_table_new (src_height, dst_height, kernel);
  tmp = (float *) malloc (tmp_bytes);

  if (! x_table || ! y_table || ! tmp)
    {
      lanczos_contrib_table_free (x_table);
      lanczos_contrib_table_free (y_table);
      free (tmp);
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

      for (x = 0; x < dst_width; x++)
        {
          double accum[16];
          int    c;
          int    i;

          memset (accum, 0, sizeof (accum));

          for (i = 0; i < contrib->n; i++)
            {
              const float *tmp_px = tmp +
                                    (((size_t) contrib->pixels[i] *
                                      (size_t) dst_width +
                                      (size_t) x) *
                                     (size_t) channels);

              for (c = 0; c < channels; c++)
                accum[c] += (double) tmp_px[c] * contrib->weights[i];
            }

          if (alpha_channel >= 0)
            {
              double alpha = accum[alpha_channel];

              for (c = 0; c < channels; c++)
                {
                  double value = accum[c];

                  if (c != alpha_channel)
                    value = (alpha > LANCZOS_ALPHA_EPSILON) ?
                            value / alpha : 0.0;
                  else if (value < 0.0)
                    value = 0.0;
                  else if (value > 1.0)
                    value = 1.0;

                  dst_row[((size_t) x * (size_t) channels) + (size_t) c] =
                    (float) value;
                }
            }
          else
            {
              for (c = 0; c < channels; c++)
                dst_row[((size_t) x * (size_t) channels) + (size_t) c] =
                  (float) accum[c];
            }
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

  return true;
}
