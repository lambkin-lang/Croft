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

The top-level `Makefile` wraps the same flow and now also has first-class
example validation:

```bash
make test
make test-examples
```

`make test-examples` builds `croft_examples` and smoke-runs the main example
families, including the windowed editor variants, with short auto-close
timeouts where supported.

Profile-targeted slices are available through CTest labels:

```bash
ctest --test-dir build -L sapling-core --output-on-failure
ctest --test-dir build -L sapling-threaded --output-on-failure
ctest --test-dir build -L sapling-runner --output-on-failure
ctest --test-dir build -L sapling-wasi --output-on-failure
```

### Orchestration Runner

The canonical Croft-hosted Wasm orchestration path is now exposed as the
`example_orchestration_bootstrap_runner` sample. After a normal build, run:

```bash
./build/example_orchestration_bootstrap_runner
```

When the bundled JSON orchestration guest was built, that command uses it as
the default bootstrap module. You can also point the runner at any compatible
bootstrap guest explicitly:

```bash
./build/example_orchestration_bootstrap_runner /path/to/bootstrap.wasm
```

That path exercises the WIT control plane in
`schemas/wit/orchestration.wit`, the compiled XPI registry, and the
in-process orchestration runtime described in
`docs/BUILD_DEPENDENCY_MATRIX.md`.

Croft now emits a set of static libraries and records them in
`build/croft-artifacts.json`. Lambkin can treat that manifest as the universe of
available mix-ins for constraint solving and final link selection. Each
artifact entry now also carries a `profile` field when the target graph is
constrained to a specific Sapling profile.
Croft also records the current example targets in `build/croft-examples.json`
and `build/croft-example-targets.txt`.
Generated WIT bindings now also emit rename/trace manifests under
`build/generated/*.manifest` (and `build/tests/generated/*.manifest` for test
fixtures) so package-qualified C names can be traced back to the source WIT
schema.

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

# Compare editor-family runtime on a shared larger document
bash ./tools/benchmark_editor_runtime.sh

# Compare editor-family runtime across several document sizes
bash ./tools/benchmark_editor_runtime_matrix.sh

# Compare runtime/auto-close behavior across example families
bash ./tools/benchmark_runtime_perf.sh

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
- `sapling_core`, `sapling_core_threaded`
- `sapling_runner_core`, `sapling_runner_core_threaded`, `sapling_runner_host`
- `sapling_wasi_runtime`, `sapling_wasi_runtime_threaded`, `sapling_wasi_host`, `sapling`
- `croft_editor_document_core`, `croft_editor_document_fs`, `croft_editor_document` (single-thread profile today)
- `croft_wit_text_runtime`, `croft_wit_store_runtime` (single-thread profile today)
- `croft_wit_common_core`, `croft_wit_mailbox_runtime`
- `croft_wit_text_program`
- `croft_wit_host_fs`, `croft_wit_host_fs_runtime`
- `croft_wit_host_clock`, `croft_wit_host_clock_runtime`
- `croft_wit_host_window`, `croft_wit_host_window_runtime`
- `croft_wit_host_gpu2d`, `croft_wit_host_gpu2d_runtime` (macOS)
- `croft_wit_host_menu`, `croft_wit_host_menu_runtime` (macOS)
- `croft_wit_host_clipboard`, `croft_wit_host_clipboard_runtime` (macOS)
- `croft_wit_host_editor_input`, `croft_wit_host_editor_input_runtime`
- `croft_wit_host_a11y`, `croft_wit_host_a11y_runtime` (macOS)
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

The regular example-validation path is:

```bash
make test-examples
```

Representative examples include:

- `example_foundation_threads`
- `example_messaging_roundtrip`
- `example_fs_inspect`
- `example_wit_fs_read`
- `example_wit_text_cli`
- `example_wit_text_wasm_host`
- `example_wit_clock_now`
- `example_wit_window_events`
- `example_wit_gpu_canvas`
- `example_wit_window_wasm_host`
- `example_sapling_text`
- `example_wit_text_handles`
- `example_wit_text_window`
- `example_wit_textpad_window`
- `example_wit_json_viewer_window`
- `example_wit_json_viewer_wasm_host`
- `example_wit_db_kv`
- `example_wit_mailbox_ping`
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

Two current GUI product lines matter most in that ladder:

- `example_editor_text`, `example_editor_text_appkit`, and
  `example_editor_text_metal_native` are document-centric editor families
- `example_zoom_canvas` is the separate spatial/zoomable workspace probe

The scene-editor family now includes incremental syntax/fold caching, shared
find/replace flows, wrapped-row layout with `Alt+Z` word-wrap toggling,
font-probe-derived line metrics, and marked-text composition preview in the
custom-rendered shells.

The current scene-editor shells no longer own pinch-to-zoom or camera
behavior. Those now belong to the workspace line rather than the editor line.
The workspace-planning questions for that separate line live in
[`docs/SPATIAL_WORKSPACE_QUESTIONS.md`](docs/SPATIAL_WORKSPACE_QUESTIONS.md).

For backend comparison experiments, Croft currently builds one tgfx GPU backend
variant per configure. Use separate build directories with `TGFX_USE_OPENGL=ON`
or `TGFX_USE_METAL=ON` when comparing render costs; the
`tools/benchmark_tgfx_backends.sh` helper automates that workflow and now also
records a direct-Metal sample that bypasses tgfx entirely.

For common-core/WIT experiments, compare:

- `example_sapling_text` as the direct common-side baseline
- `example_wit_text_handles` as the text/resource handle baseline
- `example_wit_db_kv` as the first `db`/`txn` handle-oriented datastore model
- `example_wit_mailbox_ping` as the first common-side mailbox/message-passing model

For the first host mix-in WIT experiment, compare:

- `example_fs_inspect` as the direct host filesystem baseline
- `example_wit_fs_read` as the first WIT/resource wrapper over native `host_fs`
- `example_wit_clock_now` as the first stateless host service mix-in over `host_time`
- `example_wit_window_events` as the first WIT window/resource facade over callback-driven `host_ui`
- `example_wit_gpu_canvas` as the first WIT GPU surface/capability facade over the native direct-Metal host
- `example_editor_text_metal_native` as the first direct-Metal editor whose host control path runs through WIT mix-ins for window, clock, menu, clipboard, editor input, and accessibility

For shared common-side WIT logic across multiple worlds, compare:

- `example_wit_text_cli` as the CLI/file-oriented host shape
- `example_wit_text_wasm_host` as the current Wasm-hosted world shape over `wasm3`
- `example_wit_window_wasm_host` as the first auto-closing window/GPU guest hosted through `wasm3`
- `example_wit_text_window` as the native window/GPU host shape
- `example_wit_textpad_window` as a smaller non-scene textpad that reuses
  WIT window/menu/clipboard/editor-input mix-ins without pulling in the scene
  editor
- `example_wit_json_viewer_window` and `example_wit_json_viewer_wasm_host` as
  the shared Thatch-backed read-only JSON viewer in native and wasm-hosted
  shells

The JSON viewer samples accept either a file path or a startup file-picker
request:

```bash
./build/example_wit_json_viewer_window path/to/sample.json
./build/example_wit_json_viewer_window --open
./build/example_wit_json_viewer_wasm_host --open
```

Current optimized size datapoints for that trio plus the GPU-only host mix-in
sample are:

- `example_wit_text_cli`: `53,416`
- `example_wit_text_wasm_host`: `123,288`
- `example_wit_gpu_canvas`: `90,456`
- `example_wit_text_window`: `109,208`
- `example_wit_text_handles` as the smaller common-core-only baseline

After routing the direct-Metal editor control plane through WIT host mix-ins,
the current optimized size datapoint for `example_editor_text_metal_native` is
`148,944`. That is still small enough to support the product-family thesis, but
it also makes the cost of those explicit host seams measurable instead of
hidden inside one ad hoc shell.

For editor-family experiments on macOS, Croft currently compares:

- the tgfx/Metal scene editor (`example_editor_text`)
- the native AppKit/TextKit CPU editor (`example_editor_text_appkit`)
- the direct-Metal scene editor (`example_editor_text_metal_native`)

The `tools/benchmark_editor_families.sh` helper automates that comparison.
The `tools/benchmark_editor_runtime.sh` helper complements it by timing the
same three editors against one generated benchmark document with shared
auto-close settings.
The direct-Metal family now routes window/menu/clipboard/input/accessibility
through WIT-facing runtimes while leaving rendering direct for now.
The new `tools/benchmark_runtime_perf.sh` helper complements the size benchmark
by timing repeated example runs, recording both runner and sample-reported wall
time when available, and capturing any emitted `frames=` telemetry.
On this macOS host it now handles the windowed GUI families directly too,
including the editor-family trio and the smaller WIT window/textpad samples.

Build specific artifacts directly when needed:

```bash
cmake --build build --target croft_foundation sapling_core sapling
cmake --build build --target croft_ui_glfw_opengl croft_render_tgfx_opengl
```

## License

MIT — see [LICENSE](LICENSE).
