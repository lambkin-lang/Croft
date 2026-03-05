# Croft
Lightweight Cross‑Platform Host Architecture & Runtime Library (C11 + pthreads + Wasm/Native)

## Overview

Croft is a portable C11 runtime that can serve as:

- A **Wasm host** executing one Wasm instance per OS thread.
- A **native runtime library** for building small, cross‑platform programs — both GUI and backend‑only (pthreads + filesystem).

See [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) for the full architecture and
[INCREMENTAL_STRATEGY.md](INCREMENTAL_STRATEGY.md) for the tiered growth plan
from the initial seed to the complete host.

## Building

```bash
cmake -B build -DCROFT_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

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

### Build Configurations

```bash
# Backend‑only (threads + filesystem + text — no window or GPU)
cmake -B build -DCROFT_ENABLE_UI=OFF -DCROFT_ENABLE_WASM=OFF

# Full host (all subsystems)
cmake -B build -DCROFT_ENABLE_UI=ON -DCROFT_ENABLE_WASM=ON \
      -DCROFT_ENABLE_AUDIO=ON -DCROFT_ENABLE_ACCESSIBILITY=ON
```

## License

MIT — see [LICENSE](LICENSE).
