# Lambkin XPI Journal

This journal captures architectural observations from Croft while Lambkin and
its aspect libraries are still co-evolving. These notes are not intended to
freeze the design; they are meant to preserve the useful pressure points,
surprises, and open questions that surfaced while forcing the ideas into code.

## March 6, 2026: Current Assessment After The First Real World Family Pass

The repository now supports several conclusions strongly enough that they
should guide future work.

### What Has Been Proven

- The common-side versus host-side split is no longer aspirational. The build
  graph now reflects it directly.
- The WIT barrier is alive on both sides of the boundary: common resources and
  host mix-ins are both being exercised by model programs.
- The same common-side text logic now survives three world shapes:
  CLI, host-Wasm, and native window/GPU.
- The direct-Metal path demonstrated that small native GPU binaries are
  possible on macOS without inheriting the full `tgfx` Metal payload.

The most useful point here is not that the APIs are elegant. They are not meant
to be. The point is that the repository now contains convincing generated-style
evidence that Lambkin can hold common logic stable while swapping out host
families and platform couplings.

### What Was Learned

- `tgfx` Metal should be treated as a comparison control, not the likely
  destination for the small-binary path.
- The direct-Metal editor is valuable precisely because it does *not* hide the
  hard seams AppKit hides for free.
- Host-Wasm is now informative as a family member in its own right. For the
  current tiny sample, the `wasm3` interpreter bridge costs more than the
  native direct-Metal window/GPU mix-ins.
- The biggest current risk is premature convenience abstraction: it would be
  easy to erase the differences that Lambkin needs to reason about.

### What Should Happen Next

- Push menu, accessibility, clipboard, and richer editor input through WIT
  host mix-ins.
- Move more of the direct-Metal/editor path off direct host calls and onto
  mix-in boundaries.
- Keep using "same logic, different world" samples as the main proof style.
- Add runtime-performance benchmarking alongside size benchmarking for the main
  family comparisons.

## March 6, 2026: Richer Host Mix-Ins, WIT-Routed Direct-Metal Editor, And Codegen Manifests

This pass pushed the host-side WIT work from small samples into the first
serious editor-shaped family member.

### What Was Added

- New host mix-in packages now exist for `menu`, `clipboard`,
  `editor-input`, and `a11y`.
- `example_editor_text_metal_native` now routes its host control plane through
  WIT-facing runtimes for window, clock, menu, clipboard, normalized editor
  input, and accessibility.
- `tools/wit_codegen.c` now emits rename/trace manifests per schema and
  normalizes exact-tail helper macros such as the `window` invalid-resource
  macro instead of preserving a visibly stuttered form.

### What This Clarified

- The direct-Metal editor no longer hides most of its host policy in one local
  shell. Rendering is still direct, but the control path is now explicit
  enough to study as a family boundary.
- Callback-to-queue normalization is now obviously one of the main XPIs in the
  system. `host-window` and `host-editor-input` are not just wrappers; they
  are policy surfaces for event ordering, shortcut translation, and the split
  between raw device signals and editor intent.
- Accessibility is not just a host service and not just a resource family. It
  also needs a bridge layer because scene nodes and host accessibility objects
  do not share one ownership model.
- The code generator is now responsible for enough architectural hygiene that
  its output should be treated as part of the substrate, not as a disposable
  convenience tool.

### New Cost Signal

The optimized direct-Metal editor datapoint moved from roughly `110 KB` to
`148,944` bytes after routing the control plane through WIT-facing runtimes.

That increase is useful evidence, not a failure:

- explicit host seams are not free
- they are still much cheaper than the tgfx/Metal editor family
- Lambkin will eventually need this kind of tradeoff data to decide when a
  reusable mix-in world is worth its extra machinery versus when a collapsed
  native world is the better fit

### Updated Questions

1. Which editor-facing host seams should be standardized as reusable mix-ins
   now: menu, clipboard, accessibility, command/input normalization, IME,
   selection affinity, find/replace, drag-and-drop?
2. Which of those should remain deliberately collapsed for the direct-Metal
   family so the small-binary path does not inherit infrastructure too early?
3. Should render commands themselves eventually cross WIT, or should the
   direct-Metal family stay collapsed there while Lambkin reasons about higher
   control-plane seams?
4. How should the new rename/trace manifests feed Lambkin: debugging aid only,
   or actual solver/codegen metadata?
5. When an interface like editor input is really an intent-normalization layer,
   should Lambkin model that as reusable generated code, reusable runtime
   advice, or family-specific XPI libraries?

## March 7, 2026: Reusing Editor-Facing Mix-Ins Outside The Scene Editor

This pass took the first step that the previous notes said was needed:
`host-menu`, `host-clipboard`, and `host-editor-input` are no longer only
proving themselves inside the direct-Metal scene editor.

### What Was Added

- `example_wit_textpad_window` now reuses the common `text` resource plus
  `host-window`, `host-gpu2d`, `host-clock`, `host-menu`,
  `host-clipboard`, and `host-editor-input`.
- `tools/benchmark_runtime_perf.sh` now exists alongside the size benchmark and
  records repeated runtime measurements plus any emitted `frames=` telemetry
  for the non-GUI examples.
- The direct-Metal scene editor now emits simple frame telemetry too, but on
  this macOS host the GUI family still has to be exercised as direct top-level
  terminal commands rather than through a wrapped shell harness.

### What This Clarified

- The newer editor-facing mix-ins are reusable enough to support a smaller,
  non-scene family member. That is good evidence that they are not purely
  scene-editor scaffolding.
- The painful part moved again: the runtime harness now drives the current
  auto-close-capable GUI families directly, so the interesting comparison is no
  longer "can they be launched cleanly?" but "what do they cost on the same
  document and the same timed window?"
- That still leaves a real host seam around application lifecycle, activation
  policy, and automated runtime control for native GUI worlds, but it is now a
  runtime-comparison concern instead of a hard blocker.

### New Risk Signal

`tools/benchmark_runtime_perf.sh` now drives the current windowed GUI samples
directly when they expose auto-close support, and
`tools/benchmark_editor_runtime.sh` adds a shared-document runtime comparison
for the three editor families. The new risk is no longer timeout handling. It
is whether Croft is comparing runtime on honest shared workloads often enough
to inform product-line choices.

### Updated Questions

1. Should GUI runtime control and auto-close be modeled as another host mix-in
   or as harness-only advice around native worlds?
2. Is AppKit activation/menu lifecycle a reusable host service, or is it one of
   the places where Lambkin should prefer collapsed native advice?
3. How should performance benchmarking distinguish between "sample logic is
   slow" and "host lifecycle automation is incomplete"?

## March 6, 2026: First Honest Common-Core Split

This pass made two boundary claims true in code instead of only in prose:

- `sapling_core` is now a real common-side artifact built only from
  `src/sapling/*.c`.
- the first WIT/resource barrier now exists as `schemas/wit/common-core.wit`
  plus the generated/common-runtime paths behind `croft_wit_text_runtime`,
  `croft_wit_store_runtime`, and `croft_wit_mailbox_runtime`.

The immediate consequence is useful: the repository now contains both a
hand-written common-side sample (`example_sapling_text`) and generated-style
samples (`example_wit_text_handles`, `example_wit_db_kv`,
`example_wit_mailbox_ping`) over the same underlying common-side substrate.

That difference matters. The latter is closer to the kind of awkward, precise,
handle-oriented C that Lambkin can generate after the SAT/refinement solver has
already chosen the right splice.

## Architectural Pressure Points

### 1. WIT Packages Need Shared Support, Not Just Shared Syntax

The first attempt at introducing another WIT package exposed a duplicate-symbol
problem: each generated schema emitted its own global `sap_wit_skip_value`.
That was not just a build bug. It exposed an important design fact:

- once multiple WIT packages live in one final program,
- some support functions belong to the WIT substrate itself,
- and therefore must not be generated independently per schema.

This is likely the first of several crosscutting "schema support" join-points.
Other likely candidates:

- resource table helpers
- ownership/borrow checks
- list/stream adapters
- error translation tables
- version guards and compatibility shims

This is a strong argument for a Lambkin-side aspect library that can weave
package-independent support around generated interfaces without pretending the
interfaces are independent.

That pressure point is now partially resolved in code: `tools/wit_codegen.c`
now uses the WIT `package` name as the generated C qualifier source, sanitizes
C identifiers centrally, and emits traceability comments in generated headers
and sources. That shifts symbol hygiene from schema-local naming folklore into
one place where Lambkin can later extend it deliberately.

This opens another likely XPI/codegen join-point:

- package-aware naming refinement
- generated symbol hygiene across independently evolved WIT packages
- what should be made globally canonical versus package-local
- how generated names should collapse duplicated stems like
  `HostFsFsCommand` or `ResultTestTestResultCarrier`

The first `host-window` package exposed a second generator hygiene issue: WIT
case names also need C-keyword awareness. A case named `char` generated illegal
C until it was renamed to `char-event`. That is another reminder that the WIT
surface and the generated-C surface are not independent concerns.

That pressure point is now mostly resolved in code:

- package-tail normalization now collapses the duplicated-stem cases we
  actually hit (`HostFsFsCommand` -> `HostFsCommand`,
  `HostClockClockReply` -> `HostClockReply`,
  `HostWindowWindowCommand` -> `HostWindowCommand`,
  `ResultTestTestResultCarrier` -> `ResultTestResultCarrier`)
- exact-tail helper names are now normalized too
  (`SAP_WIT_HOST_WINDOW_WINDOW_RESOURCE_INVALID` ->
  `SAP_WIT_HOST_WINDOW_RESOURCE_INVALID`) and recorded in the generated
  rename/trace manifests
- the remaining question is narrower and more interesting:
  how much further should helper-name normalization go before it starts hiding
  package provenance that Lambkin may want to observe directly?

### 2. Resource Handles Are the First Honest XPI Boundary

`common-core.wit` currently defines:

- `resource text`
- `resource db`
- `resource txn`
- `resource mailbox`

All four are now implemented as model runtimes. The model C program is no
longer passed a `Text*`, `DB*`, or `Txn*`; it sees opaque resource IDs and
constructs WIT-shaped commands.

That means the real join-point is no longer "call this function on a pointer".
The join-point becomes:

- command construction
- handle lifetime
- error mapping
- ownership of returned buffers
- which world is allowed to name which resource at all

This is much closer to XPIs than to a conventional portable C API.

### 2a. Transaction Lifetimes Are Their Own Join-Point

Once `db` and `txn` crossed the barrier, another seam became obvious:

- opening a database is not the same concern as beginning a transaction
- transaction commit/abort policy is not the same concern as key/value access
- returning values across the barrier forces an ownership decision every time

That suggests at least two XPIs, not one:

- datastore/resource lifetime
- transactional visibility and commit policy

The current runtime chooses the conservative shape:

- `db` and `txn` are distinct resources
- values crossing the barrier are copied into owned buffers
- dropping a `db` while `txn` handles are live returns `busy`

Those are good modeling defaults because they keep future remote or distributed
implementations plausible.

### 2b. Mailbox Semantics Must Stay Above the Common-Core Floor

Implementing `resource mailbox` exposed a different kind of join-point:

- should receive block or poll?
- where do timeouts live?
- who owns scheduling or wakeups?
- should dropping a mailbox discard queued data or reject while busy?

The current runtime answers those by *not* pretending to solve them in the
common layer:

- `recv` is nonblocking and returns `empty`
- send/receive copy payloads explicitly
- `drop` returns `busy` while messages remain queued

This is deliberate. Blocking, sleeping, or worker wakeups belong in higher
worlds and likely in host mix-in interfaces such as clock/scheduler services.

### 2c. Shared Common Logic Can Stay Stable Across Host Worlds

The new `croft_wit_text_program` helper is deliberately small, but it proves an
important point: the same handle-oriented text logic can now run in at least
two host worlds without changing its internal sequencing at all.

- `example_wit_text_cli` uses it with `text` plus `host-fs`
- `example_wit_text_wasm_host` uses it with `text` plus the current `wasm3`
  host executor boundary
- `example_wit_text_window` uses it with `text` plus `host-window`,
  `host-gpu2d`, and `host-clock`

That is not the end-state API. It is a model of what machine-generated Lambkin
code can look like once the solver has already decided which mix-ins are
present. The useful observation is that the host family can vary while the
common-side command choreography stays fixed.

The current optimized datapoints for that shared-logic family are also useful:

- `example_wit_text_cli`: `53,416`
- `example_wit_text_wasm_host`: `123,288`
- `example_wit_gpu_canvas`: `90,456`
- `example_wit_text_window`: `109,208`

So the host-Wasm shape is already informative: the common-side logic remains
stable, but the interpreter/guest bridge is currently more expensive than the
direct-Metal window/GPU mix-ins for this specific tiny workload.

### 3. The Common/Data Layer and the Host Layer Diverge Earlier Than Usual

It was not enough to split UI or filesystems away from the editor. The more
important split was earlier:

- `sapling_core`
- `sapling_runner_core`
- `sapling_runner_host`
- `sapling_wasi_runtime`
- `sapling_wasi_host`

That is a sign that the eventual Lambkin aspect libraries should expect to
advise over *families of artifacts* and not just methods on one object graph.
Some advice will need to add or remove entire runtime services, not merely
decorate a function call.

### 4. Host Mix-Ins Need Handle Translation Policies Too

`host_fs` already carried a hidden warning in its C header: its `uint64_t`
"handle" is really just a native pointer cast to an integer. The new
`host-fs.wit` package confirms that these host facades are not trivial wrappers.

The real join-points are:

- translation from unsafe native handles to opaque resource IDs
- ownership of buffers returned by host reads
- cwd versus absolute-path policy
- whether repeated reads imply seek/reset capability or one-shot semantics

That means even "simple" host mix-ins are already policy surfaces, not just ABI
adapters.

### 5. Not Every Mix-In Should Be Forced Into `resource`

The first `host-clock` package validated the opposite side of the same idea:
some host capabilities are pure service queries, not owned-state handles.

That matters because it is easy to overfit the early WIT work around resources
just because `text`, `db`, `txn`, `mailbox`, and `file` all need lifetime
management. `host_time_millis()` does not.

So the current split is now explicit:

- stateful boundaries: `text`, `db`, `txn`, `mailbox`, `file`
- stateless service boundaries: `clock`

That is likely to matter later for other mix-ins too:

- accessibility may be partly resource-shaped and partly service-shaped
- menu construction may create handles, but command routing may be service-like
- GPU may split between surface/resource ownership and stateless capability
  queries

### 6. Callback Hosts and Polled Worlds Need an Explicit Bridge

`host_ui` currently exposes a singleton window plus global callbacks. The new
`host-window` package deliberately does *not* pretend that is already a clean
WIT boundary. Instead it wraps that host in:

- a `window` resource
- a polled `next-event` command
- an internal queue that captures callback traffic

That bridge is itself a crosscutting concern:

- event ordering and queue overflow policy
- callback-to-command translation
- singleton host state versus resource multiplicity
- whether cursor positions, modifiers, and future IME state are sampled or
  pushed

This looks exactly like the kind of place where Lambkin advice or XPIs will
need to choose among several legitimate strategies rather than inheriting one
hard-coded runtime shape.

### 7. GPU Boundaries Split Capability Queries from Resource Ownership

The first `host-gpu2d` package confirmed an earlier suspicion: GPU mix-ins are
not cleanly "all resource" or "all service".

The current shape now has both:

- stateless capability queries (`capabilities`)
- a stateful `surface` resource with explicit `open`, `begin-frame`, and `drop`

That split is already useful, but it also exposed a deeper join-point:

- the current native direct-Metal host does not really own an independent
  surface object
- it owns one render target implicitly attached to the current window
- yet the WIT boundary models `window` and `surface` as distinct resources

That mismatch is worth keeping visible. It is exactly the kind of open
implementation seam Lambkin will need to advise over differently in other
families:

- web/worker worlds may synthesize surfaces differently
- AppKit-like worlds may collapse window and drawing surface together
- more portable GPU worlds may need a stricter swapchain/present model

## Likely Join-Points / XPIs

The following are likely to become first-class XPIs or equivalent aspect
libraries.

### Resource Lifetime XPI

Concern:

- create
- clone
- destroy
- aliasing
- move/borrow semantics
- escape analysis interplay

Why it matters:

- WIT resources expose opaque IDs, but the runtime still needs policies for
  slot reuse, generations, invalidation, and eventual remote/distributed
  implementations.

### Text Mutation XPI

Concern:

- replace
- insert
- delete
- coalescing boundaries
- undo grouping
- command intent versus concrete mutation

Why it matters:

- AppKit, direct-Metal, CLI, and web variants will all reach the text model
  through different gesture/input/event vocabularies, yet may still want to
  share mutation policy and undo semantics.

### Presentation Mapping XPI

Concern:

- command bindings
- menus
- keybindings
- toolbar/widgets
- drag-and-drop
- file-open flows

Why it matters:

- the same domain declaration could plausibly generate a CLI parser, a web
  upload surface, or a macOS menu/window workflow.
- the join-point is not only "when command runs", but also "how command becomes
  present in a target world".

### Editor Interaction XPI

Concern:

- caret policy
- selection policy
- hit-testing
- scroll anchoring
- text layout invalidation
- accessibility tree updates

Why it matters:

- these concerns cut across renderer, input, document model, and accessibility.
- they are not naturally encapsulated by one module in either the AppKit or
  direct-Metal families.

### Runner Policy XPI

Concern:

- retry policy
- dedupe policy
- dead-letter policy
- scheduling/sleep
- clocks
- replay hooks

Why it matters:

- Croft already separates runner protocol from runner host shell, but the
  policy hooks are still scattered across multiple helper layers.
- this is a good candidate for "advice over a protocol family" rather than one
  monolithic API.

## AppKit vs Direct-Metal Pain Points

The AppKit and direct-Metal editors are useful precisely because they do not
hide the same pain in the same place.

### What AppKit Gives Away "For Free"

`NSTextView` quietly absorbs:

- IME/composition behavior
- rich selection affinity rules
- native undo-manager semantics
- text input services
- accessibility behavior
- find/replace integration

In Croft, those are not "free". They are just compressed into a host-side
component we did not have to reimplement yet.

### What Direct-Metal Keeps Honest

The direct-Metal editor keeps the binary small, but it surfaces hard questions:

- who owns text shaping?
- where does selection geometry live?
- how is caret blinking timed?
- how are IME composition spans represented?
- which layer owns accessibility objects?
- should line breaking be renderer-driven, scene-driven, or document-driven?

These are not arguments against direct Metal. They are arguments for making the
crosscutting seams explicit instead of pretending a "renderer abstraction"
solves them.

## Research Questions Opened By This Pass

1. Which WIT support utilities should be globally shared substrate code instead
   of per-package codegen output?
2. Should resource handles carry generations in the C ABI, or should Lambkin
   treat generation checks as a separately woven safety aspect?
3. Is undo/coalescing best modeled as document advice, command advice, or both?
4. Which editor concerns should remain family-specific rather than being pushed
   into a universal editor interface?
5. Can the same declarative command surface generate CLI, web, and desktop
   presentations without over-constraining the host families?
6. How much of AppKit-like text behavior should be considered required
   capability versus optional mix-in advice?
7. Should the runner policy layer be represented as WIT-visible services,
   Lambkin-only advice, or a hybrid?
8. Should mailbox blocking/timeout behavior be modeled as a host mix-in XPI,
   command advice, or a separate scheduler resource family?
9. Which transaction policies should be explicit in WIT worlds versus woven by
   Lambkin as advice around command sequences?
10. Now that duplicated leading stems are mostly normalized, how much further
    should Lambkin/codegen go for exact-tail items and helper macros without
    hiding the source package provenance?
11. Which host capabilities are stable enough to present as reusable mix-ins
    (`fs`, `clock`) versus which should stay family-specific (`window`, `gpu`)?
12. Which future host interfaces should deliberately *avoid* `resource` and
    remain pure command/service surfaces?
13. When a native host is callback-driven but the target world wants polling,
    should the bridge live in generated code, runtime advice, or a reusable XPI
    library?
14. Which editor host concerns should be standardized as reusable mix-ins
    (`clipboard`, `menu`, `accessibility`, `input-method`) versus kept as
    family-specific collapsed integrations?
15. At what point should Lambkin deliberately choose a collapsed native world
    over a reusable mix-in world, even when that reduces portability?
16. How should performance benchmarking and responsiveness join the existing
    size-based decision process for world selection?

## Near-Term Follow-Through

The next high-value steps look like this:

- reuse the newer `host-menu`, `host-clipboard`, `host-editor-input`, and
  `host-a11y` mix-ins in more than one family so they stop being editor-only
  experiments
- use the now-stable GUI runtime automation to compare editor and WIT window
  families on honest shared workloads instead of only on launch survival
- move more of the direct-Metal/editor host interaction onto WIT-facing
  boundaries where that clarifies family differences, but keep rendering itself
  deliberately direct for now
- keep `tgfx` Metal as a comparison control while treating the native
  direct-Metal path as the current small-binary reference path
- isolate direct-Metal editor concerns into explicit layout/input/accessibility
  seams instead of letting them accumulate inside one renderer-centric module
- decide how much further exact-tail helper naming should be normalized now
  that codegen already emits rename/trace manifests
- add runtime-performance comparisons next to the size comparisons for the main
  world-family samples
- start naming candidate Lambkin aspect libraries from the join-points above,
  even if their final surface is still tentative
