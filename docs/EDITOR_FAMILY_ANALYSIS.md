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

## Scene Profile Probe

To localize that `5,000`-line cliff, I added opt-in scene-editor telemetry and
ran:

`bash ./tools/benchmark_editor_runtime.sh --editor-profile --iterations 1 --editor-lines 5000 --keep 10`

That single profiled pass produced:

| Target | Wall ms | Frames | Editor draw ms | Measure-text calls | Measure-text ms | Background lines | Text lines | Gutter lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `example_editor_text` | `5,431` | `31` | `324.244` | `14,876` | `189.078` | `155,124` | `155,124` | `155,124` |
| `example_editor_text_metal_native` | `5,098` | `72` | `202.812` | `34,520` | `95.331` | `360,288` | `360,288` | `360,288` |

The useful constraints from that probe are:

- The idle benchmark never touched the expensive line-mapping helpers:
  `visible_line_count`, `visible_line_number_for_model_line`,
  `model_line_number_for_visible_line`, and hit-testing all stayed at zero.
- The scene editor is still redrawing the entire `~5,000`-line document in
  each of its three per-frame passes. The line counters scale almost exactly as
  `frames * line_count`, which means there is no viewport culling yet.
- Text measurement is real work, but it is not the whole story. The measured
  editor-node CPU time stayed far below the end-to-end wall time, so a large
  part of the cliff still sits after the node has already emitted all of that
  geometry.

## Follow-up Probe

I then added frame-shell timing plus backend timing and reran the same profiled
`5,000`-line benchmark. For these scene-only probes, the useful wall clock is
the sample's own `editor-scene ... wall_ms=` telemetry, not the outer
`runtime_bench_runner` process time, because the runner also includes shutdown
after the sample loop exits.

That produced:

| Target | Sample wall ms | Frames | Drawable ms | Submit ms | Draw-tree ms | Background lines | Text lines | Gutter lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `example_editor_text` | `1,202` | `33` | `152.847` | `641.840` | `339.584` | `1,221` | `1,221` | `1,221` |
| `example_editor_text_metal_native` | `1,205` | `71` | `948.940` | `1.086` | `184.000` | `2,627` | `2,627` | `2,627` |

That follow-up changes the conclusion again:

- Viewport culling worked. The per-pass line counts dropped by roughly two
  orders of magnitude.
- The scene sample itself was already hitting the `~1200 ms` auto-close floor.
  The misleading `~5 s` number was mostly process teardown outside the sample's
  own render loop.
- The remaining scene-side work clustered around the render-frame boundary:
  `flushAndSubmit` in the tgfx/Metal path and `nextDrawable` in the
  native-Metal path.

## Idle Redraw Probe

I then changed both scene editor shells to redraw only on invalidation
(input/scroll/resize/find changes plus caret blink) instead of submitting a new
frame on every poll iteration. Rerunning the same profiled `5,000`-line
benchmark produced:

| Target | Sample wall ms | Frames | Drawable ms | Submit ms | Draw-tree ms | Background lines | Text lines | Gutter lines |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `example_editor_text` | `1,200` | `3` | `1.242` | `620.946` | `30.935` | `111` | `111` | `111` |
| `example_editor_text_metal_native` | `1,200` | `4` | `1.260` | `0.071` | `10.000` | `148` | `148` | `148` |

That final probe changes the conclusion one more time:

- The scene-family benchmark was dominated by unnecessary continuous redraw, not
  by unavoidable large-document editor work.
- Once redraw is invalidation-driven, the native-Metal path stops spending
  meaningful time in drawable acquisition, and the shared scene editor work
  drops to a few milliseconds over the full run.
- The tgfx/Metal path still pays a visible submit cost per rendered frame, but
  with only a few frames per run it no longer dominates the sample's own wall
  time.

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
- The first scene-profile probe narrows that further: the immediate hot path is
  full-document redraw every frame, while line-map lookup and hit-testing are
  not part of the idle large-document cliff.
- The backend-timing probe narrows it again: after culling, the scene work that
  remained was mostly continuous frame submission and drawable acquisition.
- The redraw-throttling probe shows the most foundational fix so far: for these
  editor shells, invalidation policy matters more than any one text-layout or
  renderer micro-optimization.
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

- push the new invalidation-driven redraw policy down into reusable host/scene
  loop support instead of leaving it duplicated in the example shells
- keep both runner wall time and sample-reported wall time visible in the
  benchmark history so GUI teardown cost stays measurable without obscuring
  in-loop work
- if tgfx/Metal still matters after redraw throttling, profile `flushAndSubmit`
  more deeply and see whether that backend can reuse or defer any per-frame
  submit work
- isolate input and accessibility concerns the same way `croft_editor_document`
  isolated the data layer
- decide whether the direct-Metal editor should keep reusing the scene nodes or
  split into its own layout/input family
