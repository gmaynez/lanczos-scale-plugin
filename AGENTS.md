# Agent Notes

This repository contains `Lanczos Scale`, an external GIMP 3 plug-in written in C.

## Current Scope

- Procedure name: `plug-in-lanczos-scale`.
- Binary name: `lanczos-scale`.
- Menu paths: `Image > Lanczos Scale...` and `Layer > Lanczos Scale...`.
- Supported image types: RGB/RGBA and grayscale/gray-alpha.
- Unsupported for now: indexed drawables, masks, channels, and destructive full layer-stack scaling.
- Interactive output: replace selected layer and resize the image canvas to the requested size.
- PDB output modes: new layer, replace selected layer, or new image.
- Resampling: custom separable Lanczos2/Lanczos3 with premultiplied-alpha filtering.
- Default quality path: linear-light float GEGL formats while preserving the drawable color space.

## Layout

- `src/lanczos-scale.c`: GIMP plug-in registration, dialog, validation, output modes, progress, and GIMP run callback.
- `src/gimp-io.c`, `src/gimp-io.h`: GEGL/GIMP pixel format bridge and row-streamed buffer resampling.
- `src/lanczos-resample.c`, `src/lanczos-resample.h`: standalone Lanczos kernel, contribution tables, and float-row resampling.
- `tests/test-lanczos-kernel.c`: kernel and contribution-table tests.
- `tests/test-resample.c`: standalone resampler behavior tests.
- `meson.build`, `meson_options.txt`: Meson build. The `plugin` feature can be enabled, disabled, or auto-detected.
- `README.md`: user-facing build/install notes.

## Known Local Environment

The working repo is `C:\Dev\lanczos`.

Known-good GIMP development toolchain:

- MSYS2 CLANG64 at `C:\msys64\clang64`.
- GIMP dev/runtime from MSYS2 exposes `gimpui-3.0.pc`, `gegl-0.4.pc`, `gimptool-3.2.exe`, and `gimp-console-3.2.exe`.
- Build directory used for the plug-in: `build-plugin`.

Known-good Windows GIMP runtime:

- Official/per-user GIMP 3.2.4 install:
  `C:\Users\garci\AppData\Local\Programs\GIMP 3\bin`
- Official Windows console:
  `C:\Users\garci\AppData\Local\Programs\GIMP 3\bin\gimp-console-3.2.exe`
- User plug-in install path:
  `C:\Users\garci\AppData\Roaming\GIMP\3.2\plug-ins\lanczos-scale\lanczos-scale.exe`

The MSYS2 build produces a Windows PE x86-64 executable, not a Linux binary. It links against GIMP/GTK/GEGL DLLs that are available in both the MSYS2 runtime and the official GIMP 3.2.4 runtime tested here.

## Build Commands

Use MSYS2 CLANG64 from PowerShell:

```powershell
C:\msys64\usr\bin\bash.exe -lc "export MSYSTEM=CLANG64; export PATH=/clang64/bin:/usr/bin:`$PATH; cd /c/Dev/lanczos && meson compile -C build-plugin"
```

Run tests:

```powershell
C:\msys64\usr\bin\bash.exe -lc "export MSYSTEM=CLANG64; export PATH=/clang64/bin:/usr/bin:`$PATH; cd /c/Dev/lanczos && meson test -C build-plugin"
```

If configuring from scratch:

```powershell
C:\msys64\usr\bin\bash.exe -lc "export MSYSTEM=CLANG64; export PATH=/clang64/bin:/usr/bin:`$PATH; cd /c/Dev/lanczos && meson setup build-plugin -Dplugin=enabled"
```

## Install Command

Manual copy is the most reliable route on this machine. `gimptool-3.2 --install-bin` reported the right destination but failed to copy correctly across Windows/MSYS2 path styles.

```powershell
$target = Join-Path $env:APPDATA 'GIMP\3.2\plug-ins\lanczos-scale'
New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item -LiteralPath 'C:\Dev\lanczos\build-plugin\lanczos-scale.exe' -Destination (Join-Path $target 'lanczos-scale.exe') -Force
```

The folder basename and executable basename must match: `lanczos-scale\lanczos-scale.exe`.

## Verification

After install, run the official Windows GIMP console smoke test:

```powershell
$gimpConsole = Join-Path $env:LOCALAPPDATA 'Programs\GIMP 3\bin\gimp-console-3.2.exe'
$script = '(begin (script-fu-use-v3) (let* ((img (gimp-image-new 4 4 RGB)) (layer (gimp-layer-new img "Base" 4 4 RGBA-IMAGE 100 LAYER-MODE-NORMAL)) (result 0)) (gimp-image-insert-layer img layer 0 0) (gimp-drawable-fill layer FILL-BACKGROUND) (set! result (plug-in-lanczos-scale #:run-mode RUN-NONINTERACTIVE #:image img #:drawables (vector layer) #:target "selected-drawable" #:new-width 8 #:new-height 8 #:kernel "lanczos3" #:output-mode "new-layer" #:linear-light #t #:name "Scaled")) (gimp-image-delete (car result)) (gimp-quit 0)))'
& $gimpConsole --no-interface --batch-interpreter=plug-in-script-fu-eval -b $script
```

Expected output may include only:

```text
GIMP-Warning: Welcome to GIMP 3.2.4!
```

That warning is normal startup noise. Loader errors, GLib criticals, or PDB errors are not expected.

To force GIMP to query plug-ins and inspect registration:

```powershell
$gimpConsole = Join-Path $env:LOCALAPPDATA 'Programs\GIMP 3\bin\gimp-console-3.2.exe'
& $gimpConsole --verbose --no-interface --batch-interpreter=plug-in-script-fu-eval -b '(gimp-quit 0)'
Select-String -Path "$env:APPDATA\GIMP\3.2\pluginrc" -Pattern 'plug-in-lanczos-scale'
```

## UI Notes

The dialog uses `gimp_procedure_dialog_get_coordinates()` for a native GIMP size control. By default, that code path's chain button constrains width and height to the same value. `src/lanczos-scale.c` adds a small aspect-lock layer around the coordinate widget so the chain preserves the current source ratio instead.

Current dialog text intentionally follows GIMP's Scale Image style:

- Section: `Image Size`
- Fields: `_Width`, `_Height`
- Section: `Quality`
- Kernel label: `Interpolation`
- Choices: `Lanczos 3`, `Lanczos 2` in a combo box
- OK button: `_Scale`

## Gotchas

- The repo is not inside `C:\Dev\gimp`; use the local GIMP source only as API/build reference.
- Git metadata writes sometimes require elevated shell access in this sandbox. Source edits do not.
- Avoid committing Meson build directories. `.gitignore` ignores `build/` and `build-*`.
- Temporary GIMP console logs created by elevated processes may need elevated deletion.
- If GIMP GUI reports `0xc0000022`, first verify with the official Windows `gimp-console-3.2.exe`, then check antivirus blocking and stale copied executables.
- For `visible-image` with no selected drawable in Script-Fu, omit the `#:drawables` argument. Passing an empty vector can trigger a GObject critical before the plug-in run callback.

## Reference Files In Local GIMP Source

Useful references under `C:\Dev\gimp`:

- `extensions/goat-exercises/goat-exercise-c.c`: minimal C plug-in structure.
- `plug-ins/common/tile.c`: size dialog, new image/layer returns, GEGL write pattern.
- `plug-ins/common/nl-filter.c`: row-oriented GEGL access and progress patterns.
- `plug-ins/common/hot.c`: whole-region pixel processing and alpha-aware output.
- `plug-ins/common/wavelet-decompose.c`: undo groups, layer insertion, dialog flow.
- `app/plug-in/gimppluginmanager-restore.c`: plug-in discovery rules.
- `libgimp/gimpproceduredialog.c`: procedure dialog helper behavior.
- `libgimpwidgets/gimppropwidgets.c`: coordinate widget/property binding behavior.
- `libgimpwidgets/gimpwidgets.c`: ratio-preserving coordinate widget implementation.
