// SPDX-License-Identifier: GPL-3.0-or-later

#include "lanczos-resample.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void
expect_near (const char *label,
             double      actual,
             double      expected,
             double      eps)
{
  if (fabs (actual - expected) > eps)
    {
      fprintf (stderr, "%s: got %.17g, expected %.17g\n",
               label, actual, expected);
      failures++;
    }
}

static void
expect_true (const char *label,
             int         condition)
{
  if (! condition)
    {
      fprintf (stderr, "%s failed\n", label);
      failures++;
    }
}

static void
expect_table_normalized (const char    *label,
                         int            src_size,
                         int            dst_size,
                         LanczosKernel  kernel)
{
  LanczosContribTable *table;

  table = lanczos_contrib_table_new (src_size, dst_size, kernel);
  expect_true (label, table != NULL);

  if (! table)
    return;

  for (int i = 0; i < table->dst_size; i++)
    {
      double sum = 0.0;

      expect_true ("tap count positive", table->items[i].n > 0);

      for (int j = 0; j < table->items[i].n; j++)
        {
          expect_true ("pixel in range",
                       table->items[i].pixels[j] >= 0 &&
                       table->items[i].pixels[j] < table->src_size);

          if (j > 0)
            expect_true ("pixels are contiguous",
                         table->items[i].pixels[j] ==
                         table->items[i].pixels[j - 1] + 1);

          expect_true ("weight is finite",
                       isfinite (table->items[i].weights[j]));

          sum += table->items[i].weights[j];
        }

      expect_near ("weight sum", sum, 1.0, 1.0e-10);
    }

  lanczos_contrib_table_free (table);
}

int
main (void)
{
  LanczosContribTable *table;

  expect_near ("sinc zero", lanczos_sinc (0.0), 1.0, 1.0e-12);
  expect_near ("sinc integer", lanczos_sinc (1.0), 0.0, 1.0e-12);
  expect_near ("kernel center", lanczos_kernel_value (0.0, LANCZOS_KERNEL_3), 1.0, 1.0e-12);
  expect_near ("kernel cutoff", lanczos_kernel_value (3.0, LANCZOS_KERNEL_3), 0.0, 1.0e-12);
  expect_near ("kernel outside", lanczos_kernel_value (3.1, LANCZOS_KERNEL_3), 0.0, 1.0e-12);
  expect_near ("lanczos2 cutoff", lanczos_kernel_value (2.0, LANCZOS_KERNEL_2), 0.0, 1.0e-12);

  table = lanczos_contrib_table_new (7, 11, LANCZOS_KERNEL_3);
  expect_true ("table allocated", table != NULL);

  if (table)
    {
      for (int i = 0; i < table->dst_size; i++)
        {
          double sum = 0.0;

          for (int j = 0; j < table->items[i].n; j++)
            {
              expect_true ("pixel in range",
                           table->items[i].pixels[j] >= 0 &&
                           table->items[i].pixels[j] < table->src_size);
              sum += table->items[i].weights[j];
            }

          expect_near ("weight sum", sum, 1.0, 1.0e-10);
        }

      lanczos_contrib_table_free (table);
    }

  expect_table_normalized ("lanczos3 upscale table", 9, 17, LANCZOS_KERNEL_3);
  expect_table_normalized ("lanczos3 downscale table", 41, 9, LANCZOS_KERNEL_3);
  expect_table_normalized ("lanczos2 upscale table", 9, 17, LANCZOS_KERNEL_2);
  expect_table_normalized ("lanczos2 downscale table", 41, 9, LANCZOS_KERNEL_2);

  table = lanczos_contrib_table_new (100, 10, LANCZOS_KERNEL_3);
  expect_true ("downscale support expands", table != NULL && table->max_taps >= 55);
  lanczos_contrib_table_free (table);

  expect_true ("bad source size rejected",
               lanczos_contrib_table_new (0, 1, LANCZOS_KERNEL_3) == NULL);
  expect_true ("bad kernel rejected",
               lanczos_contrib_table_new (4, 4, (LanczosKernel) 4) == NULL);

  return failures == 0 ? 0 : 1;
}
