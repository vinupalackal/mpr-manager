SUMMARY = "Multi-Plane Runtime Manager diagnostics service"
DESCRIPTION = "Plane-aware diagnostics runtime with catalog + plugin dispatch over Parodus"
LICENSE = "CLOSED"

inherit cmake pkgconfig systemd externalsrc

# Set EXTERNALSRC from local.conf/layer config to your source checkout path.
# Example:
# EXTERNALSRC:pn-multi-plane-runtime-manager = "/path/to/diag-server"

DEPENDS = "nanomsg msgpack-c cjson"

PACKAGECONFIG ??= "dynamic-plugins metadata-fields register-with-parodus"

PACKAGECONFIG[dynamic-plugins] = "-DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=ON,-DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_DYNAMIC_PLUGINS=OFF"
PACKAGECONFIG[metadata-fields] = "-DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=ON,-DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_METADATA_FIELDS=OFF"
PACKAGECONFIG[acl] = "-DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL=ON,-DMULTI_PLANE_RUNTIME_MANAGER_ENABLE_ACL=OFF"
PACKAGECONFIG[register-with-parodus] = "-DMULTI_PLANE_RUNTIME_MANAGER_REGISTER_WITH_PARODUS=ON,-DMULTI_PLANE_RUNTIME_MANAGER_REGISTER_WITH_PARODUS=OFF"
PACKAGECONFIG[push-local-only] = "-DMULTI_PLANE_RUNTIME_MANAGER_PUSH_REQUIRE_LOCAL_ONLY=ON,-DMULTI_PLANE_RUNTIME_MANAGER_PUSH_REQUIRE_LOCAL_ONLY=OFF"
PACKAGECONFIG[sample-plugin] = "-DMULTI_PLANE_RUNTIME_MANAGER_BUILD_SAMPLE_PLUGIN=ON,-DMULTI_PLANE_RUNTIME_MANAGER_BUILD_SAMPLE_PLUGIN=OFF"

EXTRA_OECMAKE += " \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_REQUIREMENTS_TESTS=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_PLUGIN_INTEGRATION_TESTS=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_UNIT_TESTS=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_VECTOR_TESTS=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_METADATA_MSGPACK_VECTOR_TESTS=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_UNIT_TESTS=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_DYNAMIC_PLUGIN_LIVE_TESTS=OFF \
    -DMULTI_PLANE_RUNTIME_MANAGER_BUILD_FEATURE_MATRIX_SPEC_TESTS=OFF \
"

SYSTEMD_SERVICE:${PN} = "multi-plane-runtime-manager.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/systemd/multi-plane-runtime-manager.service ${D}${systemd_system_unitdir}/

    install -d ${D}${sysconfdir}/multi-plane-runtime-manager
    install -m 0644 ${S}/multi-plane-runtime-manager.conf ${D}${sysconfdir}/multi-plane-runtime-manager/
    install -m 0644 ${S}/multi-plane-runtime-manager-triage-catalog.json ${D}${sysconfdir}/multi-plane-runtime-manager/
    install -m 0644 ${S}/multi-plane-runtime-manager-management-catalog.json ${D}${sysconfdir}/multi-plane-runtime-manager/
    install -m 0644 ${S}/multi-plane-runtime-manager-control-catalog.json ${D}${sysconfdir}/multi-plane-runtime-manager/
    install -m 0644 ${S}/multi-plane-runtime-manager-config-apply-catalog.json ${D}${sysconfdir}/multi-plane-runtime-manager/
}

FILES:${PN} += " \
    ${systemd_system_unitdir}/multi-plane-runtime-manager.service \
    ${sysconfdir}/multi-plane-runtime-manager/* \
    /usr/lib/multi-plane-runtime-manager/plugins/triage/*.so \
"

CONFFILES:${PN} += " \
    ${sysconfdir}/multi-plane-runtime-manager/multi-plane-runtime-manager.conf \
    ${sysconfdir}/multi-plane-runtime-manager/multi-plane-runtime-manager-triage-catalog.json \
    ${sysconfdir}/multi-plane-runtime-manager/multi-plane-runtime-manager-management-catalog.json \
    ${sysconfdir}/multi-plane-runtime-manager/multi-plane-runtime-manager-control-catalog.json \
    ${sysconfdir}/multi-plane-runtime-manager/multi-plane-runtime-manager-config-apply-catalog.json \
"
