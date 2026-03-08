# Incremental Growth Strategy

This document describes how Croft grows from a minimal **seed** into the full
host described in `DEVELOPMENT_PLAN.md`. Each tier is a self‑contained,
testable, and useful layer that later tiers build on.

## Design Principles

1. **Wasm‑shaped APIs everywhere.**
   Every host function uses fixed‑width integers, passes buffers as
   `(ptr, len)` pairs, and returns `int32_t` status codes. This means
   the same signatures work as Wasm imports *or* as plain C calls from
   native code.

2. **No hidden global state.**
   All context flows through explicit structs (`host_thread_ctx`,
   `host_queue`, `text_buffer`, …). A native program can create as many
   or as few of these as it needs.

3. **Artifact selection, not feature gates.**
   Croft emits modular libraries for each subsystem and supported
   implementation variant. Lambkin decides which artifacts to link based
   on program references and constraint solving. Croft's job is to build
   the available pieces, not to decide the final product composition.

4. **Each tier is independently testable.**
   A new contributor can clone the repo, build just tier 0, and run its
   tests without installing GLFW, Skia, or any Wasm runtime.

---

## Tier Map

```
Tier  Name              Depends on   Representative artifacts
────  ────              ──────────   ────────────────────────
 0    Foundation         —            croft_foundation
 1    Messaging          0            croft_wire_runtime + croft_messaging
 2    Filesystem         0            croft_fs
 3    Text Buffer        0            sapling
 4    Wasm Embedding     0–2          croft_wasm_wasm3
 5    Windowing / Input  0            croft_ui_glfw_opengl
 6    Rendering          0, 5         croft_render_tgfx_opengl
 7    Scene Graph        0, 5, 6      croft_scene_core_tgfx_opengl
 7    Text Editor Node   3, 6, 7      croft_scene_text_editor_tgfx_opengl
 8    Audio              0            croft_audio_miniaudio
 9    Accessibility      7            croft_a11y_stub / croft_a11y_macos
 9    Menu / Gestures    5            croft_menu_macos / croft_gesture_macos
 10   Hardening          all          manifests, tests, packaging
```

### What you get at each milestone

| Tiers linked | What you can build |
|---|---|
| 0 | Multi‑threaded native utilities (logging, timing, threads) |
| 0–1 | Actor‑style concurrent services (message queues between threads) |
| 0–2 | File‑processing daemons, CLI tools with I/O |
| 0–3 | Text editors (terminal), language servers, REPL back‑ends |
| 0–4 | Wasm‑driven back‑end services (no window) |
| 0–7 | Full GUI applications with GPU rendering |
| 0–8 | GUI + audio (media apps, games) |
| 0–9 | Fully accessible GUI applications |
| 0–10 | Production‑hardened, ABI‑versioned host |

---

## Tier Details

### Tier 0 — Foundation (the seed)

**Goal:** A working C11 library with platform abstraction, logging,
time, and threading primitives.

| Component | Header | Source | Purpose |
|---|---|---|---|
| Platform detection | `croft/platform.h` | — | OS/compiler macros, fixed‑width types |
| Logging | `croft/host_log.h` | `host_log.c` | `host_log(level, ptr, len)` — Wasm‑shaped |
| Time | `croft/host_time.h` | `host_time.c` | `host_time_millis()` — monotonic clock |
| Threading | `croft/host_thread.h` | `host_thread.c` | `host_thread_t`, mutex, condvar wrappers |

A native program can already use this tier to write a portable
multi‑threaded tool. Because every function signature is Wasm‑import
compatible, the same code compiles unchanged when Wasm support is added
in Tier 4.

**Tests:** thread create/join, mutex lock/unlock, time monotonicity,
log output capture.

### Tier 1 — Messaging

**Goal:** Lock‑free or mutex‑guarded MPSC queues so threads can
exchange byte‑buffer messages through channels.

| Component | Header | Source |
|---|---|---|
| Queue | `croft/host_queue.h` | `host_queue.c` |

Exposes `host_send(channel, ptr, len)` and
`host_recv(channel, out_ptr, max_len)`. Channels are lightweight
integer IDs; the host maps them to queues.

**Tests:** single‑producer/single‑consumer, multi‑producer, back‑
pressure, zero‑length message.

### Tier 2 — Filesystem

**Goal:** Portable file I/O plus per‑OS standard directory helpers.

| Component | Header | Source |
|---|---|---|
| File I/O | `croft/host_fs.h` | `host_fs.c` |

Wraps `fopen`/`fread`/`fwrite`/`fclose` behind Wasm‑shaped
signatures. Adds helpers for config, cache, and resource directories
on each platform.

**Tests:** read/write round‑trip, non‑existent path error, directory
helpers return non‑empty strings.

### Tier 3 — Text Buffer / Datastore
    
**Goal:** Persistent text capability and ordered map (B-Tree/Finger Tree) via the embedded Sapling engine.
    
| Component | Header | Source |
|---|---|---|
| Sapling DB | `sapling/sapling.h` | `sapling.c` |
| Sequence | `sapling/seq.h` | `seq.c` |
| Mutable Text | `sapling/text.h` | `text.c` |
    
Fulfills the persistent text data structure using Sapling's `text.c` (an immutable finger-tree implementation) and `seq.c`, instead of a generic piece table. The `SapMemArena` handles all caching.
    
**Tests:** insert at start/middle/end, delete range, slice round‑trip,
empty buffer edge cases, large‑document stress.

### Tier 4 — Wasm Embedding & Wasm Runtime Event Loop

**Goal:** Embed a Wasm runtime and wire every host function from
Tiers 0–3 as Wasm imports, mediated by the Sapling `runner_v0` loop.

Utilizes the `sapling/src/runner/` infrastructure as the canonical
runtime framework. The `SapRunnerV0Workers` own the thread execution
and message handling context.

Wasm exports and intent executions are called by `sap_runner_v0_worker_tick`.

**Tests:** load a trivial `.wasm` module, call `wasm_tick`, verify
`host_log` output, round‑trip a message through send/recv.

### Tier 5 — Windowing / Input

**Goal:** GLFW window + event loop, input events dispatched to a
callback table (native) or `wasm_handle_event` (Wasm).

**Tests:** window opens and closes cleanly, key/mouse events reach
the callback.

### Tier 6 — Rendering

**Goal:** GPU‑accelerated 2D drawing via tgfx or Skia, including
text rendering (HarfBuzz + FreeType).

**Tests:** render a coloured rectangle, render shaped text, verify
no GPU errors.

### Tier 7 — Scene Graph

**Goal:** Traversable node tree (`scene_node`) with draw, hit‑test,
and accessibility hooks.

**Tests:** build a small tree, verify draw order, hit‑test point
inside/outside nodes.

### Tier 8 — Audio

**Goal:** miniaudio playback (and optional capture).

**Tests:** enumerate devices, play a short sine wave, verify callback
fires.

### Tier 9 — Accessibility

**Goal:** Map scene graph nodes to OS accessibility nodes via
per‑platform C shims.

**Tests:** create an accessibility tree from a scene graph, verify
node roles and labels (macOS or Linux AT‑SPI in CI).

### Tier 10 — Hardening

**Goal:** ABI versioning, message schema validation, fuzz testing,
and packaging.

---

## Mapping to DEVELOPMENT_PLAN Phases

| DEVELOPMENT_PLAN Phase | Tiers |
|---|---|
| Phase 1: Host Skeleton | 0, 5, 6 |
| Phase 2: Wasm on UI Thread | 4 (+ 5, 6) |
| Phase 3: Worker Threads + Messaging | 1 (threading already in 0) |
| Phase 4: Filesystem, Audio, Editor | 2, 3, 8 |
| Phase 5: Accessibility | 9 |
| Phase 6: Hardening | 10 |

The tier order **re‑sequences** the original phases so that the
back‑end runtime (Tiers 0–3) is solid before any GUI or Wasm code is
written. This lets native‑only consumers ship useful programs while
the UI tiers are still in progress.

---

## Build Outputs

```cmake
# Build every Croft artifact whose dependencies are present.
cmake -B build \
      -DCROFT_GLFW_SOURCE_DIR=/path/to/glfw \
      -DCROFT_TGFX_SOURCE_DIR=/path/to/tgfx \
      -DCROFT_WASM3_SOURCE_DIR=/path/to/wasm3 \
      -DCROFT_WABT_SOURCE_DIR=/path/to/wabt \
      -DCROFT_MINIAUDIO_SOURCE_DIR=/path/to/miniaudio
```

The resulting build records the available artifact set in
`build/croft-artifacts.json`. Lambkin can then select a minimal link set
such as:

- `croft_foundation + croft_fs`
- `croft_foundation + croft_wire_runtime + croft_messaging`
- `croft_foundation + croft_fs + sapling`
- `croft_ui_glfw_opengl + croft_render_tgfx_opengl + croft_scene_core_tgfx_opengl`
- `croft_scene_text_editor_tgfx_opengl + sapling + croft_a11y_stub`

Each artifact entry also records a `profile` when the target graph is pinned to
the single-thread or threaded Sapling family, so downstream selection can avoid
mixing incompatible variants.

This keeps Croft modular while preserving the "big analysis, small
binaries" goal: Croft builds the solution space, Lambkin chooses the
smallest correct binary.

---

## Seed Deliverables (this PR)

This PR provides Tier 0 — the foundation — as working code:

- `include/croft/platform.h` — OS/compiler detection, base types
- `include/croft/host_log.h` + `src/host/host_log.c` — logging
- `include/croft/host_time.h` + `src/host/host_time.c` — monotonic time
- `include/croft/host_thread.h` + `src/host/host_thread.c` — thread primitives
- `CMakeLists.txt` — modular artifact graph plus build manifest generation
- `tests/` — test suite for every Tier 0 component
