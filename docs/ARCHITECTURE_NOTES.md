# Architectural Notes: GC-Gibbon, LSM-Trees, and Linear Memory

This document tracks architectural insights regarding the Wasm text editor backend and Concrete Syntax Tree (CST) representation.

## Core Insight: Decoupling Read vs. Write Workloads
The core architectural insight is applying an LSM-tree (Log-Structured Merge-tree) philosophy directly to Wasm's linear memory. This is achieved by decoupling **read-optimized structural data** from **write-optimized state**. 

By using a mutable `Seq` finger tree to absorb real-time text edits and a dense, GC-Gibbon-style contiguous byte array for the base CST, we avoid traditional heap fragmentation and pointer-chasing. Because Wasm pointers are simply `u32` offsets, a 'smart cursor' achieves maximum CPU cache locality by sequentially scanning the dense base array, temporarily branching into a sparse HAMT overlay only to resolve active, uncompacted transaction patches.

---

## Architecture Components: Base, Overlay, and Text

To make transactional overlays work over a GC-Gibbon-style dense tree, the base CST is treated as an immutable snapshot, while handling active edits in separate, sparse data structures.

### 1. The Text Layer: `Seq` Finger Tree
The mutable `Seq` finger tree acts as the text buffer engine.
- Because finger trees provide $O(1)$ access to the ends and $O(\log n)$ splitting/concatenation, it acts as a highly optimized Rope or Piece Table.
- When a keystroke occurs, the `Seq` tree is locally mutated.
- This mutation generates an edit span (e.g., "inserted 1 character at offset 1024").

### 2. The Overlay Layer: Sparse Patches
Instead of shifting the contiguous bytes of the dense Gibbon CST to accommodate new text, the edit span triggers the creation of an overlay patch.
- **The Structure:** A Hash Array Mapped Trie (HAMT) mapping the original Gibbon `u32` linear memory offsets (cursors) to localized patch nodes.
- **The Patch Node:** These represent the "transactional overlay." They can be traditional pointer-based structs or pointers to newly bump-allocated dense chunks at the end of the Wasm linear memory.

### 3. The Resolution Layer: Intercepting the Cursor
For read operations (syntax highlighting, incremental compilation), the system uses a smart cursor.
- As the cursor scans sequentially through the dense Gibbon base array (yielding maximum CPU cache locality), it continuously checks the overlay HAMT.
- If the cursor's current offset exists in the HAMT, the traversal logic temporarily jumps into the overlay patch, processes the updated nodes, and then seamlessly resumes scanning the dense base where the patch ends.

---

## The Compaction Cycle (Garbage Collection)

Because Wasm allows mutation, the system has flexibility in managing when and how overlays are resolved, preventing the HAMT from growing indefinitely and degrading $O(1)$ sequential scan times.

1. **Active Typing:** The user types rapidly. The `Seq` finger tree absorbs the text mutations. The parser quickly appends patches to the overlay HAMT. The dense base remains entirely untouched.
2. **Idle Time / Background Worker:** Once the user pauses (or concurrently in a Web Worker/Lambkin background thread), a compaction phase triggers.
3. **Re-serialization:** The system walks the smart cursor through the `base + overlays`, writing a brand new, fully contiguous, dense GC-Gibbon byte array to a fresh region of linear memory.
4. **Pointer Swap:** The root pointer is swapped to the new dense array, and the old region (along with the overlay patches) is freed or marked for region-level garbage collection.

---

## Synergy with WebAssembly

This architecture exploits exactly what Wasm excels at:
- A "cursor" or "pointer" is just an integer offset.
- Because the dense base requires no complex pointer chasing, it avoids the overhead of crossing the Wasm-to-JS boundary or misusing the host's generic garbage collector.
- Absolute control over the memory layout keeps the active working set incredibly dense and cache-friendly, while isolating mutations to a strictly bounded overlay namespace.

---

## WASIX Integration & Open Implementation Tradeoffs

As Croft aims to be a robust host environment for Portable C11 and Wasm applications, there is strong potential for hosting [WASIX](https://wasix.org/) applications. WASIX extends the WASI standard to provide full POSIX compatibility (including pthreads, sockets, fork/exec, etc.).

However, Croft and Lambkin are fundamentally designed around **Open Implementation** ("Beyond the Black Box" by Gregor Kiczales) and **Synthesizing Objects** (Czarnecki and Eisenecker). This means Lambkin targets a highly modular layering system that allows for radical architectural tradeoffs depending on the deployment target:
1. **Scale-Up (WASIX)**: Hosting full WASIX environments to run complex, server-like, or Node.js-style applications seamlessly within the text editor's spatial interface.
2. **Scale-Down (IoT)**: Generating tiny, universal Wasm targets or minimal native binaries explicitly designed for resource-constrained IoT devices.

This dual nature ensures the environment can scale from analyzing massive cloud backend codebases down to deploying tightly packed, deterministic logic to microcontrollers, relying heavily on the customizable data representations of Sapling and Lambkin.

---

## Makepad Architectural Synergy

[Makepad](https://github.com/makepad/makepad) is an AI-accelerated application development platform for Rust targeting Wasm/WebGL and native graphics APIs (Metal, DX11, OpenGL). It serves as another primary architectural reference (MIT Licensed) for Croft, offering several overlapping synergies:

1. **GPU-Accelerated 2D/3D Rendering**: Makepad utilizes raw native rendering APIs rather than traditional OS windowing widgets, mapping closely to Croft's use of `tgfx` (hardware-accelerated 2D drawing) and custom `scene_node` graph projections.
2. **Scriptable UI DSL**: Makepad employs a live-editable UI DSL and runtime script integration. Croft conceptually aligns with this via the Lambkin Wasm engine, utilizing WIT (Wasm Interface Types) for dynamic, sandboxed UI scripting and logic updates without stalling the main render thread.
3. **Data-Oriented Text Management**: Unlike Monaco's explicit separation of PieceTree text representation and DOM-based View Models, Makepad utilizes a highly dense, Rust-oriented `CodeDocument` struct natively backing functional selections and inline layovers. Croft's C11 `text_editor_node` draws on this by keeping spatial layout and rendering logically closer to the data substrate—rather than relying on heavy object-oriented abstractions.

---

## Microsoft VS Code PieceTree Comparison

While Croft takes functional inspiration from the Piece Table concept, its implementation through Sapling/Thatch diverges from Microsoft VS Code's approach in `pieceTreeBase.ts` in key areas, though we adapt crucial lessons on tradeoff management:

1. **Mutability vs. Persistence**: VS Code's PieceTree uses a Red-Black Tree to track contiguous `StringBuffer` chunks, but it mutates this tree in place during insertions and deletions, aggressively splitting and restructuring nodes. While Thatch's approach with `Seq` finger trees and GC-Gibbon emphasizes functional persistence, **Croft is not strictly bound to purely non-destructive operations**. Where mutable operations yield significantly better alignment with Wasm linear memory efficiency and lifecycle management, we will trade persistent transactions for destructive edits.
2. **Memory Reclamation Strategy**: VS Code appends all new typing directly into a raw string `changeBuffer` (which eventually triggers a garbage collection compaction run). In Croft, alignment with the Wasm linear memory model is paramount. Finding a robust strategy to reclaim linear memory areas that become "garbage" during editing is more critical than maintaining perfectly immutable transaction histories for every keystroke. 
3. **Line Indexing Cache**: VS Code eagerly computes `lineFeedCnt` and caches line-break indices directly inside the Red-Black tree nodes to optimize `getPositionAt(offset)` lookups. In Croft, this aligns closely with our intent to use Sapling's chunked metadata caches to resolve geometries quickly.

---

## Editor Mechanics: Text Layout & Proportional Metrics

To successfully implement a fully custom raw text editor in graphical C, several foundational concepts diverge from standard DOM-based UI development:

### 1. Typographic Advance vs. Visual Ink Bounds
When hit-testing mouse clicks against a string of proportional text (e.g., mapping `X=150` to the letter 'i' or 'm'), simply measuring the "bounding box" of the text graphic fails catastrophically. 
- Visual envelopes (Ink Bounds) ignore trailing whitespace (zero-width ink) and can have negative left-bearings (the tail of a 'j' extending backwards).
- Accurate interaction requires summing the **Typographic Advance** (the physical space the font designer allocated for the character to occupy on the raster grid). We implemented UTF-8 decoding in the `host_render` loop specifically to query hardware `getAdvance` byte-by-byte for exact coordinate alignment.

### 2. Nearest-Neighbor Cursor Snapping
When a user clicks inside a proportional character's hit-box, the cursor should jump to the closest edge (left or right). A naive `while (width < mouse_x)` loop traps the cursor on the right edge. Croft uses a delta comparison between the previous character's absolute right boundary and the current character's boundary, snapping the `selection_index` based on structural proximity.

### 3. State-driven Cursor Blinking
Cross-platform UI toolkits rarely offer a "blinking cursor" primitive. Croft passes the canonical high-resolution OS time (`glfwGetTime`) down through the `render_ctx` tree on every single frame. The scene graph leaf node computes a `1Hz` cycle mathematically: `(millis / 500) % 2 == 0`. Because this depends on continuous frame delivery, the underlying windowing loop must actively pump `.time` to the scene graph to keep the visual cursor alive, trading idle-CPU for fluid UI states.
