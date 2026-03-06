# TGFX Size Analysis

This note summarizes the March 6, 2026 investigation of why the macOS Metal
render examples are much larger than the lower-tier Croft examples and than the
equivalent OpenGL render sample.

## Observed Sizes

From the optimized backend-comparison run:

- `example_ui_window_opengl`: `34,920` bytes
- `example_window_menu_opengl`: `52,680` bytes
- `example_render_canvas_opengl`: `1,024,704` bytes
- `example_ui_window_metal`: `34,712` bytes
- `example_window_menu_metal`: `52,536` bytes
- `example_render_canvas_metal`: `7,475,752` bytes
- `example_render_canvas_metal_native`: `88,928` bytes

The UI-only and window+menu programs are effectively the same size on both
paths. The large step is specific to the Metal render path, not to macOS UI
windowing itself. A direct native Metal path lands much closer to the shell-only
examples than to the tgfx Metal renderer.

## Revised Conclusion

Metal itself is not the main size problem. The new direct-Metal prototype shows
that a bespoke native Metal render path can stay around `88.9 KB` in the same
release-oriented benchmark profile even after adding text drawing. The large
cost is specific to tgfx's current Metal implementation strategy.

## Build Settings Verified

The `build-size-opt` configuration already used release-oriented flags for the
Croft binary and for `tgfx` itself:

- `-Os`
- `-DNDEBUG`
- `-g0`
- `-ffunction-sections`
- `-fdata-sections`
- `-flto=thin`
- `-Wl,-dead_strip`
- `-Wl,-dead_strip_dylibs` (macOS)
- `-Wl,-x`

The strip delta on the final executable was negligible, which confirms that
debug information was not the primary problem.

## Main Contributors

The generated link map for the Metal render example showed that the dominant
live payload is the Metal shader translation stack inside `tgfx`, not Croft’s
own wrapper code.

Largest live object contributions included:

- `spirv_msl.cpp.o`: about `910 KB`
- `spirv_glsl.cpp.o`: about `773 KB`
- `Initialize.cpp.o` (glslang): about `366 KB`
- `glslang_tab.cpp.o`: about `264 KB`
- `ParseHelper.cpp.o`: about `252 KB`
- `hlslParseHelper.cpp.o`: about `196 KB`
- `GlslangToSpv.cpp.o`: about `182 KB`
- `MetalShaderModule.mm.o`: about `57 KB`

These come from tgfx’s current Metal shader path:

`GLSL -> SPIR-V via shaderc/glslang -> MSL via SPIRV-Cross`

That logic lives in the upstream file:

- `/Users/mshonle/Projects/Tencent/tgfx/src/gpu/metal/MetalShaderModule.mm`

## What Dead-Stripping Already Removes

The same link map showed that image codec vendors are mostly dead-stripped in
the current examples:

- PNG objects were largely `<<dead>>`
- JPEG objects were largely `<<dead>>`
- WEBP objects were largely `<<dead>>`

So those codecs were not the main reason the final renderer binary landed near
7.5 MB. They still inflate the archive and build time, but not the live binary
nearly as much as the shader compiler/translator stack.

## Changes Applied In Croft

To keep Croft aligned with production/release expectations and trim avoidable
payload around the edges:

1. Release and `MinSizeRel` now default to `-g0`, section-splitting, dead-strip,
   dylib dead-strip on macOS, and symbol stripping in the main Croft CMake
   build.
2. IPO/LTO is enabled for `Release` and `MinSizeRel`.
3. Croft now treats the tgfx GPU backend as an explicit configure-time analysis
   axis. A build may request either `TGFX_USE_OPENGL=ON` or `TGFX_USE_METAL=ON`,
   and the new backend comparison wrapper measures both in separate build
   directories.
4. Croft now disables tgfx PNG/JPEG/WEBP codec options, since the current
   examples do not use those features.

The codec change did not move the final `example_render_canvas_metal` size in a
measurable way: the optimized benchmark stayed at `7,475,752` bytes before and
after the change. That lines up with the link-map evidence showing that those
codec objects were already mostly dead-stripped from the final binary.

Adding `-Wl,-dead_strip_dylibs` to the benchmark link also left the size
unchanged, which is consistent with the remaining payload being live code
inside the linked static tgfx/shader toolchain objects rather than unused dylib
dependencies.

## Remaining Structural Problem

The largest remaining cost is not something the linker can solve by itself.
Once `MetalShaderModule.mm` is linked, the runtime shader translation stack
pulls in large pieces of:

- `shaderc`
- `glslang`
- `SPIRV-Tools`
- `SPIRV-Cross`

The OpenGL-vs-Metal comparison confirms that the heavy payload is coming from
Metal plus tgfx's shared GLSL/program-builder layers, not from Croft's macOS UI
shells. The OpenGL render example lands near `1.0 MB`; the tgfx Metal render
example lands near `7.5 MB`; the native-direct Metal render example lands near
`88.9 KB`.

That is the subsystem boundary that should be isolated next if smaller macOS
GUI binaries are a priority.

## Best Next Step

The clean architectural direction is now two-track:

1. Keep the tgfx/OpenGL and tgfx/Metal examples as comparison controls.
2. Grow the direct-Metal path as its own implementation family, independent of
   tgfx's shader translation stack.

That makes the product-family split explicit instead of pretending every macOS
GPU path must share the same renderer abstraction.
