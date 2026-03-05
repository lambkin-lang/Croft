# References & Architecture Sources

## 1. Microsoft Monaco Editor (VS Code Editor)
**Source**: `microsoft/monaco-editor` & `microsoft/vscode`
**License**: MIT License

The Croft C11 text editor architecture references the Microsoft Monaco editor as a primary structural baseline. However, rather than a direct port of JavaScript and Web Workers, Croft maps Monaco's organizational semantics to the Portable C11 / Wasm environment using Sapling transactions and Lambkin thread isolations.

### Key Conceptual Adaptations:
- **Text Model (Piece Tree)**: Where Monaco utilizes a Piece Tree (balanced binary tree of text chunks) for memory-efficient text buffering, Croft utilizes the `Sapling Text Tree` (a functional, copy-on-write persistent data structure backed by Transactions).
- **View Model (View Lines)**: Monaco strictly separates the canonical Text Model from the View Model (which computes soft-wrapping, folded regions, and injected view zones). Croft maps this to our `text_editor_node` layout phase, projecting 1D linear text into 2D scene graph components with inline Code Bubbles serving as our equivalent to View Zones.
- **Concurrency & Workers**: Monaco uses Web Workers for Language Server parsing and asynchronous syntax highlighting. Croft utilizes Lambkin threads and Wasm sandboxes, coordinating strictly via Sapling transaction logs and message queues without shared memory.
- **Selections & Cursors**: Croft adopts Monaco's `Selection` logic (where directionality matters: `selectionStart` vs `position`) and its multi-cursor abstractions, implementing them directly as scene graph native state inside the C host.
