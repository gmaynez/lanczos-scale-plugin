#include "lanczos-resample.h"

#include <math.h>
#include <stdio.h>

static int failures = 0;

static void
expect_near (const char *label,
             float       actual,
             float       expected,
             float       eps)
{
  if (fabsf (actual - expected) > eps)
    {
      fprintf (stderr, "%s: got %.9g, expected %.9g\n",
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
test_identity_rgba (void)
{
  const float src[] =
  {
    0.0f, 0.1f, 0.2f, 1.0f,
    0.3f, 0.4f, 0.5f, 0.5f,
    0.6f, 0.7f, 0.8f, 0.0f,
    0.2f, 0.4f, 0.6f, 1.0f,
    0.8f, 0.4f, 0.2f, 0.7f,
    1.0f, 0.9f, 0.8f, 0.3f,
  };
  float dst[sizeof (src) / sizeof (src[0])] = { 0.0f };
  int   ok;

  ok = lanczos_resample_float (src, 3, 2, 4, 3,
                               dst, 3, 2,
                               LANCZOS_KERNEL_3,
                               NULL, NULL);

  expect_true ("identity resample ok", ok);

  for (size_t i = 0; i < sizeof (src) / sizeof (src[0]); i++)
    expect_near ("identity value", dst[i], src[i], 1.0e-5f);
}

static void
test_gray_downscale (void)
{
  const float src[] =
  {
    0.0f, 0.25f, 0.75f, 1.0f,
  };
  float dst[2] = { 0.0f, 0.0f };
  int   ok;

  ok = lanczos_resample_float (src, 4, 1, 1, -1,
                               dst, 2, 1,
                               LANCZOS_KERNEL_3,
                               NULL, NULL);

  expect_true ("gray downscale ok", ok);
  expect_true ("gray downscale ordered", dst[0] < dst[1]);
  expect_near ("gray downscale low", dst[0], 0.125f, 0.2f);
  expect_near ("gray downscale high", dst[1], 0.875f, 0.2f);
}

static void
test_alpha_premultiply (void)
{
  const float src[] =
  {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 1.0f,
  };
  float dst[4 * 4] = { 0.0f };
  int   ok;

  ok = lanczos_resample_float (src, 2, 1, 4, 3,
                               dst, 4, 1,
                               LANCZOS_KERNEL_3,
                               NULL, NULL);

  expect_true ("alpha resample ok", ok);

  for (int x = 0; x < 4; x++)
    {
      const float *px = dst + x * 4;

      if (px[3] > 1.0e-4f)
        expect_near ("transparent color did not bleed red", px[0], 0.0f, 1.0e-4f);
    }
}

int
main (void)
{
  test_identity_rgba ();
  test_gray_downscale ();
  test_alpha_premultiply ();

  return failures == 0 ? 0 : 1;
}
