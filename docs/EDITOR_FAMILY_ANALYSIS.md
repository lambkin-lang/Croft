# Editor Family Analysis

This note captures the first text-editor family comparison on macOS with
Sapling held constant as the document model and the presentation path varied.

## Implemented Families

- `example_editor_text`
  - Scene-graph editor using `croft_scene_text_editor_tgfx_metal`
  - Rendering path: tgfx on Metal
  - Input/layout model: Croft scene nodes plus host gestures
- `example_editor_text_appkit`
  - Native CPU editor using `croft_editor_appkit`
  - Rendering path: AppKit/TextKit/CoreText
  - Input/layout model: native `NSTextView`
- `example_editor_text_metal_native`
  - Scene-graph editor using `croft_scene_text_editor_metal_native`
  - Rendering path: direct Metal with Croft-managed text quads
  - Input/layout model: Croft scene nodes plus host gestures

## Shared Crosscutting Layer

To make the comparison honest, the AppKit path does not reuse the scene editor,
and the direct-Metal path does not reuse tgfx. All three families share only
the document and file-IO
concerns through:

- `croft_editor_document_core`
- `croft_editor_document_fs`

Those layers own:

- Sapling arena/environment/text allocation
- document history, undo, and coalescing
- UTF-8 import/export
- host file loading and saving via the filesystem adapter

Everything above that is allowed to diverge by family.

## Size Results

From the optimized `MinSizeRel` benchmark on March 6, 2026:

- `example_editor_text`: `7,495,920` bytes
- `example_editor_text_appkit`: `88,568` bytes
- `example_editor_text_metal_native`: `110,224` bytes
- `example_render_canvas_metal_native`: `88,928` bytes

The AppKit and direct-Metal editors are both much closer to the direct-Metal
renderer baseline than to the tgfx/Metal editor. That indicates the dominant
size cost in the tgfx editor path is still the tgfx Metal renderer stack, not
the editor/document logic.

Follow-up after routing the direct-Metal editor control path through WIT-facing
host mix-ins for window, clock, menu, clipboard, editor input, and
accessibility:

- `example_editor_text_metal_native`: `148,944` bytes

That adds about `38 KB` over the earlier ad hoc direct-Metal shell. The cost is
real, but it is still small enough to keep the direct-Metal family near the
small-binary end of the spectrum. More importantly, the extra size now buys
explicit host seams that Lambkin can reason about instead of one collapsed
editor shell.

## Runtime Results

From the shared-document runtime benchmark on March 8, 2026, using
`tools/benchmark_editor_runtime_matrix.sh` defaults (`2` runs per target,
shared generated documents, `1200 ms` auto-close):

| Shared document lines | `example_editor_text` avg wall ms | `example_editor_text_appkit` avg wall ms | `example_editor_text_metal_native` avg wall ms |
| --- | ---: | ---: | ---: |
| `200` | `1,390` | `1,364` | `1,383` |
| `1,200` | `1,597` | `1,405` | `1,602` |
| `5,000` | `4,769` | `1,480` | `4,798` |

Because the auto-close floor is fixed at `1200 ms`, the useful signal is the
extra cost above that floor. AppKit stays close to the floor even at `5,000`
lines, while both scene families climb sharply and stay very close to one
another.

## What This Shows

- Sapling text storage and file IO do not force a multi-megabyte editor.
- The main crosscutting concern so far is the document model, not the renderer.
- Text shaping, selection behavior, hit-testing, and scrolling do not need a
  single universal abstraction yet. Different families can own those concerns
  differently while still sharing Sapling-backed state.
- The direct-Metal path can stay close to the AppKit path in size even after
  adding enough text rendering to support the scene editor.
- On larger documents, the current runtime cliff clusters in the two scene
  families, not in AppKit.
- The direct-Metal editor now matches the tgfx/Metal editor much more closely
  in large-document runtime than it matches AppKit, which suggests the current
  hot path is the shared scene-text layout/input stack rather than the render
  backend alone.
- The remaining comparison work is not just about performance. It is about
  making hidden host services explicit: AppKit currently gives IME,
  accessibility, and undo-manager behavior "for free", while the direct-Metal
  path keeps those as visible join-points we still need to model.
- The direct-Metal editor is now a better research probe than before because
  the control plane is no longer mostly ad hoc. The remaining collapsed portion
  is rendering itself, which is exactly where the current family is supposed to
  stay opinionated.

## Next Step

The next useful step is no longer "add the third family"; that part is done.
The next useful step is no longer generic runtime comparison either; that data
now exists. The next useful step is to explain the large-document scene-family
cliff:

- isolate or profile the shared scene-text layout/input path on larger
  documents
- isolate input and accessibility concerns the same way `croft_editor_document`
  isolated the data layer
- decide whether the direct-Metal editor should keep reusing the scene nodes or
  split into its own layout/input family
