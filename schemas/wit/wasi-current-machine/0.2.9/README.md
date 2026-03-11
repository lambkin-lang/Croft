# Croft Current-Machine WASI Overlay

This tree contains Croft-authored overlays for vendored WASI `0.2.9` WIT.

Use it only when Croft's current-machine host/runtime intentionally supports a
strict subset of the raw upstream world surface. The canonical upstream source
still lives under `vendor/wasi/0.2.9/proposals/`.

Today this is mainly needed for `wasi:cli`, where the raw upstream
`imports/command` worlds pull in more capability bundles than Croft currently
binds directly. The overlay keeps the package/version stable while making the
current-machine support boundary explicit.
