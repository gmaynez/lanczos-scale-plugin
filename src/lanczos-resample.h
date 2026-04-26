// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LANCZOS_RESAMPLE_H
#define LANCZOS_RESAMPLE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  LANCZOS_KERNEL_2 = 2,
  LANCZOS_KERNEL_3 = 3,
} LanczosKernel;

typedef struct
{
  int     n;
  int    *pixels;
  double *weights;
} LanczosContribution;

typedef struct
{
  int                  src_size;
  int                  dst_size;
  LanczosKernel        kernel;
  int                  max_taps;
  LanczosContribution *items;
  int                 *pixels;
  double              *weights;
} LanczosContribTable;

typedef void (*LanczosProgressFunc) (double fraction,
                                     void  *data);

double                lanczos_sinc                    (double                 x);
double                lanczos_kernel_value            (double                 x,
                                                       LanczosKernel          kernel);

LanczosContribTable * lanczos_contrib_table_new       (int                    src_size,
                                                       int                    dst_size,
                                                       LanczosKernel          kernel);
void                  lanczos_contrib_table_free      (LanczosContribTable   *table);

void                  lanczos_resample_horizontal_row (const float           *src_row,
                                                       float                 *dst_row,
                                                       int                    channels,
                                                       int                    alpha_channel,
                                                       const LanczosContribTable *x_table);

void                  lanczos_resample_store_pixel    (const double          *accum,
                                                       float                 *dst_pixel,
                                                       int                    channels,
                                                       int                    alpha_channel);

bool                  lanczos_resample_float          (const float           *src,
                                                       int                    src_width,
                                                       int                    src_height,
                                                       int                    channels,
                                                       int                    alpha_channel,
                                                       float                 *dst,
                                                       int                    dst_width,
                                                       int                    dst_height,
                                                       LanczosKernel          kernel,
                                                       LanczosProgressFunc    progress,
                                                       void                  *progress_data);

#ifdef __cplusplus
}
#endif

#endif
