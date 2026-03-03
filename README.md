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
