# Lambkin Package System Prototype: "Larder"

## 1. The Larder Directory Format
A Larder is an additive package directory. Lambkin compilers consume the directory by reading the `.almanac` files to build a capability solve graph, and linking the corresponding `.bc` files along with weaving any `.ville` XPI rules.

```text
croft_core.larder/
├── core.almanac            # Umbrella capabilities of the package
├── sapling/
│   ├── sapling.almanac     # Capabilities, requires #txn, #allocator
│   ├── sapling_txn.ville   # Compiler XPI rules for atomic blocks
│   └── sapling.bc          # The pre-compiled LLVM bitcode slice
├── hamt/
│   ├── hamt.almanac        # Capabilities, requires #allocator
│   └── hamt.bc             # The pre-compiled LLVM bitcode slice
├── txn/
│   ├── txn.almanac         # Provides #transaction-manager
│   └── txn.bc
├── ryu64/
│   ├── ryu64.almanac       # Formatting capabilities, config variants
│   ├── ryu64_tiny.bc       # Bitcode slice WITHOUT bigint parsing
│   └── ryu64_full.bc       # Bitcode slice WITH bigint parsing
└── allocator/
    ├── arena.almanac       # Provides #allocator
    └── arena.bc
```

---

## 2. Almanac Mockups (Capabilities & Dependencies)

### `sapling.almanac`
The Sapling DB almanac exposes an ordered, persistent map. It obligates the user to provide a schema definition.

```javascript
-- sapling/sapling.almanac
package croft.sapling

// What this slice provides to resolving applications:
provides: 
  #db-map, 
  #ordered, 
  #persistent,
  #cursor (mix-ins: #seek, #dup-sort, #first, #last)

// What this slice requires other .bc files to provide:
requires: 
  #transaction-manager,
  #allocator,
  #host-vfs

// A build-time obligation: 
// The developer's app MUST tag a .wit file with #schema if they consume this capability.
obligation:
  input schema_wit = $require_tag(#schema)
  generator execute: "python3 tools/wit_schema_codegen.py ${schema_wit}"
  generator output: "${build_dir}/schema_generated.bc"

export:
  link: sapling.bc
  link: ${build_dir}/schema_generated.bc
```

### `hamt.almanac`
A purely functional, unordered hash map. Simpler dependency surface, no schema obligations.

```javascript
-- hamt/hamt.almanac
package croft.hamt

provides: 
  #db-map,
  #unordered,
  #ephemeral

requires:
  #transaction-manager,
  #allocator

export:
  link: hamt.bc
```

### `ryu64.almanac`
This showcases how Larder handles optional configuration without forcing exponential compile matrices. The package provides two mutually exclusive `.bc` slices depending on how aggressive the application's capabilities are.

```javascript
-- ryu64/ryu64.almanac
package croft.ryu64

// The basic formatting capability is always provided
provides: #float-to-string

export variant Tiny:
  // Provided if the application wants minimal memory footprint
  provides: #float-parsing-minimal (range: -19 to 19 sig figs)
  conflicts: #ryu-bigint
  link: ryu64_tiny.bc

export variant Full:
  // Fulfills the requirement if the application asks for full IEEE-754 coverage
  provides: #float-parsing-full
  requires_memory: 2KB // The solver knows to reject this if the target is an embedded micro-device
  link: ryu64_full.bc
```

---

## 3. Ville Mockup (The XPI Compiler Weaver)

### `sapling_txn.ville`
This `.ville` file operates at the MLIR/Build-time level. It instructs the Lambkin compiler on how to enforce the rules of a capability dynamically or statically. 

The user observed that Sapling DB transactions MUST happen within a bounded, restartable block. Here, we use XPI (Crosscut Programming Interfaces) to define an assertion that the compiler will weave into any AST node that calls `croft.sapling.put`.

```text
-- sapling/sapling_txn.ville
weaver: croft.sapling.txn_enforcement

// XPI Pointcut: Match any call to a function tagged with #db-write
pointcut db_writes = call(*::*(..)) where target has tag #db-write;

// XPI Advice: What the compiler must prove or inject around the call
around db_writes(ctx) {
  // Static MLIR Check
  // The compiler must prove the invocation site exists inside an `atomic { ... }` block
  assert_enclosing_scope(ctx, #lambkin-atomic-block) 
    else error("Sapling capability #db-write requires an enclosing atomic/transaction block.");

  // Because atomic blocks are restartable, ensure the state inside the block is bounded
  assert_no_allocations_escaping(ctx)
    else error("Memory cannot escape the atomic block boundary.");

  // Lowering: Proceed with the actual C ABI function call to the bitcode
  proceed(ctx);
}
```

### How the system consumes this:
1.  The user's app writes: `require: #db-map + #ordered`.
2.  The Almanac solver selects `sapling.almanac`.
3.  The build directory links `sapling.bc` and its required `txn.bc`.
4.  The compiler ingests `sapling_txn.ville` and statically scans the user's `.lamb` source code. It validates that every `DB.put(...)` call exists inside an `atomic` keyword block. If it doesn't, the compilation fails with an aspect-woven error message.

---

## 4. XPI Weaving as Structural Synthesis

Instead of forcing library authors to implement combinations like a `LinkedHashMap`, the solver can intercept capabilities and synthesize them dynamically out of orthogonal primitives.

If an application declares:
```text
require: #db-map + #unordered + #fifo-iteration
```

The Lambkin compiler synthesizes this constraint via XPI:
1. **Selection:** The Almanac solver matches `#db-map + #unordered` to `hamt.bc` and `#fifo-iteration` to `seq.bc` (Finger Tree).
2. **State Weaving:** A `.ville` recipe intercepts the user's Map allocation, injecting a Finger Tree instance inside the Map state automatically.
3. **Execution Weaving:**
```text
-- hamt_fifo_synthesis.ville
weaver: croft.core.synthesis

around db_insert(ctx, key, value) target #hamt {
  // 1. Invoke the HAMT implementation
  let new_hamt = proceed(ctx, key, value);
  
  // 2. Weave the sequence update: track the insertion order
  let new_seq = seq_push_back(ctx.linked_seq, key);
  
  return (new_hamt, new_seq); // The synthesized ABI tuple
}
```
4. **Resolution:** The user iterates over what they think is a "Linked HAMT." In reality, the compiler provides the Finger Tree's iterator, returning keys in strict FIFO order, perfectly solving the architectural "diamond" or "matrix" explosion without any `LinkedHamt.c` having to be manually programmed by the Croft author.

---

## 5. Scaling Dimensions: Mutability vs. Immutability

Because Lambkin targets both resource-constrained embedded Wasm (where mutable, imperative data structures win on performance and memory footprint) and scaled-up multi-threaded environments (where functional immutability prevents data races), the Larder format must be able to mix-in mutability domains. 

The system handles this through two distinct architectural paths depending on the exact semantic shift:

### Path A: Compiler-Enforced "Views" (The XPI Approach)
If the underlying C structural implementation inherently supports Copy-On-Write (like `sapling.c` or `hamt.c`), you do *not* need to compile and link two separate `.bc` files. The separation is purely cognitive and compiler-enforced.

- **The Setup**: The program links only the standard `hamt.bc`.
- **Domain 1 (Mutable/Embedded)**: The compiler's `.ville` weaver asserts that the reference has *Linear Ownership* (only one active reference exists). It allows the Lambkin code to mutate the `hamt` in place (from the host's perspective, this means just ignoring the returned new root and overwriting the current pointer contextually).
- **Domain 2 (Immutable/Multi-threaded)**: The compiler's `.ville` weaver detects that the `hamt` reference is being passed across a thread boundary. It statically strips the `#mutable` capability from the reference, projecting a `#read-only` view. If Domain 2 attempts to call `hamt_put`, the XPI aspect compiler intercepts it and emits a compilation error: `"Cannot implicitly mutate an immutable view. Explicit clone required."`

**Why this works**: The underlying Wasm/Native code is the exact same. The compiler simply uses aspects to strip the ABI capability in Domain 2 while preserving it in Domain 1.

### Path B: Linking Distinct Artifacts (The Implementation Divergence Approach)
If the required performance profiles demand fundamentally different underlying memory architectures, compiler views aren't enough. The library author must provide two `.bc` slices.

- **Example**: `seq.c` (Finger Tree). As implemented today in Croft, `seq.c` actively transfers ownership and calls `sap_arena_free_node` on consumed interior nodes. It is imperatively destructive by design for performance.
- If Domain 2 (Multi-threaded) asks for `#list` + `#immutable` + `#thread-safe`, the current `seq.c` *cannot* fulfill this safely because it actively destroys old roots. 
- **The Setup**: The `seq.almanac` would need to offer two `.bc` slices: 
  1. `seq_linear_fast.bc` (current implementation, fulfills `#mutable + #fast`).
  2. `seq_persistent_gc.bc` (a version compiled with a `#persistent` XPI macro that relies on a tracing garbage collector or atomic ref-counts instead of immediate `free()`, fulfilling `#immutable + #thread-safe`).

The Almanac solver is smart enough to detect that Domain 1 needs `seq_linear_fast.bc` and Domain 2 needs `seq_persistent_gc.bc`, and will link *both* into the binary, strictly segregating which execution domain gets which pointer.

---

## 6. Structural Mix-ins: The Thatch (Gibbon) Model

Thatch implements a "Gibbon-style" packed tree over linear memory. Unlike `sapling` or `hamt`, it is categorically hostile to in-place mutation and traditional tree rotations because it represents ASTs as a continuous array of bytes with `skip_len` lookahead markers.

However, precisely because it is purely linear contiguous memory, the Larder system can weave fascinating cross-cutting mix-ins into it without changing the `thatch.c` structure:

### Mix-in 1: Memory Provenance (Ephemeral vs. Transactional)
By default, Thatch regions (`ThatchRegion`) are allocated out of the `SapMemArena` inside an active transaction. 
But Lambkin can mix this out. If an app needs to parse a tiny JSON payload and throw it away immediately, resolving `#thatch + #ephemeral` instructs Lambkin to map the `thatch_region_new()` call not to the DB arena, but directly to a Wasm local stack pointer or a bump-allocator, creating a zero-DB-footprint AST parser.

### Mix-in 2: Trust vs. Validation Fences
Because Thatch allows `#zero-deserialize` cursor traversals directly over bytes, it relies heavily on the data being well-formed.
- If Thatch data is generated internally by the compiler, the app resolves `#thatch + #trusted`. The `.ville` weaver emits bare pointer math for `thatch_read_tag`.
- If the data is from a network socket, the app resolves `#thatch + #untrusted`. The compiler's weaver intercepts the Thatch read calls and weaves `#bounds-checked` validation fences before every memory read.

### Mix-in 3: Indexing over Packed Data
This is the most powerful synthesis. Because Thatch uses lookahead nodes to jump over subtrees, randomly accessing `#thatch` data is $O(N)$.
If the user resolves `#thatch + #indexed-query`, the Lambkin solver does not change `thatch.c`. Instead, it links `thatch.bc` alongside `hamt.bc`. The `.ville` weaver intercepts the Thatch JSON/AST traversal and dynamically maintains a side-band HAMT map, where the keys are JSON-Pointers (e.g., `"/user/name"`) and the values are the `ThatchCursor` integer locations inside the raw byte array.

The user asks for a simple, queryable JSON tree, and the compiler fulfills it by symbiotically weaving the linear speed of Gibbon with the $O(1)$ lookup guarantees of a HAMT, projecting them unified interface to the programmer.

---

## 7. Type Parameterization Strategy (Generics vs. Erasure)

In traditional systems languages, type parameters (Generics) force the designer to make a hard, permanent choice between **Monomorphization Bloat** (Rust/C++) or **Type Erasure Indirection Overhead** (Java/Go).

Because Lambkin relies on an algebraic solver rather than explicit generic instantiation, "Type Parameterization Strategy" is treated as just another application dimension that can be mixed in or out. The Lambkin developer simply writes `var db: sapling<UserRecord>`, and the solver decides how that type physically manifests in the LLVM bitcode:

### Strategy 1: The Wasm-Aligned Uniform Representation (Erasure)
Because WebAssembly is optimized for opaque `anyref` handles (and Croft relies heavily on `uint32_t` handles for nodes), a library author can provide a single, pre-compiled slice: `sapling_erased.bc`. 

- **The Resolution**: If the application requires `#small-binary` constraints (e.g., for an embedded Wasm target with strict footprint limits), the almanac solver links `sapling_erased.bc`.
- **The Weave**: The `.ville` component intercepts all `get`/`put` calls to the DB to automatically weave in the type-safe casts and box/unbox instructions at the call site. The binary stays tiny because there is only one map implementation in memory.

### Strategy 2: The Monomorphized Generator (Value Types)
If the type parameter is a tightly packed 12-byte structural record (like a `Vector3` in a hot render loop), shoving it into an opaque handle adds unacceptable pointer indirection and ruins CPU cache locality.

- **The Resolution**: If the Lambkin app resolving `sapling<Vector3>` prioritizes `#fast-execution` or `#cache-locality`, the `.almanac` solver ignores the pre-compiled `sapling_erased.bc`. 
- **The Weave**: The solver fulfills the build obligation by invoking a code-generation aspect (or a macro-expanded LLVM IR pass) to emit a bespoke `sapling_Vector3.bc` slice perfectly scaled to that 12-byte payload, avoiding all indirection.

By piggybacking generics directly onto the Larder solver, the source code remains completely oblivious to whether a pointer is boxed or unboxed, gracefully scaling from extreme embedded minimalism to bare-metal native performance based purely on the application's top-level capability requirements.
