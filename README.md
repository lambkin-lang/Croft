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

## Building

```bash
cmake -B build -DCROFT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Croft now emits a set of static libraries and records them in
`build/croft-artifacts.json`. Lambkin can treat that manifest as the universe of
available mix-ins for constraint solving and final link selection.

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

### GUI Binary Size Benchmark

Track size of the primary GUI executable over time (default target: `test_editor_standalone`):

```bash
tools/benchmark_binary_size.sh
```

This produces:
- Detailed run log: `local_logs/size_bench/runs/<timestamp>--<git>--<target>.log`
- Append-only history: `local_logs/size_bench/history.csv`

Each run records two profiles:
- `debug` (`Debug`)
- `optimized` (`MinSizeRel` + LTO + dead-strip + `-Os` by default)

Useful options:

```bash
# Force more aggressive size optimization
tools/benchmark_binary_size.sh --opt-level Oz

# Benchmark a different GUI target
tools/benchmark_binary_size.sh --target test_scene_standalone

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

- `croft_foundation`, `croft_thatch_core`, `croft_wire_runtime`, `croft_messaging`, `croft_fs`
- `sapling`
- `croft_wasm_wasm3`
- `croft_ui_glfw_opengl`, `croft_ui_glfw_metal` (macOS)
- `croft_render_tgfx_opengl`, `croft_render_tgfx_metal` (macOS)
- `croft_scene_core_tgfx_opengl`, `croft_scene_core_tgfx_metal` (macOS)
- `croft_scene_text_editor_tgfx_opengl`, `croft_scene_text_editor_tgfx_metal` (macOS)
- `croft_audio_miniaudio`
- `croft_a11y_stub`, `croft_a11y_macos`, `croft_menu_macos`, `croft_gesture_macos`

On macOS, the standalone GPU-based example executables now prefer the Metal
artifacts when those variants are available.

Build specific artifacts directly when needed:

```bash
cmake --build build --target croft_foundation sapling
cmake --build build --target croft_ui_glfw_opengl croft_render_tgfx_opengl
```

## License

MIT — see [LICENSE](LICENSE).
