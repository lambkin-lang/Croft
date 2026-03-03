# Lightweight Cross‑Platform Host Architecture (C11 + pthreads + Wasm)

This document defines the architecture, subsystem boundaries, and development plan for a lightweight, cross‑platform host runtime written in **portable C11**, using **pthreads**, **GLFW**, **tgfx/Skia**, **miniaudio**, and an embedded **Wasm runtime**. The host executes one Wasm instance per OS thread, with strict isolation and host‑mediated messaging.

Croft is also designed to serve as the **runtime library for a new programming language**, producing small native binaries. The Wasm‑style isolation model (per‑thread instances, host‑mediated messaging, no shared memory) forms the semantic conceptualization of the architecture whether modules are compiled to Wasm or to native code.

---

## 1. Goals

- Small native binaries (2–4 MB).
- Pure C11 codebase (no C++).
- Cross‑platform: macOS, Windows, Linux.
- GPU‑accelerated 2D rendering with infinite zoom.
- Full font shaping (HarfBuzz + FreeType + native APIs).
- Accessibility via OS‑native bridges.
- One Wasm instance per thread; no direct Wasm–Wasm communication.
- Host‑mediated messaging between threads.
- Modular subsystems (audio, filesystem, rendering, threading).
- Serve as a runtime library for a new programming language, supporting both Wasm and native compilation targets while preserving Wasm‑like isolation semantics.

---

## 2. Process Model

- Single OS process.
- Multiple OS threads created via pthreads (or Win32 threads on Windows).
- Each thread owns:
  - A Wasm runtime store.
  - A Wasm instance.
  - An inbox/outbox message queue.

Threads never share Wasm memory or tables.

---

## 3. Subsystems

### 3.1 Windowing and Input (GLFW)

- GLFW handles:
  - Window creation
  - Input events
  - Clipboard
  - GPU context creation

### 3.2 Rendering (tgfx or Skia)

- GPU‑accelerated 2D renderer.
- Required features:
  - Vector shapes
  - Text rendering
  - Arbitrary transforms
  - Infinite zoom

### 3.3 Audio (miniaudio)

- Single‑header C library.
- Provides:
  - Playback
  - Capture (optional)
  - Device enumeration

### 3.4 Filesystem

- Standard C I/O:
  - `fopen`, `fread`, `fwrite`, `fclose`
  - `stat` via `<sys/stat.h>`
- Per‑OS helpers for:
  - Config directory
  - Cache directory
  - Resource directory

### 3.5 Threading (pthreads)

- Abstractions:
  - `host_thread_t`
  - `host_mutex_t`
  - `host_cond_t`
- Implemented via pthreads on macOS/Linux.
- Windows uses Win32 threads with a pthread‑compatible wrapper.

### 3.6 Messaging (Host‑Mediated)

- Each thread has:
  - `struct host_queue inbox;`
  - `struct host_queue outbox;`
- Queues are MPSC or mutex‑protected ring buffers.
- Wasm can only:
  - `host_send(channel, ptr, len)`
  - `host_recv(channel, out_ptr, max_len)`

### 3.7 Accessibility

- Scene graph nodes map to OS accessibility nodes.
- Per‑OS C shims:
  - macOS: NSAccessibility
  - Windows: UI Automation
  - Linux: AT‑SPI

---

## 4. Wasm Execution Model

The Wasm execution model defines the isolation and communication semantics for all modules. These same constraints apply when modules are compiled to native code — each thread context is isolated with its own state and message queues, mirroring Wasm‑style sandboxing.

### 4.1 Per‑Thread Wasm Context

```c
struct wasm_thread_ctx {
    struct wasm_runtime *rt;
    struct wasm_instance *inst;
    struct host_queue inbox;
    struct host_queue outbox;
};
```

### 4.2 Host‑Provided Imports

```c
void     host_log(int level, const char *ptr, uint32_t len);
uint64_t host_time_millis(void);

int32_t  host_send(uint32_t channel, const uint8_t *ptr, uint32_t len);
int32_t  host_recv(uint32_t channel, uint8_t *out_ptr, uint32_t max_len);

int32_t  host_fs_read(const char *path_ptr, uint32_t path_len,
                      uint8_t *buf_ptr, uint32_t buf_len);

int32_t  host_get_buffer_slice(uint32_t buffer_id,
                               uint32_t start,
                               uint32_t len,
                               uint8_t *out_ptr);

int32_t  host_apply_edit(uint32_t buffer_id,
                         const uint8_t *edit_ptr,
                         uint32_t edit_len);
```

### 4.3 Wasm Exports

```c
int wasm_handle_event(uint32_t event_type, uint32_t arg0, uint32_t arg1);
int wasm_tick(void);
```

---

## 5. Scene Graph

### 5.1 Node Base Type

```c
typedef struct scene_node_vtbl scene_node_vtbl;

typedef struct scene_node {
    float x, y, sx, sy;
    uint32_t flags;
    scene_node_vtbl *vtbl;
    struct scene_node *first_child;
    struct scene_node *next_sibling;
} scene_node;

struct scene_node_vtbl {
    void (*draw)(scene_node *n, struct render_ctx *rc);
    void (*hit_test)(scene_node *n, float x, float y, struct hit_result *out);
    void (*update_accessibility)(scene_node *n);
};
```

### 5.2 Node Types

- `viewport_node`
- `code_block_node`
- `file_tile_node`
- `asset_node`
- `graph_node`

---

## 6. Editor Model (Persistent Text)

### 6.1 API

```c
typedef struct text_buffer text_buffer;

text_buffer *tb_create(void);
void tb_destroy(text_buffer *);

int tb_insert(text_buffer *, uint32_t pos,
              const uint8_t *utf8, uint32_t len);

int tb_delete(text_buffer *, uint32_t pos, uint32_t len);

int tb_slice(const text_buffer *, uint32_t start, uint32_t len,
             uint8_t *out, uint32_t out_cap);
```

### 6.2 Requirements

- Persistent structure (rope, finger tree, or piece table).
- Cheap slicing for rendering.
- Structural views (AST, symbols) computed in worker threads.

---

## 7. Development Plan

### Phase 1: Host Skeleton
- GLFW window + event loop.
- GPU renderer integration.
- Static text buffer drawn on screen.

### Phase 2: Wasm on UI Thread
- Embed Wasm runtime.
- Implement `host_log`, `host_time_millis`.
- Route input events → Wasm.

### Phase 3: Worker Threads + Messaging
- Implement pthread wrapper.
- Add message queues.
- Create worker thread with its own Wasm instance.

### Phase 4: Filesystem, Audio, Editor Model
- Implement filesystem helpers.
- Integrate miniaudio.
- Implement `text_buffer`.
- Expose buffer APIs to Wasm.

### Phase 5: Accessibility
- Implement macOS or Windows accessibility bridge.
- Map scene graph → accessibility tree.

### Phase 6: Hardening
- ABI versioning.
- Message schemas.
- Additional Wasm modules (analysis, language services).

---

## 8. Directory Layout

```
/src
  /host
    host_main.c
    host_window.c
    host_render.c
    host_audio.c
    host_fs.c
    host_threads.c
    host_accessibility.c
    host_wasm.c
    host_messaging.c
  /scene
    scene_node.c
    scene_graph.c
    node_code_block.c
    node_file_tile.c
  /text
    text_buffer.c
  /wasm
    wasm_imports.c
    wasm_exports.c
/include
  host.h
  scene.h
  text_buffer.h
  wasm_api.h
/third_party
  glfw/
  tgfx/ or skia/
  miniaudio/
  harfbuzz/
  freetype/
  wasm_runtime/
```

---

## 9. Build System

- CMake or Meson recommended.
- Must support:
  - C11
  - pthreads
  - LTO
  - dead‑code elimination
  - static linking (optional)

---

## 10. License

- All host code: MIT or zlib.
- Third‑party libraries retain their own licenses.
