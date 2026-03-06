# Croft
Lightweight Cross‑Platform Host Architecture & Runtime Library (C11 + pthreads + Wasm/Native)

## Overview

Croft is a portable C11 runtime that can serve as:

- A **Wasm host** executing one Wasm instance per OS thread.
- A **native runtime library** for building small, cross‑platform programs through composable runtime artifacts rather than one forced monolith.

See [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) for the full architecture and
[INCREMENTAL_STRATEGY.md](INCREMENTAL_STRATEGY.md) for the tiered growth plan
from the initial seed to the complete host.

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

```bash
# Supply local source trees for optional subsystems you want Croft to emit.
cmake -B build \
  -DCROFT_GLFW_SOURCE_DIR=/path/to/glfw \
  -DCROFT_TGFX_SOURCE_DIR=/path/to/tgfx \
  -DCROFT_WASM3_SOURCE_DIR=/path/to/wasm3 \
  -DCROFT_WABT_SOURCE_DIR=/path/to/wabt \
  -DCROFT_MINIAUDIO_SOURCE_DIR=/path/to/miniaudio
```

Croft no longer uses `CROFT_ENABLE_*` product flags. Optional subsystems are
built when their dependencies are present, and each subsystem is emitted as a
separate artifact.

### Artifact Examples

Typical targets include:

- `croft_foundation`, `croft_thatch_core`, `croft_wire_runtime`, `croft_messaging`, `croft_fs`
- `sapling`
- `croft_wasm_wasm3`
- `croft_ui_glfw_opengl`
- `croft_render_tgfx_opengl`
- `croft_scene_core_tgfx_opengl`
- `croft_scene_text_editor_tgfx_opengl`
- `croft_audio_miniaudio`
- `croft_a11y_stub`, `croft_a11y_macos`, `croft_menu_macos`, `croft_gesture_macos`

Build specific artifacts directly when needed:

```bash
cmake --build build --target croft_foundation sapling
cmake --build build --target croft_ui_glfw_opengl croft_render_tgfx_opengl
```

## License

MIT — see [LICENSE](LICENSE).
