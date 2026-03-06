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
| `example_foundation_threads` | Foundation-only worker thread, timing, and logging | `croft_foundation` |
| `example_messaging_roundtrip` | Validated message envelope round-trip over the host queue | `croft_messaging` |
| `example_fs_inspect` | Host filesystem access and resource-path discovery | `croft_fs` |
| `example_sapling_text` | Sapling text clone-on-write editing with no GUI | `sapling` |
| `example_wasm_guest` | Embedded Wasm guest bridged into Croft host imports | `croft_wasm_wasm3` |
| `example_ui_window` | Windowing only; no renderer | `croft_ui_glfw_*` |
| `example_render_canvas` | GPU-backed 2D rendering | `croft_render_tgfx_*` |
| `example_scene_graph` | Scene graph layout, hit-testing, and rendering | `croft_scene_core_tgfx_*` |
| `example_zoom_canvas` | Gesture-assisted infinite canvas demo | `croft_scene_core_tgfx_*`, gesture backend |
| `example_editor_text` | Text-editor shell over Sapling, scene, and host IO | `croft_scene_text_editor_tgfx_*`, `croft_fs`, gesture backend |
| `example_a11y_tree` | Native accessibility handles for scene nodes on macOS | scene target, `croft_a11y_macos` |
| `example_menu_bar` | Native menu intents on macOS | UI target, `croft_menu_macos` |
| `example_audio_tone` | Host audio playback via miniaudio | `croft_audio_miniaudio` |

Notes:

- `*` in target names above means Croft picks the platform-appropriate variant.
  On macOS the current preferred path is Metal-backed when available.
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
