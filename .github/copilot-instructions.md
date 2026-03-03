# Copilot Instructions for Croft

## Project Overview

Croft is a lightweight, cross-platform host runtime written in **portable C11**. It uses pthreads, GLFW, tgfx/Skia, miniaudio, and an embedded Wasm runtime. The host executes one Wasm instance per OS thread with strict isolation and host-mediated messaging.

Croft also serves as the **runtime library for a new programming language**, producing small native binaries. The Wasm-style isolation model (per-thread instances, host-mediated messaging, no shared memory) defines the conceptual architecture regardless of whether modules are compiled to Wasm or to native code.

Target platforms: macOS, Windows, Linux.

## Tech Stack

- **Language**: C11 (no C++ allowed)
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

- **Pure C11** — do not use C++ features or compile as C++.
- Use `snake_case` for functions, variables, types, and file names.
- Prefix public API symbols with their subsystem (e.g., `host_`, `tb_`, `scene_`).
- Use opaque pointers and vtable structs for polymorphism (see `scene_node_vtbl` pattern in `DEVELOPMENT_PLAN.md`).
- Each thread owns its own Wasm store/instance — never share Wasm memory or tables across threads.
- Host-mediated messaging only: Wasm modules communicate via `host_send`/`host_recv`, not directly.

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
