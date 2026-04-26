<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Lanczos Scale

`Lanczos Scale` is an external GIMP 3 plug-in that scales image canvases and layer drawables with custom windowed-sinc and EWA resamplers.

## Features

- Registers `plug-in-lanczos-scale` at `Image > Lanczos Scale...`.
- Registers `plug-in-lanczos-scale-layer` at `Layer > Lanczos Scale...`.
- Supports RGB/RGBA and grayscale/gray-alpha drawables.
- Offers Lanczos3, Lanczos2, experimental Kaiser-windowed sinc kernels, and an experimental EWA Jinc filter.
- Defaults to linear-light float processing while preserving the drawable color space.
- Handles alpha by premultiplying before filtering and unpremultiplying after filtering.
- The Image menu operation scales the image canvas, layers, layer masks, channels, and selection to the requested size.
- The Layer menu operation replaces only the selected layer and mask, keeps its center position, and leaves the image canvas and other layers unchanged.
- The PDB procedures still support explicit new-layer, replace, and new-image output modes for scripts. Image replace mode requires the visible-image target and scales the current image; layer replace mode requires one selected layer.

Indexed drawables are intentionally out of scope for this first version.

## Build

Requirements:

- GIMP 3 development files with `gimpui-3.0.pc`.
- GEGL 0.4 and Babl development files.
- Meson, Ninja, `pkg-config` or `pkgconf`, and a C compiler matching the GIMP build.

Configure and build:

```sh
meson setup build
meson compile -C build
meson test -C build
```

By default, Meson builds the unit tests and builds the GIMP plug-in only when `gimpui-3.0` and `gegl-0.4` development files are discoverable. To require the plug-in target and fail if the GIMP development files are missing:

```sh
meson setup build -Dplugin=enabled
```

To run only the standalone resampler tests on a machine without GIMP development files:

```sh
meson setup build -Dplugin=disabled
meson test -C build
```

## Performance Builds

The default build is portable when no CPU baseline is selected. Release builds can opt into a minimum CPU baseline:

```sh
meson setup build-release -Dcpu-baseline=x86-64-v3 --buildtype=release -Db_lto=true
meson compile -C build-release
```

Supported `cpu-baseline` values are:

- `generic`: no extra ISA requirement; this is the safest public Windows release artifact.
- `x86-64-v2`, `x86-64-v3`, `x86-64-v4`: x86_64 feature levels. `x86-64-v3` enables AVX/AVX2/FMA/BMI-class code generation on GCC/Clang-family compilers.
- `armv8-a`, `armv8.2-a`: AArch64 baselines for ARM builds. AArch64 already includes SIMD/NEON as a baseline feature.
- `native`: local-machine tuning only; do not use this for public release artifacts.

For public Windows releases, publish the generic build and an optimized `x86-64-v3` build side by side. A binary built with `x86-64-v3` will not run on older x86_64 CPUs without AVX2/FMA/BMI-class support.

## GitHub Actions

The CI workflow builds and tests the standalone resampler on Linux, macOS, Windows, x86_64, and ARM64 runner families, and builds the full GIMP plug-in on Windows/MSYS2 CLANG64.

Tagging a release with a version tag such as `v1.0` or `v1.2.3` runs the release workflow. It publishes Windows x86_64 plug-in zips for:

- `lanczos-scale-windows-x86_64.zip`: generic build with no extra x86_64 ISA requirement.
- `lanczos-scale-windows-x86_64-v3.zip`: optimized x86-64-v3 build for modern CPUs.

Each release zip includes `INSTALL-WINDOWS.txt`. To install, close GIMP, unzip the package, and copy the contained `lanczos-scale` folder to:

```text
%APPDATA%\GIMP\3.2\plug-ins
```

The final installed path should be:

```text
%APPDATA%\GIMP\3.2\plug-ins\lanczos-scale\lanczos-scale.exe
```

Linux and macOS CI coverage is source/build validation for now. Public plug-in binaries for those platforms should wait until there is a stable, reproducible GIMP 3 SDK/runtime packaging path for each target.

## License

Lanczos Scale is licensed under the GNU General Public License version 3 or later (`GPL-3.0-or-later`). See `LICENSE`.

Install with GIMP's tool when available:

```sh
gimptool-3.0 --install-bin build/src/lanczos-scale
```

On MSYS2 CLANG64 with GIMP 3.2, the tool is typically `gimptool-3.2` and the user-profile install path is:

```text
C:\Users\<you>\AppData\Roaming\GIMP\3.2\plug-ins\lanczos-scale\lanczos-scale.exe
```

If `gimptool-3.2 --install-bin` fails to copy across Windows/MSYS2 path styles, create that directory and copy `build-plugin\lanczos-scale.exe` there manually.

Or install manually so the executable basename matches the containing folder:

```text
<user-gimp-dir>/plug-ins/lanczos-scale/lanczos-scale.exe
```

Meson can also install directly when given a plug-in folder:

```sh
meson setup build -Dplugindir="$HOME/.config/GIMP/3.0/plug-ins/lanczos-scale"
meson install -C build
```

On Unix-like systems, ensure the installed binary is executable.

## Notes

The resampler uses the standard destination-center mapping:

```text
src = (dst + 0.5) * src_size / dst_size - 0.5
```

For separable filters, downscaling widens the support by the inverse scale and evaluates the selected windowed-sinc kernel at `distance * scale` before normalization. The experimental EWA Jinc filter evaluates a 2D elliptical footprint directly. Pixel reads are row-streamed through GEGL with a small row cache instead of allocating a full intermediate image.
