cmake_minimum_required(VERSION 3.10)
project(multi-plane-runtime-manager C)

include(CheckIncludeFile)

set(CMAKE_C_STANDARD 11)

# Added 2026-08-16: the only two build-time-configurable behaviors this
# source currently has -- both are #ifndef-guarded in multi-plane-runtime-manager.c
# (see REGISTER_WITH_PARODUS's and PUSH_REQUIRE_LOCAL_ONLY's own
# comments there), so an override here changes the compiled-in default
# without touching source. Defaults here match the source's own
# defaults exactly -- building with neither option touched reproduces
# today's behavior bit-for-bit. Exposed as CMake cache options (rather
# than raw target_compile_definitions with a fixed value) specifically
# so a Yocto recipe can surface them as PACKAGECONFIG entries -- see
# multi-plane-runtime-manager_1.0.bb.
option(MULTI_PLANE_RUNTIME_MANAGER_REGISTER_WITH_PARODUS
       "Send the WRP type-9 registration to Parodus at startup" ON)
option(MULTI_PLANE_RUNTIME_MANAGER_PUSH_REQUIRE_LOCAL_ONLY
    "Reject a catalog PUSH received via the public (Parodus) pair, accepting it only on the local-only endpoint" ON)

# Added 2026-08-19: metadata field parsing/validation/policy module.
option(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS
    "Enable metadata field support (decode, validation, policy enforcement)" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS
    "Enable dynamic plugin manager support" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL
    "Enable ACL policy-store integration (requires acl_policy_store_query implementation)" OFF)

option(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB
    "Enable experimental LMDB catalog backend skeleton (selection/logging only)" OFF)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_SAMPLE_PLUGIN
    "Build a sample dynamic plugin shared object as part of default build/install" ON)

# Added 2026-08-19: builds the executable requirement-spec harness for
# newly introduced plugin + metadata requirement test cases.
option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_REQUIREMENTS_TESTS
    "Build executable requirement-spec tests (TC-PLUG-*, TC-META-*)" ON)

# Added 2026-08-19: optional integration runner for plugin test cases
# requiring a live runtime environment and external hooks.
option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_PLUGIN_INTEGRATION_TESTS
    "Build plugin integration runner (TC-PLUG-002/003/004/009)" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_UNIT_TESTS
    "Build metadata policy/validation unit tests" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_VECTOR_TESTS
    "Build metadata request-flow vector tests" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_MSGPACK_VECTOR_TESTS
    "Build metadata msgpack byte-level vector tests" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_UNIT_TESTS
    "Build dynamic plugin manager/registry unit tests" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_LIVE_TESTS
    "Build live dynamic plugin load/invoke/unload tests" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_FEATURE_MATRIX_SPEC_TESTS
    "Build broad feature-matrix executable spec tests (config/logging/routing/WRP/exec guardrails)" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_CAPABILITY_PUBLICATION_UNIT_TESTS
    "Build capability publication snapshot/merge unit tests" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_PHASE1_BASELINE_GUARDRAIL_TESTS
    "Build phase-1 baseline guardrail latency tests (steady + churn)" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_PHASE2_LOCK_SCOPE_TESTS
    "Build phase-2 lock-scope regression tests" ON)

option(MULTI_PLANE_RUNTIME_MANAGER_BUILD_DB_MODE_TESTS
    "Build LMDB database-mode tests (requires MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB=ON)" ON)

check_include_file(msgpack.h MULTI_PLANE_RUNTIME_MANAGER_HAVE_MSGPACK_H)

# Corrected 2026-08-14 (docs/24_diag_server_merge_plan.md §2): the source
# filename here already matched multi-plane-runtime-manager.c in the uploaded baseline --
# README.md and REQUIREMENTS.md described a multi-plane-runtime-manager.c/multi-plane-runtime-manager.c
# mismatch that does not exist in this file. Left as-is; the stale
# documentation was corrected instead of this line.
set(MULTI_PLANE_RUNTIME_MANAGER_SOURCES
    src/multi-plane-runtime-manager.c
    src/catalog_backend.c
    src/metadata_fields.c
    src/capability_publication.c
)

if(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS)
    list(APPEND MULTI_PLANE_RUNTIME_MANAGER_SOURCES src/plugin_manager.c src/tool_registry.c)
endif()

add_executable(multi-plane-runtime-manager ${MULTI_PLANE_RUNTIME_MANAGER_SOURCES})

# Added 2026-08-16: translates the two cache options above into the
# #ifndef-guarded #defines multi-plane-runtime-manager.c reads. Generator expression
# form (rather than a plain if/else block calling
# target_compile_definitions twice) so both options are handled
# identically in one place.
target_compile_definitions(multi-plane-runtime-manager PRIVATE
    REGISTER_WITH_PARODUS=$<BOOL:${MULTI_PLANE_RUNTIME_MANAGER_REGISTER_WITH_PARODUS}>
    PUSH_REQUIRE_LOCAL_ONLY=$<BOOL:${MULTI_PLANE_RUNTIME_MANAGER_PUSH_REQUIRE_LOCAL_ONLY}>
    MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=$<BOOL:${MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS}>
    MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=$<BOOL:${MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS}>
    MULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL=$<BOOL:${MULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL}>
    MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB=$<BOOL:${MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB}>
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glibc_version.map")
    target_link_options(multi-plane-runtime-manager PRIVATE
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/glibc_version.map
    )
endif()

# Corrected 2026-08-14: multi-plane-runtime-manager.c talks to Parodus over raw nanomsg
# sockets directly (see the file's own header comment) and never calls into
# wrp-c, libparodus, or cimplog -- those include paths were removed.
target_include_directories(multi-plane-runtime-manager PRIVATE
    ${CMAKE_INSTALL_PREFIX}/include
    /usr/include/cjson
)

target_link_directories(multi-plane-runtime-manager PRIVATE
    ${CMAKE_INSTALL_PREFIX}/lib
)

# Corrected 2026-08-14 (docs/24_diag_server_merge_plan.md §2): wrp-c,
# libparodus, and cimplog were linked but never referenced by the code --
# likely carried over from a sibling project's CMakeLists (the previous
# comment here said "mirrors apparmor-simulator"). Removed; only the
# libraries multi-plane-runtime-manager.c actually uses remain.
#
# nanomsg   -- transport (PUSH/PULL sockets to Parodus)
# msgpackc  -- WRP outer envelope + inner payload encoding
# cjson     -- catalog.json loading
# pthread   -- detached per-request worker threads
target_link_libraries(multi-plane-runtime-manager
    nanomsg
    msgpackc
    cjson
    pthread
)

if(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB)
    target_link_libraries(multi-plane-runtime-manager lmdb)

    add_executable(multi-plane-runtime-manager-catalog-lmdb-import
        src/catalog_lmdb_import.c
    )
    target_include_directories(multi-plane-runtime-manager-catalog-lmdb-import PRIVATE
        ${CMAKE_INSTALL_PREFIX}/include
        /usr/include/cjson
    )
    target_link_directories(multi-plane-runtime-manager-catalog-lmdb-import PRIVATE
        ${CMAKE_INSTALL_PREFIX}/lib
    )
    target_link_libraries(multi-plane-runtime-manager-catalog-lmdb-import
        cjson
        lmdb
    )
    install(TARGETS multi-plane-runtime-manager-catalog-lmdb-import DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

    if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_DB_MODE_TESTS)
        enable_testing()

        add_executable(multi-plane-runtime-manager-catalog-backend-lmdb-mode-tests
            tests/catalog_backend_lmdb_mode_tests.c
            src/catalog_backend.c
        )

        target_compile_definitions(multi-plane-runtime-manager-catalog-backend-lmdb-mode-tests PRIVATE
            MULTI_PLANE_RUNTIME_MANAGER_ENABLE_CATALOG_LMDB=1
        )

        target_include_directories(multi-plane-runtime-manager-catalog-backend-lmdb-mode-tests PRIVATE
            ${CMAKE_INSTALL_PREFIX}/include
            /usr/include/cjson
            src
        )

        target_link_libraries(multi-plane-runtime-manager-catalog-backend-lmdb-mode-tests
            cjson
            lmdb
            pthread
        )

        add_test(
            NAME multi-plane-runtime-manager-catalog-backend-lmdb-mode-tests
            COMMAND multi-plane-runtime-manager-catalog-backend-lmdb-mode-tests
        )
    endif()
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS)
    target_link_libraries(multi-plane-runtime-manager ${CMAKE_DL_LIBS})
endif()

install(TARGETS multi-plane-runtime-manager DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)

install(FILES
    multi-plane-runtime-manager.conf
    DESTINATION /etc/multi-plane-runtime-manager
)

install(FILES
    multi-plane-runtime-manager-triage-catalog.json
    multi-plane-runtime-manager-management-catalog.json
    multi-plane-runtime-manager-control-catalog.json
    multi-plane-runtime-manager-config-apply-catalog.json
    DESTINATION /etc/multi-plane-runtime-manager
)

install(FILES
    systemd/multi-plane-runtime-manager.service
    DESTINATION /lib/systemd/system
)

if(MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS AND MULTI_PLANE_RUNTIME_MANAGER_BUILD_SAMPLE_PLUGIN)
    add_library(multi-plane-runtime-manager-sample-plugin SHARED
        plugins/sample_multi_plane_runtime_manager_plugin.c
    )
    set_target_properties(multi-plane-runtime-manager-sample-plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "sample_multi_plane_runtime_manager_plugin"
    )

    install(TARGETS multi-plane-runtime-manager-sample-plugin
        DESTINATION /usr/lib/multi-plane-runtime-manager/plugins/triage
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_REQUIREMENTS_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-requirements-spec-tests
        tests/requirements_spec_tests.c
    )

    add_test(
        NAME multi-plane-runtime-manager-requirements-spec-tests
        COMMAND multi-plane-runtime-manager-requirements-spec-tests
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_PLUGIN_INTEGRATION_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-plugin-integration-tests
        tests/plugin_integration_runner.c
    )

    add_test(
        NAME multi-plane-runtime-manager-plugin-integration-tests
        COMMAND multi-plane-runtime-manager-plugin-integration-tests
    )

    # 77 is a conventional "skip" exit code for integration tests whose
    # runtime environment is not provisioned.
    set_tests_properties(multi-plane-runtime-manager-plugin-integration-tests PROPERTIES
        SKIP_RETURN_CODE 77
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_UNIT_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-metadata-unit-tests
        tests/metadata_fields_unit.c
        src/metadata_fields.c
    )

    target_compile_definitions(multi-plane-runtime-manager-metadata-unit-tests PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=1
    )

    add_test(
        NAME multi-plane-runtime-manager-metadata-unit-tests
        COMMAND multi-plane-runtime-manager-metadata-unit-tests
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_VECTOR_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-metadata-flow-vectors
        tests/metadata_flow_vectors.c
        src/metadata_fields.c
    )

    target_compile_definitions(multi-plane-runtime-manager-metadata-flow-vectors PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=1
    )

    add_test(
        NAME multi-plane-runtime-manager-metadata-flow-vectors
        COMMAND multi-plane-runtime-manager-metadata-flow-vectors
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_MSGPACK_VECTOR_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-metadata-msgpack-vectors
        tests/metadata_msgpack_vectors.c
        src/metadata_fields.c
    )

    target_compile_definitions(multi-plane-runtime-manager-metadata-msgpack-vectors PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=1
        MULTI_PLANE_RUNTIME_MANAGER_HAVE_MSGPACK_H=$<BOOL:${MULTI_PLANE_RUNTIME_MANAGER_HAVE_MSGPACK_H}>
    )

    if(MULTI_PLANE_RUNTIME_MANAGER_HAVE_MSGPACK_H)
        target_include_directories(multi-plane-runtime-manager-metadata-msgpack-vectors PRIVATE
            ${CMAKE_INSTALL_PREFIX}/include
        )

        target_link_directories(multi-plane-runtime-manager-metadata-msgpack-vectors PRIVATE
            ${CMAKE_INSTALL_PREFIX}/lib
        )

        target_link_libraries(multi-plane-runtime-manager-metadata-msgpack-vectors
            msgpackc
        )
    else()
        message(STATUS "multi-plane-runtime-manager-metadata-msgpack-vectors: msgpack.h not found, using built-in fallback serializer/parser")
    endif()

    add_test(
        NAME multi-plane-runtime-manager-metadata-msgpack-vectors
        COMMAND multi-plane-runtime-manager-metadata-msgpack-vectors
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_UNIT_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-dynamic-plugins-unit-tests
        tests/dynamic_plugins_unit.c
        src/plugin_manager.c
        src/tool_registry.c
    )

    target_compile_definitions(multi-plane-runtime-manager-dynamic-plugins-unit-tests PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=1
    )

    target_link_libraries(multi-plane-runtime-manager-dynamic-plugins-unit-tests
        pthread
        ${CMAKE_DL_LIBS}
    )

    add_test(
        NAME multi-plane-runtime-manager-dynamic-plugins-unit-tests
        COMMAND multi-plane-runtime-manager-dynamic-plugins-unit-tests
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_LIVE_TESTS)
    enable_testing()

    if(NOT TARGET multi-plane-runtime-manager-sample-plugin)
        add_library(multi-plane-runtime-manager-sample-plugin SHARED
            plugins/sample_multi_plane_runtime_manager_plugin.c
        )
        set_target_properties(multi-plane-runtime-manager-sample-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "sample_multi_plane_runtime_manager_plugin"
        )
    endif()

    add_executable(multi-plane-runtime-manager-dynamic-plugins-live-tests
        tests/dynamic_plugins_live.c
        src/plugin_manager.c
        src/tool_registry.c
    )

    target_compile_definitions(multi-plane-runtime-manager-dynamic-plugins-live-tests PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=1
    )

    target_link_libraries(multi-plane-runtime-manager-dynamic-plugins-live-tests
        pthread
        ${CMAKE_DL_LIBS}
    )

    add_test(
        NAME multi-plane-runtime-manager-dynamic-plugins-live-tests
        COMMAND multi-plane-runtime-manager-dynamic-plugins-live-tests
    )

    set_tests_properties(multi-plane-runtime-manager-dynamic-plugins-live-tests PROPERTIES
        ENVIRONMENT "MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO=$<TARGET_FILE:multi-plane-runtime-manager-sample-plugin>"
        SKIP_RETURN_CODE 77
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_FEATURE_MATRIX_SPEC_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-feature-matrix-spec-tests
        tests/feature_matrix_spec_tests.c
    )

    add_test(
        NAME multi-plane-runtime-manager-feature-matrix-spec-tests
        COMMAND multi-plane-runtime-manager-feature-matrix-spec-tests
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_CAPABILITY_PUBLICATION_UNIT_TESTS)
    enable_testing()

    add_executable(multi-plane-runtime-manager-capability-publication-unit-tests
        tests/capability_publication_unit.c
        src/capability_publication.c
    )

    add_test(
        NAME multi-plane-runtime-manager-capability-publication-unit-tests
        COMMAND multi-plane-runtime-manager-capability-publication-unit-tests
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_PHASE1_BASELINE_GUARDRAIL_TESTS)
    enable_testing()

    if(NOT TARGET multi-plane-runtime-manager-sample-plugin)
        add_library(multi-plane-runtime-manager-sample-plugin SHARED
            plugins/sample_multi_plane_runtime_manager_plugin.c
        )
        set_target_properties(multi-plane-runtime-manager-sample-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "sample_multi_plane_runtime_manager_plugin"
        )
    endif()

    add_executable(multi-plane-runtime-manager-phase1-baseline-guardrails
        tests/phase1_baseline_guardrails.c
        src/plugin_manager.c
        src/tool_registry.c
    )

    target_compile_definitions(multi-plane-runtime-manager-phase1-baseline-guardrails PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=1
    )

    target_link_libraries(multi-plane-runtime-manager-phase1-baseline-guardrails
        pthread
        ${CMAKE_DL_LIBS}
    )

    add_test(
        NAME multi-plane-runtime-manager-phase1-baseline-guardrails
        COMMAND multi-plane-runtime-manager-phase1-baseline-guardrails
    )

    set_tests_properties(multi-plane-runtime-manager-phase1-baseline-guardrails PROPERTIES
        ENVIRONMENT "MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO=$<TARGET_FILE:multi-plane-runtime-manager-sample-plugin>"
        SKIP_RETURN_CODE 77
    )
endif()

if(MULTI_PLANE_RUNTIME_MANAGER_BUILD_PHASE2_LOCK_SCOPE_TESTS)
    enable_testing()

    if(NOT TARGET multi-plane-runtime-manager-sample-plugin)
        add_library(multi-plane-runtime-manager-sample-plugin SHARED
            plugins/sample_multi_plane_runtime_manager_plugin.c
        )
        set_target_properties(multi-plane-runtime-manager-sample-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "sample_multi_plane_runtime_manager_plugin"
        )
    endif()

    if(NOT TARGET multi-plane-runtime-manager-sample-slow-plugin)
        add_library(multi-plane-runtime-manager-sample-slow-plugin SHARED
            plugins/sample_slow_multi_plane_runtime_manager_plugin.c
        )
        set_target_properties(multi-plane-runtime-manager-sample-slow-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "sample_slow_multi_plane_runtime_manager_plugin"
        )
    endif()

    add_executable(multi-plane-runtime-manager-plugin-lock-scope-live-tests
        tests/plugin_lock_scope_live.c
        src/plugin_manager.c
        src/tool_registry.c
    )

    target_compile_definitions(multi-plane-runtime-manager-plugin-lock-scope-live-tests PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=1
    )

    target_link_libraries(multi-plane-runtime-manager-plugin-lock-scope-live-tests
        pthread
        ${CMAKE_DL_LIBS}
    )

    add_test(
        NAME multi-plane-runtime-manager-plugin-lock-scope-live-tests
        COMMAND multi-plane-runtime-manager-plugin-lock-scope-live-tests
    )

    set_tests_properties(multi-plane-runtime-manager-plugin-lock-scope-live-tests PROPERTIES
        ENVIRONMENT "MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO=$<TARGET_FILE:multi-plane-runtime-manager-sample-plugin>"
        SKIP_RETURN_CODE 77
    )

    add_executable(multi-plane-runtime-manager-plugin-unload-wait-lock-scope-live-tests
        tests/plugin_unload_wait_lock_scope_live.c
        src/plugin_manager.c
        src/tool_registry.c
    )

    target_compile_definitions(multi-plane-runtime-manager-plugin-unload-wait-lock-scope-live-tests PRIVATE
        MULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=1
    )

    target_link_libraries(multi-plane-runtime-manager-plugin-unload-wait-lock-scope-live-tests
        pthread
        ${CMAKE_DL_LIBS}
    )

    add_test(
        NAME multi-plane-runtime-manager-plugin-unload-wait-lock-scope-live-tests
        COMMAND multi-plane-runtime-manager-plugin-unload-wait-lock-scope-live-tests
    )

    set_tests_properties(multi-plane-runtime-manager-plugin-unload-wait-lock-scope-live-tests PROPERTIES
        ENVIRONMENT "MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_PLUGIN_SO=$<TARGET_FILE:multi-plane-runtime-manager-sample-plugin>;MULTI_PLANE_RUNTIME_MANAGER_SAMPLE_SLOW_PLUGIN_SO=$<TARGET_FILE:multi-plane-runtime-manager-sample-slow-plugin>"
        SKIP_RETURN_CODE 77
    )
endif()
