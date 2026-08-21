FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    ca-certificates \
    git \
    libnanomsg-dev \
    libmsgpack-dev \
    libcjson-dev \
    liblmdb-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . /workspace

RUN chmod +x /workspace/scripts/container-ci.sh /workspace/scripts/container-smoke-daemon.sh

# Build everything needed for local validation, then run tests.
RUN cmake -S . -B build -G Ninja \
    -DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_REQUIREMENTS_TESTS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_PLUGIN_INTEGRATION_TESTS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_UNIT_TESTS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_VECTOR_TESTS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_MSGPACK_VECTOR_TESTS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_UNIT_TESTS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_LIVE_TESTS=ON \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_FEATURE_MATRIX_SPEC_TESTS=ON

RUN cmake --build build -j"$(nproc)"
RUN ctest --test-dir build --output-on-failure

CMD ["bash"]
