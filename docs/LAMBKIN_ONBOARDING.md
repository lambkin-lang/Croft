# Lambkin Core Onboarding Guide

Welcome to the Croft integration guide for the Lambkin compiler project. The `croft_lambkin_core` module provides a fully freestanding, zero-dependency, and Wasm-aligned implementation of Croft's advanced data structures:

*   **Sapling:** MVCC B+ tree
*   **HAMT:** Hash Array Mapped Trie
*   **Seq:** Finger tree sequence
*   **Bept:** Big-Endian Patricia Trie
*   **Thatch:** Packed MVCC primitives
*   **Text:** Code-point sequence string library

## Consumption Format

The core is compiled into a single LLVM bitcode object: `croft_lambkin_core.bc`.
It is completely devoid of any `libc` dependencies, including `malloc`, `free`, `printf`, `assert`, and basic `<string.h>` routines like `memcmp` or `memcpy`. It is designed to be linked directly into the Lambkin compiler's generated LLVM modules without requiring any OS-level C runtime.

To link the structure, simply use `llvm-link`:
```bash
llvm-link your_compiler_output.bc croft_lambkin_core.bc -o final.bc
```

## ABI Mapping in Lambkin

The entire data payload revolves around opaque structures that the Lambkin frontend must map using MLIR/LLVM bindings. The core structures you must represent as opaque LLVM struct types are:

*   `SapEnv`: The overall environment and memory arena manager.
*   `SapTxnCtx`: The active MVCC transaction state.
*   `SapMemArena`: The freestanding, Wasm-aligned bump and node allocator.

Lambkin provides memory management for Croft by wrapping operations inside `sap_txn_begin()` and `sap_txn_commit()`.

## Initializing the Generic Memory Arena

Because `croft_lambkin_core` has severed its ties to system allocation, you must initialize `SapEnv` manually using the `SapMemArena` Wasm allocator.

```c
// 1. Give the arena a block of linear memory (from the Lambkin runtime or Wasm memory page)
uint32_t capacity = 1024 * 1024 * 64; // 64MB linear memory budget
void* memory_block = lambkin_runtime_request_pages(capacity);

// 2. Initialize the arena struct over the memory block
SapMemArena *arena = ...; 
sap_arena_init(arena, memory_block, capacity, 4096);

// 3. Create the Database Environment using the generic arena
SapEnv *env = sap_env_create(arena, 4096, 64);
```

## Implementing `__builtin_memcpy` and Friends
The bitcode relies on compiler intrinsics (e.g., `@llvm.memcpy.p0.p0.i32`). Lambkin's LLVM passes will automatically lower these to inline assembly or generic loops based on the target architecture. No external `libc` resolution is required. `memcmp` has been implemented natively as an inline utility.

## Testing and Fault Injection
To verify the engine from Lambkin's runtime, you can initialize the subsystem fault injectors (such as `sap_fi_should_fail`). However, in `LAMBKIN_CORE` mode, fault injection is stripped out to keep the bitcode footprint minimal. You can inject faults if you rebuild without `-DLAMBKIN_CORE`.
