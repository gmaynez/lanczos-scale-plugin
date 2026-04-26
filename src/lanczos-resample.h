// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LANCZOS_RESAMPLE_H
#define LANCZOS_RESAMPLE_H

#include <stdbool.h>

#ifndef LANCZOS_RESTRICT
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define LANCZOS_RESTRICT restrict
#elif defined(__GNUC__) || defined(__clang__)
#define LANCZOS_RESTRICT __restrict__
#elif defined(_MSC_VER)
#define LANCZOS_RESTRICT __restrict
#else
#define LANCZOS_RESTRICT
#endif
#endif

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

void                  lanczos_resample_horizontal_row (const float           *LANCZOS_RESTRICT src_row,
                                                       float                 *LANCZOS_RESTRICT dst_row,
                                                       int                    channels,
                                                       int                    alpha_channel,
                                                       const LanczosContribTable *LANCZOS_RESTRICT x_table);

void                  lanczos_resample_store_pixel    (const double          *LANCZOS_RESTRICT accum,
                                                       float                 *LANCZOS_RESTRICT dst_pixel,
                                                       int                    channels,
                                                       int                    alpha_channel);

bool                  lanczos_resample_float          (const float           *LANCZOS_RESTRICT src,
                                                       int                    src_width,
                                                       int                    src_height,
                                                       int                    channels,
                                                       int                    alpha_channel,
                                                       float                 *LANCZOS_RESTRICT dst,
                                                       int                    dst_width,
                                                       int                    dst_height,
                                                       LanczosKernel          kernel,
                                                       LanczosProgressFunc    progress,
                                                       void                  *progress_data);

#ifdef __cplusplus
}
#endif

#endif
