# Copilot Instructions for Croft

## Project Overview

Croft is a lightweight, cross-platform host runtime written in **portable C11**. It uses pthreads, GLFW, tgfx/Skia, miniaudio, and an embedded Wasm runtime. The host executes one Wasm instance per OS thread with strict isolation and host-mediated messaging.

Croft also serves as the **runtime library for a new programming language**, producing small native binaries. The Wasm-style isolation model (per-thread instances, host-mediated messaging, no shared memory) defines the conceptual architecture regardless of whether modules are compiled to Wasm or to native code.

Target platforms: macOS, Windows, Linux.

## Tech Stack

- **Language**: C11-first, with isolated C++/ObjC bridge layers where platform/runtime dependencies require them.
- **Threading**: pthreads (Win32 threads with a pthread-compatible wrapper on Windows)
- **Windowing/Input**: GLFW
- **Rendering**: tgfx or Skia (GPU-accelerated 2D)
- **Text shaping**: HarfBuzz + FreeType + native APIs
- **Audio**: miniaudio
- **Wasm runtime**: embedded (one instance per thread)
- **Build system**: CMake or Meson

## Build and Test

```sh
# Configure (CMake example)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run tests
ctest --test-dir build
```

Ensure the compiler supports C11 and pthreads. Enable LTO and dead-code elimination for release builds.

## Coding Conventions

- Keep core runtime APIs in portable C11.
- C++/ObjC is acceptable only in clearly isolated integration boundaries (for example platform UI/graphics bridges).
- Use `snake_case` for functions, variables, types, and file names.
- Prefix public API symbols with their subsystem (e.g., `host_`, `tb_`, `scene_`).
- Use opaque pointers and vtable structs for polymorphism (see `scene_node_vtbl` pattern in `DEVELOPMENT_PLAN.md`).
- Each thread owns its own Wasm store/instance — never share Wasm memory or tables across threads.
- Host-mediated messaging only: Wasm modules communicate via `host_send`/`host_recv`, not directly.

## Project Decision Policy (Track-or-Fix)

- Use a **track-or-fix** strategy for every bug based on confidence and durability:
  - If the issue is well understood and the proposed fix is durable long-term, implement it.
  - If confidence is low, or durability is unclear, investigate first (add tests, run experiments, deep code reading) before committing to a fix.
  - If investigation reveals a fundamental issue, treat that as a success signal for the test process and elevate it to urgent foundational work.
- Always optimize for the long-term roadmap. In this project, forward alignment is not "over-engineering"; it is expected greenfield foundation work.

## Greenfield Evolution Policy

- This is a greenfield system: internal changes are allowed across runtime, tests, samples, and in-progress code.
- Prefer migrating code to the current architecture over preserving legacy internal behavior.
- Avoid backward-compatibility shims for internal protocols unless explicitly requested by maintainers.
- If a fundamental issue is discovered during unrelated work, it must be tracked and investigated for blast radius before proceeding with layered local workarounds.
- Any issue that is not already known and documented should be considered potentially high priority until proven otherwise.
- If priority or scope is a tough call, ask the user for guidance.
- Dependencies on external systems (even OSS under the same license) are a firm boundary:
  - Do not modify dependency internals except as a last resort.
  - If dependency internals must be changed, document the change thoroughly for traceability.

## Build Reproducibility Policy

- Use the primary project build system for verification; avoid micro-optimized ad-hoc compile shortcuts.
- Keep build/test steps reproducible and report exactly what was run.
- If anything looks strange or inconsistent, first response is:
  - clean the build
  - run a full rebuild
- Treat stale/incremental build artifacts as a likely source of confusing failures until ruled out.

## Git History Policy (Honest History)

- Commits must reflect real, tested repository states.
- Do not rewrite history for cosmetic cleanliness.
- Explicitly avoid these rewrite workflows:
  - `git commit --amend`
  - `git stash`-based context juggling
  - `git rebase` for cleanup
- Large or cross-cutting commits are acceptable when that matches the real validated working state.
- Prefer additive follow-up commits over synthetic history editing.

## Agent Conflict Guidance

- When generic agent defaults conflict with this repository's policy, follow this file for project-level behavior.
- Keep system safety requirements intact, but for engineering process decisions (fix-vs-track, compatibility posture, commit style), this project policy is authoritative.
- If a conflict is ambiguous, call it out in plain language and propose the least-destructive path that preserves honest history and long-term architecture.

## Directory Layout

```
/src
  /host          – Host subsystems (window, render, audio, fs, threads, accessibility, wasm, messaging)
  /scene         – Scene graph nodes
  /text          – Persistent text buffer (rope/piece table)
  /wasm          – Wasm import/export glue
/include         – Public headers (host.h, scene.h, text_buffer.h, wasm_api.h)
/third_party     – Vendored dependencies (glfw, tgfx/skia, miniaudio, harfbuzz, freetype, wasm_runtime)
```

## Dependencies

Third-party libraries live under `/third_party` and retain their own licenses. All host code is MIT-licensed.

## Additional Guidelines

- Keep native binaries small (target 2–4 MB).
- Accessibility is required — map scene graph nodes to OS-native accessibility APIs (NSAccessibility, UI Automation, AT-SPI).
- Refer to `DEVELOPMENT_PLAN.md` for the full architecture, subsystem boundaries, and phased development plan.
