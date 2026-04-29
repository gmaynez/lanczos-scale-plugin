// SPDX-License-Identifier: MPL-2.0

#ifndef LANCZOS_RESAMPLE_H
#define LANCZOS_RESAMPLE_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef LANCZOS_RESTRICT
#ifdef __cplusplus
#define LANCZOS_RESTRICT
#else
#define LANCZOS_RESTRICT restrict
#endif
#endif

#define LANCZOS_MAX_CHANNELS 16

static_assert(LANCZOS_MAX_CHANNELS > 0 && LANCZOS_MAX_CHANNELS <= 64,
              "LANCZOS_MAX_CHANNELS must be positive and reasonable");

#define LANCZOS_EWA_WEIGHT_LUT_SIZE 8192

static_assert(LANCZOS_EWA_WEIGHT_LUT_SIZE >= 2,
              "EWA weight LUT must have at least 2 entries");

static_assert(sizeof (float) == 4, "float must be 4 bytes");
static_assert(sizeof (double) == 8, "double must be 8 bytes");
static_assert(sizeof (size_t) >= 4, "size_t must be at least 32 bits");

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  LANCZOS_KERNEL_2        = 2,
  LANCZOS_KERNEL_3        = 3,
  LANCZOS_KERNEL_KAISER_3 = 103,
  LANCZOS_KERNEL_KAISER_4 = 104,
  LANCZOS_KERNEL_EWA_JINC = 203,
} LanczosKernel;

static_assert(LANCZOS_KERNEL_2 == 2, "kernel enum values must match PDB IDs");
static_assert(LANCZOS_KERNEL_3 == 3, "kernel enum values must match PDB IDs");
static_assert(LANCZOS_KERNEL_KAISER_3 == 103, "kernel enum values must match PDB IDs");
static_assert(LANCZOS_KERNEL_KAISER_4 == 104, "kernel enum values must match PDB IDs");
static_assert(LANCZOS_KERNEL_EWA_JINC == 203, "kernel enum values must match PDB IDs");

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

typedef struct
{
  int    raw_start;
  int    raw_end;
  double center;
  double filter_scale;
} LanczosEwaAxisItem;

typedef struct
{
  int                 src_size;
  int                 dst_size;
  LanczosKernel       kernel;
  int                 radius;
  int                 max_taps;
  LanczosEwaAxisItem *items;
} LanczosEwaAxisTable;

typedef struct
{
  LanczosKernel kernel;
  int           size;
  double        radius2;
  double       *weights;
} LanczosEwaWeightLut;

typedef void (*LanczosProgressFunc) (double fraction,
                                     void  *data);

double                lanczos_sinc                    (double                 x);
double                lanczos_jinc                    (double                 x);
bool                  lanczos_kernel_is_valid         (LanczosKernel          kernel);
bool                  lanczos_kernel_is_separable     (LanczosKernel          kernel);
bool                  lanczos_kernel_is_ewa           (LanczosKernel          kernel);
int                   lanczos_kernel_radius           (LanczosKernel          kernel);
double                lanczos_kernel_value            (double                 x,
                                                       LanczosKernel          kernel);

LanczosContribTable * lanczos_contrib_table_new       (int                    src_size,
                                                       int                    dst_size,
                                                       LanczosKernel          kernel);
void                  lanczos_contrib_table_free      (LanczosContribTable   *table);

LanczosEwaAxisTable * lanczos_ewa_axis_table_new      (int                    src_size,
                                                       int                    dst_size,
                                                       LanczosKernel          kernel);
void                  lanczos_ewa_axis_table_free     (LanczosEwaAxisTable   *table);

LanczosEwaWeightLut * lanczos_ewa_weight_lut_new      (LanczosKernel          kernel,
                                                       int                    size);
void                  lanczos_ewa_weight_lut_free     (LanczosEwaWeightLut   *lut);
double                lanczos_ewa_weight_lut_lookup   (const LanczosEwaWeightLut *lut,
                                                       double                 r2);

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
