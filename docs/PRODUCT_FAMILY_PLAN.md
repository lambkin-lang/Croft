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
  `example_sapling_text` now exercises the linear arena backing through that
  target.
- `sapling_runner_core`, `sapling_runner_host`, `sapling_wasi_runtime`, and
  `sapling_wasi_host` now keep runner/WASI host shell concerns out of
  `sapling_core` while preserving the compatibility aggregate.
- `croft_editor_document_core` now carries document state/history and
  `croft_editor_document_fs` now handles file-backed open/save.
- The first common-side WIT package now exists in `schemas/wit/common-core.wit`
  and is exercised by `example_wit_text_handles` through opaque resource
  handles instead of raw `Text*`.
- The remaining cleanup is to extend that WIT barrier beyond `text` and keep
  pushing consumers onto the narrower targets.

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

### Family B: Same Core Logic with CLI and Filesystem

These prove the add-on nature of host services:

- file-driven batch processor
- command-line editor or transformer

### Family C: Native Desktop Variants

These prove optional host and render paths:

- direct Metal native sample
- Mac-collapsed AppKit sample
- larger tgfx-backed comparison sample

### Family D: Wasm Host Execution Samples

These should remain clearly on the host side:

- embedded guest execution
- WIT-backed service provision to the guest
- comparison points for interpreter vs. other future Wasm hosts

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

## Immediate Next Tasks

The next concrete implementation work should happen in this order:

1. Extend `common-core.wit` from `text` to `db`, `txn`, and `mailbox`.
2. Create the first Wasm-aligned common-core sample programs that use those
   generated interfaces in more than one world shape.
3. Read `docs/LAMBKIN_XPI_JOURNAL.md` before expanding host/editor surface area
   so the current join-point questions remain explicit.

## Resume Checklist For Future Sessions

When resuming this work in a new session:

1. Read this document first.
2. Read `README.md` for the current build and benchmark workflow.
3. Read `docs/EXAMPLE_MATRIX.md` to see the current modeled sample families.
4. Read `docs/LAMBKIN_XPI_JOURNAL.md` to pick up the current join-points,
   XPIs, and open research questions.
5. For any new code, decide whether it belongs to:
   common support, WIT bindings, or host/platform support.
6. If a change adds an OS dependency to a common target, stop and split the
   boundary instead.
7. If a new sample is added, state which world it models and which artifacts it
   should justify linking.

## Non-Goal Reminder

Croft is not trying to prove that these C programs are pleasant for humans to
author manually. The purpose is to prove that the runtime shapes are valid,
decomposable, and capable of producing small final binaries once Lambkin has
performed the higher-level analysis and selection work.
