#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build-daemon -G Ninja \
  -DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=ON \
  -DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=ON \
  -DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL=OFF \
  -DMULTI_PLANE_RUNTIME_MANAGER_REGISTER_WITH_PARODUS=OFF \
  -DMULTI_PLANE_RUNTIME_MANAGER_PUSH_REQUIRE_LOCAL_ONLY=OFF

cmake --build build-daemon -j"$(nproc)"

export MULTI_PLANE_RUNTIME_MANAGER_CONFIG_FILE=/workspace/multi-plane-runtime-manager.conf

./build-daemon/multi-plane-runtime-manager /workspace &
PID=$!

sleep 3
kill -TERM "$PID"
wait "$PID" || true

echo "daemon smoke run complete"
