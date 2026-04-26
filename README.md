# Lanczos Scale

`Lanczos Scale` is an external GIMP 3 plug-in that scales the selected layer drawable in place with a custom separable Lanczos resampler.

## Features

- Registers `plug-in-lanczos-scale` at `Image > Lanczos Scale...` and `Layer > Lanczos Scale...`.
- Supports RGB/RGBA and grayscale/gray-alpha drawables.
- Offers Lanczos3 and Lanczos2 kernels.
- Defaults to linear-light float processing while preserving the drawable color space.
- Handles alpha by premultiplying before filtering and unpremultiplying after filtering.
- The interactive dialog replaces the selected layer and resizes the image canvas to the requested size.
- The PDB procedure still supports explicit new-layer, replace-drawable, and new-image output modes for scripts.

Indexed drawables, masks, channels, and full layer-stack scaling are intentionally out of scope for this first version.

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

For downscaling, the support is widened by the inverse scale and the Lanczos kernel is evaluated at `distance * scale` before normalization. Pixel reads are row-streamed through GEGL with a small horizontal row cache instead of allocating a full intermediate image.
