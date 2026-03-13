# Croft Build Dependency Matrix

This document defines the external build closure for Croft. The goal is a reproducible build where:

- Installed prerequisites and pinned external checkouts are all documented.
- CMake is told exactly where local inputs live.

The canonical reproducible path is `tools/bootstrap_deps.sh`, which stages the
non-brew dependencies into `local_deps/` using pinned revisions from
`tools/deps.lock.sh`, then generates `local_deps/croft-deps.cmake` for CMake.

Do not treat `build*/_deps` directories as canonical source locations. Those are generated configure/build outputs.

## Recommended External Layout

The preferred workspace-local layout is produced by `tools/bootstrap_deps.sh`:

```text
<croft-worktree>/local_deps/
  src/
    wasm3/
    miniaudio/
  croft-deps.cmake
  env.sh
```

Croft itself does not require these exact paths, but using the generated
`local_deps/` layout keeps local machines and CI runners aligned. On this
workstation, GLFW is expected from Homebrew at `/opt/homebrew/Cellar/glfw/3.4`.

## Core Toolchain Prerequisites

| Input | Purpose | Canonical acquisition | Notes |
| --- | --- | --- | --- |
| CMake 3.15+ | Top-level configure/build | Install outside Croft | Required by [CMakeLists.txt](../CMakeLists.txt). |
| C11 compiler | Build Croft runtime | Install outside Croft | On this macOS workstation, a fresh configure now prefers Homebrew `llvm@21` automatically when present. |
| C++17 compiler | Build `host_render.cpp` and `tgfx` | Install outside Croft | On this macOS workstation, a fresh configure now prefers Homebrew `llvm@21` automatically when present. |
| pthread-capable system toolchain | `Threads::Threads` | OS toolchain | Required for Croft and Sapling threading. |
| macOS SDK / Xcode Command Line Tools | AppKit-backed artifacts | Install outside Croft | Needed for `croft_a11y_macos`, `croft_menu_macos`, and `croft_gesture_macos`. These artifacts cannot be produced in a Linux Docker image. |

## Actual Build Dependency Closure

The table below covers the external dependencies that affect which Croft artifacts can be emitted.

| Dependency | What it is | Croft artifacts unlocked | Standalone or hidden inside another repo? | Canonical inclusion for Croft | Croft input knob | Breadcrumbs for maintainers | Verified local state |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `glfw` | Cross-platform window, input, and OpenGL-context library | `croft_ui_glfw_opengl` and everything above it | Standalone. It is not provided by `tgfx`, `makepad`, `vscode`, `WASI`, or `wasm-micro-runtime`. | Prefer the installed Homebrew prefix on this workstation. A local source checkout is still supported as an override. | `CROFT_GLFW_SOURCE_DIR`, then `CROFT_GLFW_ROOT` | Consult the official GLFW repo and README for source builds. For this machine, the active installed prefix is `/opt/homebrew/Cellar/glfw/3.4`. | Present on this machine at `/opt/homebrew/Cellar/glfw/3.4`. |
| `tgfx` | Tencent GPU-accelerated 2D graphics library | `croft_render_tgfx_opengl`, `croft_scene_core_tgfx_opengl`, `croft_scene_text_editor_tgfx_opengl` | Standalone repo. It is not a sub-system of `glfw`. | Use a pinned external checkout. Do not rely on generated `_deps/tgfx` build outputs as the source of truth. Croft's bootstrap verifies the pinned checkout instead of recloning it. | `CROFT_TGFX_SOURCE_DIR` | Consult the local `tgfx` checkout README at `/Users/mshonle/Projects/Tencent/tgfx/README.md`. On this machine, prepare it with `git -C /Users/mshonle/Projects/Tencent/tgfx status --short --branch`, `git lfs version`, and `(cd /Users/mshonle/Projects/Tencent/tgfx && bash ./sync_deps.sh)`. | Present at `/Users/mshonle/Projects/Tencent/tgfx`; `sync_deps.sh` completed successfully on 2026-03-06. |
| `wasm3` | Embeddable WebAssembly interpreter | `croft_wasm_wasm3` and host Wasm test coverage | Standalone. It is not part of the `WASI` spec repo and it is not a sub-system of `wasm-micro-runtime`. | Use a pinned local checkout staged by `tools/bootstrap_deps.sh`. Direct `FetchContent` is only a fallback path. | `CROFT_WASM3_SOURCE_DIR`; fallback: `CROFT_FETCH_WASM3` with `CROFT_WASM3_GIT_REPOSITORY` and `CROFT_WASM3_GIT_TAG` | Consult the official `wasm3` repo and README for build instructions if you need a local override. | Not installed locally before bootstrap. |
| `wabt` | WebAssembly Binary Toolkit. Croft currently uses the `wat2wasm` tool, not the library. | Wasm guest test fixture generation when `croft_wasm_wasm3` is enabled | Standalone. It is not part of the `WASI` spec repo. | Prefer an external tool install that puts `wat2wasm` on `PATH`. Use a separate source checkout only if you want Croft to build the tool itself. | `find_program(wat2wasm)` or `CROFT_WABT_SOURCE_DIR` | Consult the WABT README if a source build is needed. For this machine, the verified tool install is under Homebrew. | Present at `/opt/homebrew/Cellar/wabt/1.0.39/bin/wat2wasm` and `/opt/homebrew/opt/wabt`. |
| `llvm@21` | Preferred Clang toolchain on this macOS workstation | Fresh CMake configure for Croft host/native builds and current C-to-Wasm guest builds | Standalone formula. | Let CMake auto-discover `/opt/homebrew/opt/llvm@21` on fresh configure, or override with `CROFT_LLVM_21_ROOT`. | `CROFT_PREFER_HOMEBREW_LLVM_21`, `CROFT_LLVM_21_ROOT` | No user-level `PATH` mutation is required. Existing build directories keep their already-cached compiler until reconfigured cleanly. | Present at `/opt/homebrew/opt/llvm@21`. |
| `lld@21` | Preferred Wasm linker for Croft guest tooling | Current C-to-Wasm guest compilation and other explicit Wasm link steps | Standalone formula, paired with `llvm@21`. | Let CMake auto-discover `/opt/homebrew/opt/lld@21` on fresh configure, or override with `CROFT_LLD_21_ROOT`. | `CROFT_WASM_LD_TOOL`, `CROFT_LLD_21_ROOT` | No user-level `PATH` mutation is required. Croft resolves `wasm-ld` by absolute path for build-scoped use. | Present at `/opt/homebrew/opt/lld@21/bin/wasm-ld`. |
| `miniaudio` | Single-header native audio library | `croft_audio_miniaudio` | Standalone. It is not hiding inside `tgfx`, `makepad`, or the Microsoft editor repos. | Stage the pinned `miniaudio.h` header into `local_deps/src/miniaudio/` with `tools/bootstrap_deps.sh`. A full repo checkout is unnecessary because Croft only includes the header. | `CROFT_MINIAUDIO_SOURCE_DIR`; fallback: `CROFT_FETCH_MINIAUDIO` with `CROFT_MINIAUDIO_GIT_REPOSITORY` and `CROFT_MINIAUDIO_GIT_TAG` | Consult the official `miniaudio` repo/header documentation if you need a local override. The bootstrap uses the raw header URL pinned in `tools/deps.lock.sh`. | Not installed locally before bootstrap. |
| WASI proposals checkout (optional) | Upstream WIT definitions | Validation against an external upstream checkout instead of Croft's vendored `0.2.9` snapshot | Standalone spec repo checkout. It is not a runtime dependency and Croft still builds without it. | Keep it outside the Croft tree and point CMake at the `proposals/` directory. | `CROFT_WASI_PROPOSALS_DIR` | Useful when validating parser/codegen changes against upstream `wasi:*` packages. If unset, Croft uses `vendor/wasi/0.2.9/proposals/` plus any explicit current-machine overlays under `schemas/wit/wasi-current-machine/0.2.9/`. | Present on this machine at `/Users/mshonle/Projects/WebAssembly/WASI/proposals`. |

## Platform-Specific Build Inputs

| Input | What it affects | How it should be acquired | Notes |
| --- | --- | --- | --- |
| AppKit framework | `croft_a11y_macos`, `croft_menu_macos`, `croft_gesture_macos` | Comes from the macOS SDK / Xcode Command Line Tools | No separate repository checkout is needed. These are macOS-only artifacts. |
| OpenGL desktop headers and libs | `glfw` and `tgfx` OpenGL-backed artifacts | Comes from the platform SDK/toolchain | Croft does not manage these separately. |

## Reference Repositories That Are Not Current Build Dependencies

These repositories are useful design references or future integration candidates, but they are not part of Croft's native build closure today.

| Resource | Role in Croft | Why it is not a build dependency today | Verified local path |
| --- | --- | --- | --- |
| VS Code | Editor architecture reference | Used for editor concepts and implementation study, not linked or built by Croft | `/Users/mshonle/Projects/microsoft/vscode` |
| Monaco Editor | Editor-model and view-model reference | Same as above; no JS/TS build from this repo is part of Croft | `/Users/mshonle/Projects/microsoft/monaco-editor` |
| `vscode-textmate` | Future grammar/tokenization reference | Relevant only if Croft later adopts TextMate grammar tooling | `/Users/mshonle/Projects/microsoft/vscode-textmate` |
| `vscode-oniguruma` | Future Oniguruma/WASM grammar support reference | Not part of the current native Croft build | `/Users/mshonle/Projects/microsoft/vscode-oniguruma` |
| Makepad | UI/runtime architecture reference | Design inspiration only; not linked into Croft | `/Users/mshonle/Projects/makepad` |
| WASI spec repo | Standards reference and optional WIT source checkout | Croft can read `proposals/` via `CROFT_WASI_PROPOSALS_DIR`, but the repo is still not a host runtime implementation for Croft and is no longer required for the default build because `0.2.9` is vendored in-tree | `/Users/mshonle/Projects/WebAssembly/WASI` |
| WebAssembly Micro Runtime (WAMR) | Alternative Wasm runtime candidate | Separate runtime with a different embedding model; not a drop-in replacement for the current `wasm3` integration | `/Users/mshonle/Projects/WebAssembly/wasm-micro-runtime` |

## Canonical Configure Patterns

### Base Headless Build

This produces the always-available base artifacts and Sapling without any optional UI/audio/Wasm host backends:

```bash
cmake -S . -B build \
  -DCROFT_BUILD_TESTS=ON
```

Then build the current-machine artifact graph and the dependency reports:

```bash
cmake --build build --target croft_full_machine
```

### Full Optional Build With Explicit External Paths

This is the preferred pattern for local builds on this workstation and in CI:

```bash
git -C /Users/mshonle/Projects/Tencent/tgfx status --short --branch
git lfs version
(cd /Users/mshonle/Projects/Tencent/tgfx && bash ./sync_deps.sh)
tools/bootstrap_deps.sh
source local_deps/env.sh
cmake -S . -B build -C local_deps/croft-deps.cmake \
  -DCROFT_WASI_PROPOSALS_DIR=/Users/mshonle/Projects/WebAssembly/WASI/proposals
```

By default, Croft now generates current-machine WASI bindings from the vendored
`vendor/wasi/0.2.9/proposals/` snapshot. A smaller overlay under
`schemas/wit/wasi-current-machine/0.2.9/` is used only where Croft's host
implementation intentionally narrows the raw upstream world surface.

Ensure `wat2wasm` is on `PATH` before configuring if WAT guest tests are expected to build:

```bash
source local_deps/env.sh
```

For the Homebrew LLVM/LLD toolchain on macOS, no shell-level `PATH` export is needed. A fresh configure will auto-discover:

- `/opt/homebrew/opt/llvm@21/bin/clang`
- `/opt/homebrew/opt/llvm@21/bin/clang++`
- `/opt/homebrew/opt/lld@21/bin/wasm-ld`

That toolchain is now used directly by the full current-machine build for the generated C-based `wit_world_bridge_guest.wasm` test fixture; no user shell configuration is required.

If those are installed elsewhere, point CMake at them explicitly:

```bash
cmake -S . -B build \
  -DCROFT_LLVM_21_ROOT=/custom/prefix/llvm@21 \
  -DCROFT_LLD_21_ROOT=/custom/prefix/lld@21
```

If you prefer local overrides without running the bootstrap script, pass explicit
source directories for `tgfx`, `wasm3`, and `miniaudio`, and point CMake at the
Homebrew GLFW / WABT installs on this machine.

If you prefer a source checkout for WABT instead of a preinstalled tool, pass
`-DCROFT_WABT_SOURCE_DIR=/path/to/wabt`.

The full current-machine build now emits:

- generated public WIT headers under `build/generated/`
- generated WIT manifests under `build/generated/` and installable copies under `share/croft/wit-manifests/`
- artifact and XPI metadata under `build/croft-artifacts.json` and `build/croft-xpi.json`
- native current-machine WASI host runtime glue via `croft_wit_wasi_machine_runtime`
- dependency audit reports under `build/reports/`

For the new current-machine WASI host layer, the libc/POSIX boundary is now
intentional and audited rather than implicit. On macOS/Unix that runtime uses
process/environment APIs (`environ`, `getcwd`), time APIs
(`clock_gettime`, `clock_getres`, `localtime_r`, `gmtime_r`, `tzset`), and
system randomness (`arc4random_buf` on macOS, `getrandom` on Linux).

The current-machine filesystem tranche extends that audited boundary to the
directory-descriptor and `*at` POSIX surface used by WASI `filesystem` and
`preopens`: `openat`, `fstatat`, `mkdirat`, `unlinkat`, `renameat`,
`symlinkat`, `readlinkat`, `linkat`, `pread`/`pwrite`, `fdopendir`/`readdir`,
and sync/timestamp calls such as `fsync`, `fdatasync`, `futimens`, and
`utimensat`. That implementation is intentionally Unix/macOS-specific today;
Windows-targeted host glue remains future work.

The current-machine CLI stdio/terminal tranche now makes the process-descriptor
boundary explicit too. `croft_wit_wasi_machine_runtime` can bind inherited or
caller-provided `stdin`/`stdout`/`stderr` through duplicated file descriptors,
and the dependency audit now tracks the extra Unix/macOS surface this uses:
`dup`, `pipe`, and `isatty`. That runtime layer deliberately treats CLI stdio
as a capability view over the shared stream substrate rather than as a special
filesystem path.

`croft-xpi.json` is now the build's machine-readable summary of that structure.
It records:

- a top-level `context` block with the current build system and derived
  current-machine applicability traits, so solver consumers do not have to
  guess how `current-machine-*` labels map onto the machine that produced the
  build
- XPI-participating artifact projections, so the solver can see which build
  units realize which bundle memberships and shared substrates without joining
  a second file first
- per-node `applicability_traits` alongside the human-facing applicability
  string, so bundle selection can operate on explicit machine traits instead
  of opaque labels
- XPI entrypoints/examples with explicit required bundle sets, so top-level
  editor and WIT shells show up as consumers of the same capability graph
- entrypoint bundle requirements are now derived from the registered artifacts
  they explicitly require, while artifact-to-artifact requirement propagation
  follows dependency `requires_bundles` rather than every capability a lower
  provider happens to participate in
- aggregate artifacts may also derive requirements from an explicit provider
  stack when they are intentionally packaging those lower capabilities as one
  higher-level unit; that keeps `croft`, editor document layers, and scene/app
  shells honest in the XPI graph without forcing ordinary dependency edges to
  inherit every lower bundle automatically
- bundle specs now also carry relationship metadata of their own:
  `requires_bundles` for hard companion capabilities, `compatible_bundles` for
  known-good co-composition edges, and `conflicts_with` for future competing
  variants when those appear
- those bundle-level `requires_bundles` are expanded transitively into XPI
  entrypoints and bundle-backed runtime artifacts at configure time, so the
  graph reflects actual composition constraints instead of repeating them
  ad hoc in every top-level target
- top-level examples can now opt out of deriving bundle requirements from every
  required artifact when a multipurpose provider is in play; that keeps
  window-only demos from accidentally looking clipboard-aware just because the
  shared GLFW provider also exposes clipboard support
- higher-level render and editor-shell alternatives are now modeled as their
  own bundles with explicit slot membership and derived `conflicts_with`, so
  solver-visible conflicts are grounded in real competing backends rather than
  placeholder metadata
- those alternatives are now also projected into explicit XPI `slots`, so the
  graph can say "pick one render backend" or "pick one editor shell" directly
  instead of reconstructing that family from repeated pairwise conflicts
- XPI artifacts and entrypoints now distinguish concrete slot selections from
  unsolved choice sites: `selected_slot_bindings` records which slot-bearing
  bundles a concrete build artifact or example already chose, while
  `open_slots` records slot families that an aggregate or solver-facing
  entrypoint intentionally leaves open for Lambkin to fill
- solver-facing family entrypoints can now appear beside concrete examples in
  the same graph, for example a current-machine render-canvas family that
  leaves the render-backend slot open and a file-backed text-editor family
  that leaves the editor-shell slot open
- shared substrates such as byte streams, descriptor tables, pollables, system random, and time-base
- current-machine capability bundles such as CLI stdio/terminal, random, clocks/poll, and filesystem/streams
- Croft host mix-in bundles and substrates for filesystem, clock, window, clipboard, popup-menu, menu-bar, editor-input normalization, GPU surface access, and accessibility where the current machine provides them
- native provider artifacts and seams such as file-dialog, gestures, GLFW window backends, and AppKit menu/a11y providers where they participate in that same graph
- the declared worlds versus expanded callable surfaces for those bundles
- helper-interface couplings such as `wasi:io/error` and `wasi:filesystem/error`

The repo now carries a canonical WIT orchestration path for Croft-hosted Wasm:

- `schemas/wit/orchestration.wit` is the control-plane contract for
  `builder`, `session`, `manifest`, `plan`, DB schema/table declarations,
  mailbox topology, and worker startup payloads
- `src/runtime/xpi_registry.c` and `include/croft/xpi_registry.h` expose a
  compiled XPI registry derived from the same bundle/substrate/slot/artifact
  graph that still renders `build/croft-xpi.json` for diagnostics
- `src/runtime/orchestration_runtime.c` and
  `include/croft/orchestration_runtime.h` are the in-process heuristic
  resolver and session manager: they validate family/applicability, expand
  bundle requirements, honor slot preferences, launch one shared host-owned
  Sapling DB, and bind declared mailboxes and worker policies
- `tests/wasm_guests/orchestration_bootstrap_toy.c` /
  `tests/wasm_guests/orchestration_worker_toy.c` are the minimal mailbox-topology
  demo over real host threads
- `tests/wasm_guests/orchestration_bootstrap_json.c` /
  `tests/wasm_guests/orchestration_worker_json.c` are the guest-local
  JSON-to-Thatch demo, where the worker parses JSON into a local Thatch tree
  and only writes serialized view artifacts back to the shared host DB
- `tests/wasm_guests/orchestration_bootstrap_fail.c` /
  `tests/wasm_guests/orchestration_worker_fail.c` are the deterministic
  failure-path pair that proves worker errors surface as failed sessions
- `example_orchestration_bootstrap_runner` is the small executable entrypoint
  on top of that runtime, and the orchestration smokes now validate the same
  path instead of reconstructing the flow from JSON manifests and solved plans,
  including both successful and failed worker lifecycles

That path is intentionally still heuristic, but it proves the current XPI
graph is already sufficient for an early Lambkin-style "pick a family, satisfy
the hard requirements, choose one bundle for each open slot, then execute"
loop. The durable boundary is now WIT plus the compiled registry/runtime path,
while `croft-xpi.json` remains an exported diagnostic and solver artifact.

`build/reports/croft-wasi-vendor-drift.txt` and
`build/reports/croft-wasi-vendor-drift.json` now complement that metadata by
recording:

- which upstream WASI packages are vendored under `vendor/wasi/0.2.9/proposals/`
- which packages are overlaid under `schemas/wit/wasi-current-machine/0.2.9/`
- per-package `.wit` file counts for the vendored snapshot and overlays
- and, when `CROFT_WASI_PROPOSALS_DIR` is set, whether the vendored snapshot
  still matches the external upstream checkout or has drifted

## Reproducibility Note On FetchContent Pins

The bootstrap workflow avoids that problem by pinning immutable SHAs in
`tools/deps.lock.sh` and staging local checkouts before the main Croft
configure step.

Direct `FetchContent` remains in `CMakeLists.txt` as a fallback for
`wasm3` and `miniaudio`, but it is not the canonical reproducible path.

## CI and Docker Guidance

- A Linux Docker image can reproduce the headless Croft runtime, Sapling, `glfw`, `tgfx`, `wasm3`, `wabt`, and `miniaudio` parts of the build if the toolchain is installed and the bootstrap script stages the pinned external inputs.
- A Linux Docker image cannot build the AppKit-backed macOS artifacts. Those require a macOS runner.
- `tgfx` should be checked out separately at the pinned revision and prepared before the Croft configure step. On macOS that means a clean checkout, `git lfs`, and `bash ./sync_deps.sh` in the `tgfx` repo.
- `wabt` is best treated as a tool prerequisite, not as a library that Croft owns.

## Current Workstation Inventory

Verified in this workspace on 2026-03-06:

- Present:
  - `glfw` install at `/opt/homebrew/Cellar/glfw/3.4`
  - `tgfx` checkout at `/Users/mshonle/Projects/Tencent/tgfx`
  - `wabt` tools at `/opt/homebrew/Cellar/wabt/1.0.39/bin/`
  - Reference repos listed above
- Staged by bootstrap:
  - `wasm3`
  - `miniaudio`
