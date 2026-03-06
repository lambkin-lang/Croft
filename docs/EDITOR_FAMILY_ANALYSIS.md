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

- `croft_editor_document`

That layer owns:

- Sapling arena/environment/text allocation
- UTF-8 import/export
- host file loading and saving

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

## What This Shows

- Sapling text storage and file IO do not force a multi-megabyte editor.
- The main crosscutting concern so far is the document model, not the renderer.
- Text shaping, selection behavior, hit-testing, and scrolling do not need a
  single universal abstraction yet. Different families can own those concerns
  differently while still sharing Sapling-backed state.
- The direct-Metal path can stay close to the AppKit path in size even after
  adding enough text rendering to support the scene editor.

## Next Step

The next useful step is no longer "add the third family"; that part is done.
The next useful step is to deepen the comparison:

- compare runtime behavior on larger documents
- isolate input and accessibility concerns the same way `croft_editor_document`
  isolated the data layer
- decide whether the direct-Metal editor should keep reusing the scene nodes or
  split into its own layout/input family
