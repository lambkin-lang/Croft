# Croft
Lightweight Cross‑Platform Host Architecture & Runtime Library (C11 + pthreads + Wasm/Native)

## Overview

Croft is a portable C11 runtime that can serve as:

- A **Wasm host** executing one Wasm instance per OS thread.
- A **native runtime library** for building small, cross‑platform programs through composable runtime artifacts rather than one forced monolith.

See [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) for the full architecture and
[INCREMENTAL_STRATEGY.md](INCREMENTAL_STRATEGY.md) for the tiered growth plan
from the initial seed to the complete host.

See [docs/BUILD_DEPENDENCY_MATRIX.md](docs/BUILD_DEPENDENCY_MATRIX.md)
for the reproducible external build closure, required checkouts, and
reference repos that are intentionally not part of the build.
See [docs/PRODUCT_FAMILY_PLAN.md](docs/PRODUCT_FAMILY_PLAN.md) for the
current Croft/Lambkin boundary model, WIT plan, and phased implementation
sequence intended to guide future sessions.
See [docs/LAMBKIN_XPI_JOURNAL.md](docs/LAMBKIN_XPI_JOURNAL.md) for the
current join-points, XPI candidates, and AppKit/direct-Metal research notes
surfaced while forcing the design into code.
See [docs/EXAMPLE_MATRIX.md](docs/EXAMPLE_MATRIX.md) for the current example
ladder from foundation-only demos up through the text-editor shell.
See [docs/EDITOR_FAMILY_ANALYSIS.md](docs/EDITOR_FAMILY_ANALYSIS.md) for the
current editor-family comparison on macOS.

## Building

```bash
cmake -B build -DCROFT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Croft now emits a set of static libraries and records them in
`build/croft-artifacts.json`. Lambkin can treat that manifest as the universe of
available mix-ins for constraint solving and final link selection.
Croft also records the current example targets in `build/croft-examples.json`
and `build/croft-example-targets.txt`.

### Logged Local Runs

Capture configure/build/test output to a commit-tagged, timestamped local log:

```bash
tools/run_logged.sh
```

Run any custom command and still log through `tee`:

```bash
tools/run_logged.sh -- ctest --test-dir build -R sapling_test_seq --output-on-failure
```

Review recent log outcomes:

```bash
tools/summarize_logs.sh
```

Notes:
- Logs are written to `local_logs/`.
- Filenames include `git describe --always --dirty --abbrev=7` and `date +%Y-%B-%d-%H%M%S`.
- Older logs are pruned automatically (default keep: 40, override with `CROFT_LOG_KEEP` or `--keep`).

### Example Binary Size Benchmark

Track size of the example set in a release-oriented profile:

```bash
tools/benchmark_binary_size.sh
```

This produces:
- Detailed run log: `local_logs/size_bench/runs/<timestamp>--<git>--<targets>.log`
- Append-only history: `local_logs/size_bench/history.csv`

By default the script:

- Uses `local_deps/croft-deps.cmake` when present, so dependency paths do not need to be re-expressed as environment variables.
- Configures Croft with `CROFT_BUILD_TESTS=OFF`.
- Reads the example target list from `build-size-opt/croft-example-targets.txt`.
- Records the optimized `MinSizeRel` profile with `-g0`, LTO, and dead-strip.

Useful options:

```bash
# Force more aggressive size optimization
tools/benchmark_binary_size.sh --opt-level Oz

# Benchmark one specific example
tools/benchmark_binary_size.sh --target example_render_canvas_metal

# Compare the OpenGL and Metal backend datapoints
bash ./tools/benchmark_tgfx_backends.sh

# Compare the current editor families on macOS
bash ./tools/benchmark_editor_families.sh

# Compare optimized sizes against Debug
tools/benchmark_binary_size.sh --compare-debug

# Show latest benchmark rows
tools/summarize_size_bench.sh --limit 20
```

### Dependency Inputs

The full reproducible dependency matrix, including which repositories are
required checkouts versus architecture references only, is documented in
[docs/BUILD_DEPENDENCY_MATRIX.md](docs/BUILD_DEPENDENCY_MATRIX.md).

For the canonical reproducible path, bootstrap the pinned non-brew dependencies
outside the CMake configure step:

```bash
git -C /Users/mshonle/Projects/Tencent/tgfx status --short --branch
git lfs version
(cd /Users/mshonle/Projects/Tencent/tgfx && bash ./sync_deps.sh)
tools/bootstrap_deps.sh
source local_deps/env.sh
cmake -S . -B build -C local_deps/croft-deps.cmake
```

Croft no longer uses `CROFT_ENABLE_*` product flags. Optional subsystems are
built when their dependencies are present, and each subsystem is emitted as a
separate artifact.

Current dependency policy:

- `glfw` is resolved from `CROFT_GLFW_SOURCE_DIR`, then `CROFT_GLFW_ROOT`, and on this workstation the default installed prefix is `/opt/homebrew/Cellar/glfw/3.4`.
- `tgfx` is consumed from the prepared external checkout pinned in `tools/deps.lock.sh`; the bootstrap script verifies that checkout and writes its path into the generated CMake cache.
- `wasm3` is staged into `local_deps/src/` by the bootstrap workflow.
- `miniaudio` is vendored as the single pinned `miniaudio.h` header into `local_deps/src/miniaudio/`, because Croft only includes the header.
- `wabt` is still treated as an external tool or source checkout because Croft only needs `wat2wasm`.

The bootstrap lock file at `tools/deps.lock.sh` is the preferred place to pin
immutable dependency revisions for reproducible builds.

Direct CMake `FetchContent` remains as a fallback path for `wasm3` and
`miniaudio`, but it is not the recommended reproducible workflow.

### Artifact Examples

Typical targets include:

- `croft_foundation`, `croft_host_log`, `croft_host_time`, `croft_host_thread`
- `croft_msg_frame`, `croft_host_queue`, `croft_messaging`, `croft_fs`
- `sapling_core`, `sapling_runner_core`, `sapling_runner_host`, `sapling_wasi_runtime`, `sapling_wasi_host`, `sapling`
- `croft_wit_common_core`, `croft_wit_text_runtime`
- `croft_wasm_wasm3`
- `croft_ui_glfw_opengl`, `croft_ui_glfw_metal` (macOS)
- `croft_render_tgfx_opengl`, `croft_render_tgfx_metal` (macOS)
- `croft_render_metal_native` (macOS)
- `croft_editor_document_core`, `croft_editor_document_fs`, `croft_editor_document`
- `croft_editor_appkit` (macOS)
- `croft_scene_core_metal_native`, `croft_scene_text_editor_metal_native` (macOS)
- `croft_scene_core_tgfx_opengl`, `croft_scene_core_tgfx_metal` (macOS)
- `croft_scene_text_editor_tgfx_opengl`, `croft_scene_text_editor_tgfx_metal` (macOS)
- `croft_audio_miniaudio`
- `croft_a11y_stub`, `croft_a11y_macos`, `croft_menu_macos`, `croft_gesture_macos`

On macOS, the standalone GPU-based example executables now prefer the Metal
artifacts when those variants are available.

### Example Targets

Examples are first-class sample programs, separate from the unit and
integration tests. They are defined as explicit build targets and can be built
as a group:

```bash
cmake --build build --target croft_examples
```

Representative examples include:

- `example_foundation_threads`
- `example_messaging_roundtrip`
- `example_fs_inspect`
- `example_sapling_text`
- `example_wit_text_handles`
- `example_wasm_guest`
- `example_ui_window_opengl`
- `example_ui_window_metal`
- `example_window_menu_opengl`
- `example_window_menu_metal`
- `example_render_canvas_opengl`
- `example_render_canvas_metal`
- `example_render_canvas_metal_native`
- `example_scene_graph`
- `example_zoom_canvas`
- `example_editor_text`
- `example_editor_text_appkit`
- `example_editor_text_metal_native`
- `example_a11y_tree`
- `example_audio_tone`

The intended ladder is documented in [docs/EXAMPLE_MATRIX.md](docs/EXAMPLE_MATRIX.md).

For backend comparison experiments, Croft currently builds one tgfx GPU backend
variant per configure. Use separate build directories with `TGFX_USE_OPENGL=ON`
or `TGFX_USE_METAL=ON` when comparing render costs; the
`tools/benchmark_tgfx_backends.sh` helper automates that workflow and now also
records a direct-Metal sample that bypasses tgfx entirely.

For common-core/WIT experiments, compare:

- `example_sapling_text` as the direct common-side baseline
- `example_wit_text_handles` as the first handle-oriented WIT/resource model

For editor-family experiments on macOS, Croft currently compares:

- the tgfx/Metal scene editor (`example_editor_text`)
- the native AppKit/TextKit CPU editor (`example_editor_text_appkit`)
- the direct-Metal scene editor (`example_editor_text_metal_native`)

The `tools/benchmark_editor_families.sh` helper automates that comparison.

Build specific artifacts directly when needed:

```bash
cmake --build build --target croft_foundation sapling_core sapling
cmake --build build --target croft_ui_glfw_opengl croft_render_tgfx_opengl
```

## License

MIT — see [LICENSE](LICENSE).
