#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build"}

cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target multi-plane-runtime-manager-plugin-integration-tests

export MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_IT_ENABLE=${MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_IT_ENABLE:-1}
export MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_DIR=${MULTI_PLANE_RUNTIME_MANAGER_PLUGIN_DIR:-"$ROOT_DIR/tests/plugins"}

# Default hooks are no-op success so CI wiring can be validated end-to-end.
# Override any of these with real commands in your integration environment.
export MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_002_CMD=${MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_002_CMD:-true}
export MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_003_CMD=${MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_003_CMD:-true}
export MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_004_CMD=${MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_004_CMD:-true}
export MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_009_CMD=${MULTI_PLANE_RUNTIME_MANAGER_IT_TC_PLUG_009_CMD:-true}

ctest --test-dir "$BUILD_DIR" -R multi-plane-runtime-manager-plugin-integration-tests --output-on-failure
