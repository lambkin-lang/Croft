# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What Is Croft

Croft is a portable C11 host runtime and runtime library for the Lambkin programming language. It executes one Wasm instance per OS thread with strict isolation and host-mediated messaging. It also produces small native binaries where the Wasm-style isolation model defines the conceptual architecture regardless of compilation target.

## Build Commands

```bash
# Full configure + build + test
make all test

# Configure only
cmake -B build -DCROFT_BUILD_TESTS=ON

# Build only
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test by name
ctest --test-dir build -R test_name --output-on-failure

# Run tests by category
ctest --test-dir build -L sapling-core --output-on-failure
ctest --test-dir build -L sapling-threaded --output-on-failure
ctest --test-dir build -L sapling-runner --output-on-failure
ctest --test-dir build -L sapling-wasi --output-on-failure

# Build and smoke-test all examples
make test-examples

# Build specific artifacts
cmake --build build --target croft_foundation sapling_core
```

## Architecture

**Language mix:** C11 is the core language. C++ and Objective-C++ are used only at isolated platform integration boundaries (GPU rendering bridges). There is no Rust in this project.

**Subsystem prefixes:** All public symbols use a subsystem prefix: `host_`, `scene_`, `sapling_`, `wit_`, `editor_`, `runner_`. Files follow the same convention.

**Wasm-shaped APIs:** Public APIs use fixed-width integers, `(ptr, len)` buffer pairs, and return `int32_t` status codes. These signatures work identically as Wasm imports or native C calls.

**Per-thread isolation:** Each thread owns its own Wasm store/instance. No shared Wasm memory or tables across threads. Inter-thread communication is strictly through `host_send`/`host_recv`.

**Polymorphism:** Opaque pointers and vtable structs (see `scene_node_vtbl`).

**Key subsystems in `/src`:**
- `host/` — Window, render, audio, filesystem, threading, messaging, Wasm embedding
- `scene/` — Scene graph nodes with vtable dispatch
- `sapling/` — Persistent text buffer (rope/piece table), arena, HAMT, B-epsilon tree
- `runner/` — Execution engine, scheduler, wire protocol, mailbox, transaction context
- `runtime/` — WIT runtime bindings (`wit_*_runtime.c`) mapping WIT schemas to host services
- `editor/` — Text editor document layer

**WIT integration:** Schemas live in `schemas/wit/`. The code generator at `tools/wit_codegen.c` produces C bindings. Each WIT mix-in becomes its own static library artifact. Generated rename/trace manifests under `build/generated/*.manifest` allow tracing package-qualified C names back to WIT source.

**Artifact composition:** Croft emits many small static libraries recorded in `build/croft-artifacts.json`. Lambkin treats that manifest as the universe of available mix-ins for constraint solving and final link selection. Each artifact carries a `profile` field when constrained to a specific Sapling profile.

**Three editor families on macOS:** tgfx/Metal scene editor, native AppKit/TextKit CPU editor, and direct-Metal scene editor. The direct-Metal family routes its control plane through WIT host mix-ins.

## Policies

See `.github/copilot-instructions.md` for the full policy file. Key points:

- **Greenfield evolution:** Prefer migrating to current architecture over backward-compatibility shims. Internal changes across runtime, tests, and samples are expected.
- **Track-or-fix:** Decide based on confidence and durability. Investigate before committing to uncertain fixes. Fundamental issues discovered during unrelated work must be tracked.
- **Honest git history:** No `--amend`, no `git stash` juggling, no `git rebase` for cleanup. Commits must reflect real tested states. Additive follow-up commits over synthetic history editing.
- **Serial git ownership:** All `git` operations must be done by the main agent only, strictly one at a time. Do not parallelize `git`, do not delegate `git` to sub-agents, and do not start a new `git` command until the prior one has fully exited and released any lock files.
- **Build reproducibility:** Use the project build system for verification. Clean + full rebuild is the first response to inconsistent failures.
- **Dependencies are a firm boundary:** Do not modify dependency internals except as a last resort; document thoroughly if unavoidable.

## Dependencies

Bootstrap with `tools/bootstrap_deps.sh` and `source local_deps/env.sh`. Pin file: `tools/deps.lock.sh`. External deps: glfw, tgfx, wasm3, miniaudio, wabt. See `docs/BUILD_DEPENDENCY_MATRIX.md` for the full matrix.

## Key Documentation

- `DEVELOPMENT_PLAN.md` — Full architecture specification
- `INCREMENTAL_STRATEGY.md` — Tiered growth plan (Tier 0–10)
- `docs/PRODUCT_FAMILY_PLAN.md` — Croft/Lambkin boundary model and WIT plan
- `docs/EXAMPLE_MATRIX.md` — Example ladder from foundation demos to editor shell
- `docs/EDITOR_FAMILY_ANALYSIS.md` — Editor family comparison on macOS
- `docs/LAMBKIN_XPI_JOURNAL.md` — Join-points and AppKit/direct-Metal research
- `docs/BUILD_DEPENDENCY_MATRIX.md` — Reproducible external build closure
- `.github/copilot-instructions.md` — Full project policies for AI agents
