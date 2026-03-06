#!/usr/bin/env bash

# Pinned dependency inputs for Croft bootstrap tooling.
# Brew-managed dependencies are version-pinned by install path.

CROFT_DEPS_LOCK_VERSION=1

GLFW_ROOT="/opt/homebrew/Cellar/glfw/3.4"
WABT_BIN="/opt/homebrew/Cellar/wabt/1.0.39/bin"

TGFX_REPO="https://github.com/Tencent/tgfx.git"
TGFX_SOURCE_DIR="/Users/mshonle/Projects/Tencent/tgfx"
TGFX_REF="6ea71127be6649a711a0271e0fda16f1de17ab74"

WASM3_REPO="https://github.com/wasm3/wasm3.git"
WASM3_REF="79d412ea5fcf92f0efe658d52827a0e0a96ff442"

MINIAUDIO_REF="9634bedb5b5a2ca38c1ee7108a9358a4e233f14d"
MINIAUDIO_HEADER_URL="https://raw.githubusercontent.com/mackron/miniaudio/${MINIAUDIO_REF}/miniaudio.h"
