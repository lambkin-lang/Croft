# Lambkin XPI Journal

This journal captures architectural observations from Croft while Lambkin and
its aspect libraries are still co-evolving. These notes are not intended to
freeze the design; they are meant to preserve the useful pressure points,
surprises, and open questions that surfaced while forcing the ideas into code.

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

The first host mix-in package exposed a related pressure point: generated C
names are still effectively global at the package boundary. A second package
cannot casually reuse generic type names like `status`, `bytes`, or `error`
without risking collisions. For now, the host-fs schema uses explicit `fs-`
prefixes to stay linkable beside `common-core`, but that is only a stopgap.

This opens another likely XPI/codegen join-point:

- package-aware naming and namespacing strategy
- generated symbol hygiene across independently evolved WIT packages
- what should be made globally canonical versus package-local

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
10. Should WIT package names participate directly in generated C symbol names,
    or should Lambkin weave a separate symbol-hygiene layer around codegen?
11. Which host capabilities are stable enough to present as reusable mix-ins
    (`fs`, `clock`) versus which should stay family-specific (`window`, `gpu`)?

## Near-Term Follow-Through

The next high-value steps look like this:

- reuse the current `text`/`db`/`txn`/`mailbox` WIT barrier in a CLI-style
  sample and a host-Wasm-facing sample
- define the next host mix-in WIT package after `host-fs`, likely `clock`
- isolate direct-Metal editor concerns into explicit layout/input/accessibility
  seams instead of letting them accumulate inside one renderer-centric module
- start naming candidate Lambkin aspect libraries from the join-points above,
  even if their final surface is still tentative
