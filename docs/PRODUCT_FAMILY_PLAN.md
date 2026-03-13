# Croft Product-Family Plan

This document is the working guide for keeping Croft aligned with the Lambkin
vision across future sessions.

Croft is not trying to be one fixed runtime shape. Croft is modeling the set of
runtime artifacts, interfaces, and implementation variants that Lambkin can
select from when constructing a final program. The working motto remains:

**Big analysis, small binaries.**

## Purpose

Croft serves two distinct roles that must remain separate:

- Common language/runtime support:
  `seq`, `text`, Sapling, editor text model and commands, wire formats, and
  other reusable data-structure logic.
- Host/platform support:
  threads, clocks, filesystems, CLI integration, windows, menus, GPU access,
  audio, and Wasm hosting.

The sample C programs in this repository are not intended to look like natural
hand-written C. They are models for the kind of targeted, generated, and often
tangled code that Lambkin can emit once it has already solved the product-family
selection problem.

## Hard Rules

The following rules should guide all future work:

- Standard-library/data-structure code must not depend on OS headers or host
  pointers.
- Host/platform code must be emitted as separate artifacts, not folded into a
  lowest-common-denominator "foundation" library.
- WIT is the IDL for exposed interfaces, but Croft uses generated C bindings
  and implementation shims rather than the Canonical ABI.
- WIT `resource` handles are the boundary type that model programs should see
  instead of raw OS pointers.
- Worlds are compositional supersets: a small common core plus optional mix-in
  interfaces for target-specific or feature-specific requirements.
- A Wasm target is not the same thing as a host-side Wasm executor. Hosting a
  Wasm guest is a host/platform feature. Compiling Lambkin output to `.wasm` is
  a target profile.
- Croft's threading model is no-shared-memory. Multiple workers are allowed,
  including Wasm instances on different host threads or worker-like runtimes,
  but the core interfaces should assume explicit messaging rather than shared
  memory.

## Target Boundary Model

### 1. Common Support Layer

This layer is the closest thing to the Lambkin standard library:

- Sapling arena, database, transactions, `seq`, and `text`
- editor text model and editor commands
- wire encoders/decoders and schema-backed message formats
- runner protocol/state logic that does not require OS clocks, sleeping, or
  host-owned threads

This layer must be buildable in a single-threaded, linear-memory-friendly
profile.

### 2. WIT Interface Layer

This layer describes the contract surface that generated programs target:

- common resources and operations
- target-neutral service interfaces
- optional mix-ins for clocks, filesystems, UI, GPU, audio, and host execution

The WIT files define the contract. Generated C headers and support code define
the concrete ABI used inside Croft and the sample programs.

### 3. Host/Platform Layer

This layer owns the concrete implementations that bind the contract to a real
environment:

- native threads, clocks, sleep, and mailbox implementations
- native filesystems and CLI integration
- Wasm hosting/interpreter/runtime adapters
- macOS-specific AppKit, menu, accessibility, gesture, and Metal code
- other future platform-specific adapters

This layer may be tightly coupled to a target OS when that produces a smaller or
more appropriate result.

## WIT Structure Plan

The first WIT packages should be split into common interfaces and optional
mix-ins.

### Common Interfaces

These are intended to be available across many worlds:

- `croft:std/seq`
- `croft:std/text`
- `croft:std/db`
- `croft:std/txn`
- `croft:std/mailbox`
- `croft:std/runner`

These interfaces should expose `resource` types such as:

- `resource seq`
- `resource text`
- `resource db`
- `resource txn`
- `resource mailbox`
- `resource runner`

### Optional Mix-In Interfaces

These are added only when a program actually needs them:

- `croft:host/clock`
- `croft:host/fs`
- `croft:host/cli`
- `croft:host/window`
- `croft:host/menu`
- `croft:host/gpu2d`
- `croft:host/audio`
- `croft:host/wasm-executor`

Representative host resources:

- `resource file`
- `resource directory`
- `resource window`
- `resource menu-bar`
- `resource surface`
- `resource audio-stream`
- `resource wasm-instance`

## Generated C ABI Rules

Because Croft uses WIT as IDL but not the Canonical ABI, the generated C layer
needs stable local rules:

- Handles are opaque integer IDs, not host pointers.
- Ownership is explicit.
- Destruction is explicit.
- Buffer passing uses pointer-plus-length fields and explicit output lengths.
- No hidden cross-boundary heap ownership.
- Host implementation tables sit behind generated facades rather than leaking
  implementation structs into model programs.
- Generated C symbol names are package-qualified from the WIT `package`
  declaration, and generated files emit traceability comments that record the
  source schema plus the chosen qualifier mapping.

If a future change violates one of these rules, it should be treated as an
architectural regression rather than a convenience improvement.

## Current Codebase Realignment

The current repository already contains the raw material, but several artifacts
need to be split or renamed to make the boundaries honest.

| Current area | Target direction | Required action |
| --- | --- | --- |
| `croft_foundation` | Split common base from host services | Move thread/time assumptions into separate host artifacts and leave a true host-neutral base |
| `croft_messaging` | Separate wire format from host mailbox implementation | Keep protocol/schema validation in the common layer, keep queueing in host layer |
| `sapling` | Support multiple profiles | Add a true single-thread core profile and keep threaded support as a separate variant |
| `editor_document_core` + `editor_document_fs` | Keep document core separate from file-backed adapter | Keep document state/history pure; move load/save to a host-fs add-on |
| `runner` + `wasi/shim` | Split protocol core from host execution shell | Keep replay/message/intent logic common; keep clocks/sleep/host execution separate |
| `croft_wasm_wasm3` | Keep as host runtime only | Treat embedded Wasm execution as a host feature, not the common runtime |

Current status:

- `croft_foundation` now anchors the host-neutral base artifact.
- `croft_host_log`, `croft_host_time`, `croft_host_thread`, `croft_msg_frame`,
  and `croft_host_queue` now exist as separate host/service artifacts.
- `sapling_core` now exists as the single-thread profile, and
  `sapling_core_threaded` now exists as the pthread-backed variant used by the
  compatibility aggregate and host runner/WASI path.
- `example_sapling_text` now exercises the linear arena backing through the
  single-thread `sapling_core` target.
- `sapling_runner_core`, `sapling_runner_core_threaded`,
  `sapling_wasi_runtime`, `sapling_wasi_runtime_threaded`,
  `sapling_runner_host`, and `sapling_wasi_host` now keep runner/WASI host
  shell concerns out of `sapling_core` while making the threaded profile an
  explicit build-graph choice instead of a downstream compile flag.
- `build/croft-artifacts.json` now records per-artifact profile metadata, and
  the first Sapling-adjacent tier8 artifacts (`croft_editor_document_*`,
  `croft_wit_text_runtime`, and `croft_wit_store_runtime`) stay explicitly
  single-thread until their own wrapper state machines gain synchronization.
- `croft_editor_document_core` now carries document state/history and
  `croft_editor_document_fs` now handles file-backed open/save.
- The first common-side WIT package now exists in `schemas/wit/common-core.wit`
  and is now exercised by `example_wit_text_handles`,
  `example_wit_db_kv`, and `example_wit_mailbox_ping` through opaque resource
  handles instead of raw `Text*`, `DB*`, or `Txn*`.
- The first host mix-in WIT package now exists in `schemas/wit/host-fs.wit`
  and is exercised by `example_wit_fs_read` through opaque `file` resource
  handles instead of the raw `uint64_t` host-fs handle representation.
- The next host mix-in WIT package now exists in `schemas/wit/host-clock.wit`
  and is exercised by `example_wit_clock_now` as a stateless service-shaped
  boundary over `host_time_millis()`.
- The next major host pressure point now has a first model in
  `schemas/wit/host-window.wit`, exercised by `example_wit_window_events`.
  It adapts the current singleton/callback GLFW host into a `window` resource
  plus a polled event stream.
- The next host mix-in WIT package now exists in `schemas/wit/host-gpu2d.wit`,
  exercised by `example_wit_gpu_canvas` as a direct-Metal surface boundary that
  keeps capability queries separate from resource lifetime.
- Richer host mix-in WIT packages now exist in `schemas/wit/host-menu.wit`,
  `schemas/wit/host-clipboard.wit`, `schemas/wit/host-editor-input.wit`, and
  `schemas/wit/host-a11y.wit`.
- The direct-Metal editor host control path now uses those runtimes through
  `example_editor_text_metal_native`, which keeps rendering direct while moving
  window, clock, menu, clipboard, input, and accessibility toward the same
  WIT-facing boundary model as the smaller samples.
- `example_wit_textpad_window` now reuses `host-menu`, `host-clipboard`, and
  `host-editor-input` outside the scene-editor family, which makes those
  mix-ins less editor-specific and gives Croft a smaller second comparison
  point.
- `croft_wit_text_program` now proves that the same common-side WIT text logic
  can survive `example_wit_text_cli`, `example_wit_text_wasm_host`, and
  `example_wit_text_window`, instead of being trapped in one sample shape.
- `tools/wit_codegen.c` now owns package-qualified C naming,
  reserved-word sanitization, and generated traceability comments instead of
  relying on per-schema manual prefixes to keep independently evolved packages
  linkable together.
- `tools/wit_codegen.c` now also collapses duplicated leading stems based on
  the WIT package tail (`HostFsFsCommand` -> `HostFsCommand`,
  `ResultTestTestResultCarrier` -> `ResultTestResultCarrier`) while preserving
  package provenance in the generated names and comments.
- `tools/wit_codegen.c` now also emits rename/trace manifests for every schema
  generation target and normalizes exact-tail helper macros such as
  `SAP_WIT_HOST_WINDOW_RESOURCE_INVALID`.
- `tools/benchmark_runtime_perf.sh` now exists alongside the size benchmark so
  runtime behavior can be compared, not just final binary size. The automated
  shell path currently applies to non-GUI examples; the macOS GUI family still
  needs direct top-level launch commands on this host.
- The remaining naming cleanup question is narrower now: how much further
  should exact-tail helper names be normalized before traceability starts to
  suffer?

## World Plan

The first world set should be explicit and small:

- `world core-minimal`
  Common text/db/txn/mailbox only. No filesystem, no clocks, no UI.
- `world batch-cli`
  `core-minimal` plus filesystem and CLI.
- `world wasm-hosted`
  `core-minimal` plus only the host services explicitly required by the guest.
- `world desktop-rendered`
  `core-minimal` plus window/menu/gpu and optional filesystem.
- `world mac-collapsed`
  A deliberately Mac-coupled shape that may use AppKit or direct Metal and is
  not expected to port by flipping one switch.

These worlds should remain mix-in compositions, not monolithic product modes.

## Sample Program Roadmap

Samples should be treated as product-family exemplars rather than generic demos.

### Family A: Wasm-Aligned Common-Core Samples

These are the first priority for validating the common layer:

- a minimal `text` sample
- a minimal `db` sample
- a mailbox/worker sample with no shared memory
- a document-editing sample that does not require host filesystem access

Current status:

- `example_sapling_text` is the direct common-core baseline.
- `example_wit_text_handles` is the first handle-oriented WIT/resource version
  of that baseline and should be treated as the preferred shape for future
  model-program growth.
- `example_wit_db_kv` now covers the transactional datastore slice.
- `example_wit_mailbox_ping` now covers the no-shared-memory mailbox slice.

### Family B: Same Core Logic with CLI and Filesystem

These prove the add-on nature of host services:

- file-driven batch processor
- command-line editor or transformer

Current status:

- `example_wit_text_cli` now reuses the same common-side WIT text logic as the
  native windowed sample.
- `example_wit_text_wasm_host` now reuses that same common-side WIT text logic
  inside a Wasm-hosted world over Croft's current `wasm3` host.

### Family C: Document-Centric Editor Variants

These are the editor product line:

- small windowed textpad/editor shells over the shared document layer
- direct-Metal editor variants that keep rendering explicit and small
- Mac-collapsed AppKit variants that absorb more native text behavior
- tgfx-backed scene-editor variants kept as comparison controls rather than the
  strategic default

Current status:

- `example_wit_gpu_canvas` now covers the first WIT GPU/window/clock mix-in
  boundary over the direct-Metal path that the editor line can build on.
- `example_wit_text_window` now reuses the same common-side WIT text logic from
  the CLI world and renders it through native window/GPU mix-ins.
- `example_wit_textpad_window` is now the smallest in-tree document-centric
  windowed editor shell.
- `example_editor_text` is now best treated as the tgfx-backed comparison
  editor. Its current shell-level gesture wiring is transitional and should not
  define the editor product direction.
- `example_editor_text_appkit` is the Mac-collapsed CPU-native editor family.
- `example_editor_text_metal_native` is the direct-Metal editor family and the
  current small-binary reference path for a custom-rendered editor.

### Family D: Spatial and Zoomable Workspace Variants

These are a separate product line from the document editors:

- scene-graph and hit-testing baselines
- zoomable canvas/workspace shells
- future Code Bubbles-style multi-node code workspaces

Current status:

- `example_scene_graph` is the lower-level scene/layout/hit-test baseline.
- `example_zoom_canvas` is the current spatial-workspace seed. It owns scroll
  and pinch zoom as viewport/camera behavior and should remain separate from the
  document-centric editor roadmap.

### Family E: Wasm Host Execution Samples

These should remain clearly on the host side:

- embedded guest execution
- WIT-backed service provision to the guest
- comparison points for interpreter vs. other future Wasm hosts

## Editor Product Direction

The editor line should now be treated explicitly as a document-centric product
family inspired by the core editing behavior of VS Code, not as the place where
Croft also experiments with arbitrary zoomable workspaces.

That means:

- the editor line should keep pushing toward a capable programmer's editor
  rather than toward a PAD++ or Code Bubbles canvas
- the spatial/zoomable workspace line should continue as a separate family with
  its own interaction model, viewport semantics, and planning questions
- shared capabilities between those lines should stay at the document/editing
  substrate, not at the shell or camera layer

Explicit non-goals for the editor line:

- not a drop-in replacement for VS Code
- not the VS Code extension marketplace or LSP ecosystem
- not a hidden spatial canvas with editor features bolted on top

Current implemented editor baseline:

- line numbers, current-line highlight, and status strip
- incremental token/fold caching for syntax and fold queries
- find/replace, next/previous match navigation, replace-all, and
  selection-occurrence highlight
- tab policy plus indent/outdent behavior
- bracket matching
- whitespace markers and indent guides
- folding
- wrapped-line layout with row-aware hit testing, viewport mapping, and caret
  movement in the scene editor family
- syntax highlighting for JavaScript/TypeScript, JSON, YAML, Markdown, Python,
  Lambkin, WIT/WAT, CSS, HTML, and XML
- file open/save/save-as plus native context menus in the AppKit and scene
  editor families
- recoverable action handling in the scene editor shells and the WIT-facing
  textpad/editor loops

The next editor capabilities should focus on the hard, high-value portions of a
VS Code-like document editor that do not require the extension/LSP ecosystem:

1. line-height, baseline, and inset policy that make the direct-Metal editor
   converge more honestly toward AppKit where that is appropriate
2. IME/composition, selection affinity, and text-input correctness for the
   custom-rendered editor families
3. decoration plumbing for diagnostics, search results, active ranges, and
   syntax-driven styling without forcing one rendering strategy across families

The spatial-workspace line should not move into larger implementation work until
it has a clearer product definition. The first step there is a question list and
vision pass, not more zoom behavior inside the text editors.

## Milestones

### Phase 1: Boundary Cleanup

- introduce a true host-neutral Croft base layer
- split host thread, time, queue, and filesystem artifacts
- split Sapling into core and threaded profiles
- split editor document core from file-backed adapter

### Phase 2: WIT Barrier Introduction

- add the first common WIT packages for `text`, `db`, `txn`, and `mailbox`
- define the generated C ABI conventions
- implement `resource`-based handles in the model C programs

### Phase 3: Wasm-Aligned Example Ladder

- add small common-core samples that do not assume host filesystem or native UI
- ensure those samples can be benchmarked separately

### Phase 4: Native and Collapsed Worlds

- map the same logic into CLI, desktop, and Mac-collapsed shapes
- preserve intentional divergence between variants when that yields better size
  or target fit

### Phase 5: Measurement and Enforcement

- keep size benchmarking across example families
- document which interfaces and artifacts each sample consumes
- reject changes that pull host/platform dependencies into common-core targets
  without an explicit boundary split

## Current Assessment

The main architectural thesis is now supported by the repository rather than
only described by it.

- The common-side versus host-side split is materially present in the build
  graph (`sapling_core`, host service artifacts, runner/WASI splits, and the
  editor document split).
- The WIT barrier is no longer hypothetical. There are now common-side
  resource worlds plus host mix-ins for `fs`, `clock`, `window`, `gpu2d`,
  `menu`, `clipboard`, `editor-input`, and `a11y`.
- The same common-side text logic now survives three distinct world shapes:
  CLI, host-Wasm, and native window/GPU.
- The macOS rendering experiments now support a clear conclusion: large Metal
  binaries came from the current `tgfx` Metal architecture, not from native
  Metal itself.
- The direct-Metal editor now makes the control-plane boundary explicit by
  routing window/menu/clipboard/input/accessibility through WIT-facing
  runtimes while leaving rendering direct.

That is the right kind of evidence for Lambkin. The repository is now proving
that common logic can stay stable while host families diverge sharply in size,
implementation shape, and coupling.

## Near-Term Priorities

The next implementation passes should stay focused on the editor and
host-boundary pressure points instead of broadening the system indiscriminately.

1. Keep the document-editor line separate from the spatial-workspace line and
   keep zoom/camera behavior out of the editor roadmap.
2. Keep WIT/codegen refactors broad: schema-shape changes should sweep
   `src/runtime`, `examples`, and `tests`, then build and smoke-check the
   example matrix instead of stopping at the first green target.
3. Tighten the direct-Metal editor around text metrics, IME/composition, and
   accessibility so the hard native-editor seams become
   explicit instead of hiding inside one renderer-centric module.
4. Keep the incremental cache and wrapped-row geometry covered by focused
   editor/scene tests so later refactors do not regress quietly.
5. Keep AppKit as the CPU-native contrast case and tgfx as the scene-rendered
   comparison control while favoring direct Metal for the custom-rendered editor
   line.
6. Refine `tools/wit_codegen.c` around remaining exact-tail helper naming and
   how much rename metadata Lambkin should consume directly.
7. Extend the "same logic, different world" proof style with more paired
   samples rather than isolated demos.

## Long-Term Direction

Croft should continue moving toward a smaller, sharper role:

- Croft is a graph of runtime artifacts plus generated WIT-facing shims.
- Lambkin owns the higher-order selection, weaving, and crosscutting policy
  problem.
- XPIs/aspect libraries should form around recurring seams such as resource
  lifetime, transaction policy, mailbox/scheduler behavior, callback-to-poll
  bridging, presentation mapping, editor interaction, and platform-collapse
  policies.

The main long-term risk is not under-capability. It is drifting back toward
convenience abstractions that flatten meaningful family differences too early.

## Horizon Planning

The following items need explicit planning even if they are not immediate
implementation tasks:

- a more explicit world catalog with durable naming and scope
- a serious WIT-facing editor family for menu/input/accessibility/clipboard
- a syntax-highlighting architecture that can serve both native-collapsed and
  custom-rendered editor families
- a decision about which host capabilities should remain reusable mix-ins
  versus which should stay family-specific or collapsed
- a separate spatial-workspace product plan that makes room for zoomable code
  bubbles, canvas semantics, and saved workspace structure without forcing those
  decisions through the editor family first
- a separate web-side Croft-shaped runtime effort for worker/browser hosts
- performance benchmarking alongside size benchmarking for the main family
  comparisons

## Immediate Next Tasks

The next concrete implementation work should happen in this order:

1. Push direct-Metal editor work into line-height/baseline policy, IME,
   and accessibility seams rather than more shell-level chrome.
2. Keep AppKit and tgfx alive as contrast cases while treating the native
   direct-Metal editor as the main custom-rendered reference path.
3. Keep `make test-examples` and the example smoke path honest whenever WIT
   schemas, codegen, or host mix-in signatures change.
4. Keep the new wrapped-row geometry pinned with unit coverage before adding
   richer editor decorations or IME state.
6. Refine `tools/wit_codegen.c` further around remaining exact-tail helpers
   and how rename/trace manifests should feed Lambkin.
7. Read `docs/LAMBKIN_XPI_JOURNAL.md` before expanding host/editor surface area
   so the current join-point questions remain explicit.
8. Use `docs/SPATIAL_WORKSPACE_QUESTIONS.md` to define the workspace vision
   before adding more zoom/canvas implementation work.

## Resume Checklist For Future Sessions

When resuming this work in a new session:

1. Read this document first.
2. Read `README.md` for the current build and benchmark workflow.
3. Read `docs/EXAMPLE_MATRIX.md` to see the current modeled sample families.
4. Read `docs/LAMBKIN_XPI_JOURNAL.md` to pick up the current join-points,
   XPIs, and open research questions.
5. Read `docs/SPATIAL_WORKSPACE_QUESTIONS.md` before treating zoom/canvas work
   as an editor requirement again.
6. For any new code, decide whether it belongs to:
   common support, WIT bindings, or host/platform support.
7. If a change adds an OS dependency to a common target, stop and split the
   boundary instead.
8. If a new sample is added, state which world it models and which artifacts it
   should justify linking.

## Non-Goal Reminder

Croft is not trying to prove that these C programs are pleasant for humans to
author manually. The purpose is to prove that the runtime shapes are valid,
decomposable, and capable of producing small final binaries once Lambkin has
performed the higher-level analysis and selection work.
