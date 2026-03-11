# Vendored WASI 0.2.9 Snapshot

This directory contains a pinned upstream WIT snapshot copied from the WASI
`proposals/` repository for the `0.2.9` line.

Rules:

- Keep the upstream layout intact under `proposals/<package>/wit/*.wit`.
- Treat these files as vendored reference inputs, not Croft-authored schemas.
- Put Croft-specific narrowing or compatibility overlays under
  `schemas/wit/wasi-current-machine/0.2.9/`, not here.
- `CROFT_WASI_PROPOSALS_DIR` may override this tree at configure time when
  validating against an external checkout, but the default build should remain
  reproducible from this vendored snapshot.
