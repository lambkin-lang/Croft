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
| C11 compiler | Build Croft runtime | Install outside Croft | Clang or GCC is fine. |
| C++17 compiler | Build `host_render.cpp` and `tgfx` | Install outside Croft | Required even if Croft itself is mostly C. |
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
| `miniaudio` | Single-header native audio library | `croft_audio_miniaudio` | Standalone. It is not hiding inside `tgfx`, `makepad`, or the Microsoft editor repos. | Stage the pinned `miniaudio.h` header into `local_deps/src/miniaudio/` with `tools/bootstrap_deps.sh`. A full repo checkout is unnecessary because Croft only includes the header. | `CROFT_MINIAUDIO_SOURCE_DIR`; fallback: `CROFT_FETCH_MINIAUDIO` with `CROFT_MINIAUDIO_GIT_REPOSITORY` and `CROFT_MINIAUDIO_GIT_TAG` | Consult the official `miniaudio` repo/header documentation if you need a local override. The bootstrap uses the raw header URL pinned in `tools/deps.lock.sh`. | Not installed locally before bootstrap. |
| WASI proposals checkout (optional) | Upstream WIT definitions | Current-machine WASI bindings and manifests when you want Croft to generate from upstream definitions instead of the in-tree fixtures | Standalone spec repo checkout. It is not a runtime dependency and Croft still builds without it. | Keep it outside the Croft tree and point CMake at the `proposals/` directory. | `CROFT_WASI_PROPOSALS_DIR` | Useful when validating parser/codegen changes against upstream `wasi:*` packages. If unset, Croft falls back to the curated in-tree WIT fixtures. | Present on this machine at `/Users/mshonle/Projects/WebAssembly/WASI/proposals`. |

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
| WASI spec repo | Standards reference and optional WIT source checkout | Croft can read `proposals/` via `CROFT_WASI_PROPOSALS_DIR`, but the repo is still not a host runtime implementation for Croft | `/Users/mshonle/Projects/WebAssembly/WASI` |
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

Ensure `wat2wasm` is on `PATH` before configuring if Wasm guest tests are expected to build:

```bash
source local_deps/env.sh
```

If you prefer local overrides without running the bootstrap script, pass explicit
source directories for `tgfx`, `wasm3`, and `miniaudio`, and point CMake at the
Homebrew GLFW / WABT installs on this machine.

If you prefer a source checkout for WABT instead of a preinstalled tool, pass
`-DCROFT_WABT_SOURCE_DIR=/path/to/wabt`.

The full current-machine build now emits:

- generated public WIT headers under `build/generated/`
- generated WIT manifests under `build/generated/` and installable copies under `share/croft/wit-manifests/`
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
