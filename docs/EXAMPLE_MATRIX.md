# Croft Example Matrix

Croft examples are meant to model meaningful subsystem selections rather than
ad hoc tests. Each example should justify the artifacts it links and avoid
pulling larger stacks unless the concept actually requires them.

Examples are defined as explicit CMake targets and emitted into:

- `build/croft-examples.json`
- `build/croft-example-targets.txt`

They are built as a group with:

```bash
cmake --build build --target croft_examples
```

## Current Ladder

| Target | Focus | Required Croft artifacts |
| --- | --- | --- |
| `example_foundation_threads` | Foundation-only worker thread, timing, and logging | `croft_host_log`, `croft_host_time`, `croft_host_thread` |
| `example_messaging_roundtrip` | Validated message envelope round-trip over the host queue | `croft_msg_frame`, `croft_host_queue` |
| `example_fs_inspect` | Host filesystem access and resource-path discovery | `croft_fs` |
| `example_wit_fs_read` | Host filesystem open/read/close through generated WIT file resource handles | `croft_wit_host_fs_runtime` |
| `example_wit_text_cli` | Shared common-side WIT text logic reused in a CLI-shaped host | `croft_wit_text_program`, `croft_wit_text_runtime`, `croft_wit_host_fs_runtime` |
| `example_wit_text_wasm_host` | The same common-side WIT text logic reused in a Wasm-hosted world through Croft's current `wasm3` bridge | `croft_wasm_wasm3`, `croft_wit_text_program`, `croft_wit_text_runtime` |
| `example_wit_clock_now` | Host monotonic clock query through generated WIT service commands | `croft_wit_host_clock_runtime` |
| `example_wit_window_events` | Window lifecycle and polled UI events through generated WIT window commands | `croft_wit_host_window_runtime`, `croft_wit_host_clock_runtime` |
| `example_wit_gpu_canvas` | Direct-Metal surface access through generated WIT window, GPU, and clock mix-ins | `croft_wit_host_window_runtime`, `croft_wit_host_gpu2d_runtime`, `croft_wit_host_clock_runtime` |
| `example_sapling_text` | Sapling text clone-on-write editing over the single-thread linear arena profile | `sapling_core` |
| `example_wit_text_handles` | Sapling text editing through generated WIT commands and opaque resource handles | `croft_wit_text_runtime` |
| `example_wit_text_window` | The same common-side WIT text logic rendered through native window and GPU mix-ins | `croft_wit_text_program`, `croft_wit_text_runtime`, `croft_wit_host_window_runtime`, `croft_wit_host_gpu2d_runtime`, `croft_wit_host_clock_runtime` |
| `example_wit_db_kv` | Sapling key-value round-trip through generated WIT `db` and `txn` resource handles | `croft_wit_store_runtime` |
| `example_wit_mailbox_ping` | Common-core mailbox round-trip through generated WIT mailbox resource handles | `croft_wit_mailbox_runtime` |
| `example_wasm_guest` | Embedded Wasm guest bridged into Croft host imports | `croft_wasm_wasm3` |
| `example_ui_window_opengl` | Window only; OpenGL-capable GLFW context and no renderer | `croft_ui_glfw_opengl` |
| `example_ui_window_metal` | Window only; no-API GLFW window for the Metal path | `croft_ui_glfw_metal` |
| `example_window_menu_opengl` | Native menu shell with no renderer on the OpenGL UI path | `croft_ui_glfw_opengl`, `croft_menu_macos` |
| `example_window_menu_metal` | Native menu shell with no renderer on the Metal UI path | `croft_ui_glfw_metal`, `croft_menu_macos` |
| `example_render_canvas_opengl` | GPU-backed 2D rendering on the tgfx OpenGL path | `croft_render_tgfx_opengl` |
| `example_render_canvas_metal` | GPU-backed 2D rendering on the tgfx Metal path | `croft_render_tgfx_metal` |
| `example_render_canvas_metal_native` | GPU-backed 2D rectangles on a direct Metal path with no tgfx dependency | `croft_render_metal_native` |
| `example_scene_graph` | Scene graph layout, hit-testing, and rendering | active `croft_scene_core_tgfx_*` variant |
| `example_zoom_canvas` | Gesture-assisted infinite canvas demo | active `croft_scene_core_tgfx_*` variant, gesture backend |
| `example_editor_text` | Text-editor shell over Sapling, scene, and host IO | active `croft_scene_text_editor_tgfx_*` variant, `croft_editor_document_core`, `croft_editor_document_fs`, gesture backend |
| `example_editor_text_appkit` | Native AppKit/TextKit editor over the same Sapling-backed document layer | `croft_editor_appkit`, `croft_editor_document_core`, `croft_editor_document_fs` |
| `example_editor_text_metal_native` | Scene-based text editor on the direct-Metal renderer with no tgfx dependency | `croft_scene_text_editor_metal_native`, `croft_editor_document_core`, `croft_editor_document_fs`, gesture backend |
| `example_a11y_tree` | Native accessibility handles for scene nodes on macOS | scene target, `croft_a11y_macos` |
| `example_audio_tone` | Host audio playback via miniaudio | `croft_audio_miniaudio` |

Notes:

- The UI-only and window+menu examples are explicit backend datapoints, so they
  can be benchmarked without dragging in tgfx.
- `example_sapling_text` now uses `sapling_core` and the linear arena backing,
  so it is the current smallest in-tree proof point for the Wasm-aligned
  datastore/text side.
- `example_wit_text_handles` is the first model-program sample that crosses a
  WIT/resource barrier instead of calling Sapling text APIs directly. It is the
  current best in-tree proxy for Lambkin-generated common-core code.
- `example_wit_db_kv` extends that barrier to transactional datastore state and
  keeps `DB*` and `Txn*` fully hidden behind WIT handle IDs.
- `example_wit_mailbox_ping` is the first common-side no-shared-memory mailbox
  sample. It intentionally models nonblocking message passing without pulling
  in host threads, sleeping, or native queue APIs.
- `example_wit_fs_read` is the first host mix-in WIT sample. It wraps the
  native `host_fs` pointer-shaped API in opaque file handles so generated model
  programs do not observe raw host file descriptors.
- `example_wit_text_cli` is the first shared common-side WIT logic sample that
  runs in a CLI/file-oriented world shape instead of only as a direct
  common-core micro-example.
- `example_wit_text_wasm_host` proves that the same common-side WIT text logic
  can also survive a host-Wasm world shape. The common command choreography is
  unchanged; only the final transport across the boundary changes.
- `example_wit_clock_now` is the first stateless host mix-in WIT sample. It is
  intentionally service-shaped rather than resource-shaped, which keeps the
  boundary honest: not every host capability owns lifetime-managed state.
- `example_wit_window_events` is the first host-window/input mix-in sample. It
  deliberately adapts Croft’s current singleton/callback UI host into a
  `window` resource plus polled event queue, which makes the mismatch explicit
  instead of hiding it behind direct callbacks.
- `example_wit_gpu_canvas` is the first GPU-facing mix-in sample. It models
  `surface` ownership separately from `window` even though the current
  direct-Metal host still hides a singleton render target behind the scenes.
- `example_wit_text_window` is the first proof that the same common-side WIT
  text logic can survive both a CLI-shaped host and a native window/GPU host
  without re-exposing raw Sapling pointers or host objects.
- Current optimized size datapoints on this machine for the shared-logic WIT
  family are: `example_wit_text_cli` `53,416`,
  `example_wit_text_wasm_host` `123,288`,
  `example_wit_gpu_canvas` `90,456`, and
  `example_wit_text_window` `109,208`. For this small sample, the current
  `wasm3` host adds more payload than the direct-Metal window/GPU mix-ins.
- The editor document layer is now split: `croft_editor_document_core` carries
  Sapling state, history, and edit semantics, while
  `croft_editor_document_fs` is the host-fs adapter for open/save.
- `example_render_canvas_metal_native` is the first direct-Metal proof point.
  It uses the same host window path as the tgfx Metal examples, but replaces
  tgfx with a bespoke Metal renderer so the solver can eventually choose a much
  smaller macOS render splice.
- `example_editor_text_appkit` is the first CPU-native editor family. It keeps
  Sapling and file IO, but delegates layout, selection, and text rendering to
  AppKit/TextKit instead of Croft scene nodes.
- `example_editor_text_metal_native` is the third editor family. It keeps the
  Croft scene/input model, but swaps out tgfx for the direct-Metal renderer and
  its cached text-quads path.
- The render backend comparison is currently done with separate build
  directories. Croft's current in-tree tgfx integration supports one GPU
  backend variant per configure, selected by `TGFX_USE_OPENGL` or
  `TGFX_USE_METAL`. The benchmark helper now records three render datapoints:
  tgfx/OpenGL, tgfx/Metal, and native-direct Metal.
- The editor-family comparison now spans tgfx/Metal, AppKit/TextKit CPU, and
  direct-Metal.
- Higher-level scene/editor examples still ride the active tgfx backend for the
  current configure. They remain useful for feature-ladder modeling even though
  their target names are not backend-suffixed yet.
- Examples are `EXCLUDE_FROM_ALL`. They exist to model subsystem selections and
  to support smoke tests and size benchmarking without forcing every default
  build to link the demo binaries.
- The text editor is intentionally near the top of the ladder. It is the current
  convergence point for UI, rendering, gestures, filesystem access, and Sapling
  text, but it should remain decomposable into the lower examples above.

## Design Rule

When adding a new example:

1. Pick one clear capability boundary.
2. Link only the artifacts needed for that capability.
3. Prefer a smaller prerequisite example when the larger example is still under construction.
4. Keep the text editor as the most demanding current example, not the starting point.
