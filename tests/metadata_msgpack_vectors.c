#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MULTI_PLANE_RUNTIME_MANAGER_HAVE_MSGPACK_H
#include <msgpack.h>
#endif

#include "../src/metadata_fields.h"

#if MULTI_PLANE_RUNTIME_MANAGER_HAVE_MSGPACK_H

typedef struct {
    int msg_type;
    char *source;
    char *dest;
    char *transaction_uuid;
    void *payload;
    size_t payload_len;
    req_metadata_t meta;
} wrp_test_req_t;

static void wrp_test_req_init(wrp_test_req_t *r)
{
    memset(r, 0, sizeof(*r));
    meta_init(&r->meta);
}

static void wrp_test_req_free(wrp_test_req_t *r)
{
    meta_free(&r->meta);
    free(r->source);
    free(r->dest);
    free(r->transaction_uuid);
    free(r->payload);
}

static void pack_str_kv(msgpack_packer *pk, const char *key, const char *val)
{
    size_t kl = strlen(key);
    size_t vl = strlen(val);
    msgpack_pack_str(pk, kl);
    msgpack_pack_str_body(pk, key, kl);
    msgpack_pack_str(pk, vl);
    msgpack_pack_str_body(pk, val, vl);
}

static int decode_wrp_for_test(const void *buf, size_t len, wrp_test_req_t *out)
{
    msgpack_unpacked result;
    int ok = 0;

    wrp_test_req_init(out);
    msgpack_unpacked_init(&result);

    if (msgpack_unpack_next(&result, (const char *)buf, len, NULL) == MSGPACK_UNPACK_SUCCESS
        && result.data.type == MSGPACK_OBJECT_MAP) {
        msgpack_object_map *map = &result.data.via.map;
        for (uint32_t i = 0; i < map->size; i++) {
            msgpack_object_kv *kv = &map->ptr[i];
            if (kv->key.type != MSGPACK_OBJECT_STR) continue;
            const char *k = kv->key.via.str.ptr;
            uint32_t kl = kv->key.via.str.size;

            if (kl == 8 && memcmp(k, "msg_type", 8) == 0
                && kv->val.type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                out->msg_type = (int)kv->val.via.u64;
            } else if (kl == 6 && memcmp(k, "source", 6) == 0
                       && kv->val.type == MSGPACK_OBJECT_STR) {
                out->source = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
            } else if (kl == 4 && memcmp(k, "dest", 4) == 0
                       && kv->val.type == MSGPACK_OBJECT_STR) {
                out->dest = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
            } else if (kl == 16 && memcmp(k, "transaction_uuid", 16) == 0
                       && kv->val.type == MSGPACK_OBJECT_STR) {
                out->transaction_uuid = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
            } else if (kl == 5 && memcmp(k, "plane", 5) == 0) {
                if (kv->val.type == MSGPACK_OBJECT_STR)
                    out->meta.plane = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
                else
                    out->meta.metadata_type_valid = 0;
            } else if (kl == 10 && memcmp(k, "plane_type", 10) == 0) {
                if (kv->val.type == MSGPACK_OBJECT_STR)
                    out->meta.plane_type = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
                else
                    out->meta.metadata_type_valid = 0;
            } else if (kl == 12 && memcmp(k, "request_type", 12) == 0) {
                if (kv->val.type == MSGPACK_OBJECT_STR)
                    out->meta.request_type = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
                else
                    out->meta.metadata_type_valid = 0;
            } else if (kl == 16 && memcmp(k, "request_sub_type", 16) == 0) {
                if (kv->val.type == MSGPACK_OBJECT_STR)
                    out->meta.request_sub_type = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
                else
                    out->meta.metadata_type_valid = 0;
            } else if (kl == 7 && memcmp(k, "payload", 7) == 0
                       && (kv->val.type == MSGPACK_OBJECT_BIN || kv->val.type == MSGPACK_OBJECT_STR)) {
                size_t pl = (kv->val.type == MSGPACK_OBJECT_BIN) ? kv->val.via.bin.size : kv->val.via.str.size;
                const char *pd = (kv->val.type == MSGPACK_OBJECT_BIN) ? kv->val.via.bin.ptr : kv->val.via.str.ptr;
                out->payload = malloc(pl);
                if (out->payload) {
                    memcpy(out->payload, pd, pl);
                    out->payload_len = pl;
                }
            }
        }
        ok = 1;
    }

    msgpack_unpacked_destroy(&result);
    return ok;
}

static int decode_payload_for_test(const void *buf, size_t len,
                                   char **tool_out, char **cmd_out, char **req_plane_out,
                                   req_metadata_t *meta)
{
    msgpack_unpacked result;
    int ok = 0;

    *tool_out = NULL;
    *cmd_out = NULL;
    *req_plane_out = NULL;

    msgpack_unpacked_init(&result);
    if (msgpack_unpack_next(&result, (const char *)buf, len, NULL) == MSGPACK_UNPACK_SUCCESS
        && result.data.type == MSGPACK_OBJECT_MAP) {
        msgpack_object_map *map = &result.data.via.map;
        for (uint32_t i = 0; i < map->size; i++) {
            msgpack_object_kv *kv = &map->ptr[i];
            if (kv->key.type != MSGPACK_OBJECT_STR) continue;
            const char *k = kv->key.via.str.ptr;
            uint32_t kl = kv->key.via.str.size;

            if (kl == 4 && memcmp(k, "tool", 4) == 0 && kv->val.type == MSGPACK_OBJECT_STR) {
                *tool_out = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
            } else if (kl == 7 && memcmp(k, "command", 7) == 0 && kv->val.type == MSGPACK_OBJECT_STR) {
                *cmd_out = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
            } else if (kl == 5 && memcmp(k, "plane", 5) == 0 && kv->val.type == MSGPACK_OBJECT_STR) {
                *req_plane_out = strndup(kv->val.via.str.ptr, kv->val.via.str.size);
            } else if (meta && kl == 6 && memcmp(k, "static", 6) == 0) {
                meta->has_static = 1;
                if (kv->val.type == MSGPACK_OBJECT_BOOLEAN)
                    meta->static_flag = kv->val.via.boolean ? 1 : 0;
                else
                    meta->static_type_valid = 0;
            } else if (meta && kl == 7 && memcmp(k, "dynamic", 7) == 0) {
                meta->has_dynamic = 1;
                if (kv->val.type == MSGPACK_OBJECT_BOOLEAN)
                    meta->dynamic_flag = kv->val.via.boolean ? 1 : 0;
                else
                    meta->dynamic_type_valid = 0;
            } else if (meta && kl == 8 && memcmp(k, "metadata", 8) == 0) {
                if (kv->val.type == MSGPACK_OBJECT_MAP)
                    meta->has_metadata_obj = 1;
                else
                    meta->metadata_type_valid = 0;
            }
        }
        ok = 1;
    }

    msgpack_unpacked_destroy(&result);
    return ok;
}

static int build_payload(const char *tool,
                         const char *command,
                         int include_static, int static_is_bool, int static_val,
                         int include_dynamic, int dynamic_is_bool, int dynamic_val,
                         int include_metadata_map,
                         char **out_buf, size_t *out_len)
{
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    int map_fields = 1;

    if (command) map_fields++;
    if (include_static) map_fields++;
    if (include_dynamic) map_fields++;
    if (include_metadata_map) map_fields++;

    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_map(&pk, map_fields);
    pack_str_kv(&pk, "tool", tool);

    if (command)
        pack_str_kv(&pk, "command", command);

    if (include_static) {
        msgpack_pack_str(&pk, 6);
        msgpack_pack_str_body(&pk, "static", 6);
        if (static_is_bool) {
            if (static_val) msgpack_pack_true(&pk);
            else msgpack_pack_false(&pk);
        } else {
            msgpack_pack_str(&pk, 4);
            msgpack_pack_str_body(&pk, "oops", 4);
        }
    }

    if (include_dynamic) {
        msgpack_pack_str(&pk, 7);
        msgpack_pack_str_body(&pk, "dynamic", 7);
        if (dynamic_is_bool) {
            if (dynamic_val) msgpack_pack_true(&pk);
            else msgpack_pack_false(&pk);
        } else {
            msgpack_pack_int(&pk, 42);
        }
    }

    if (include_metadata_map) {
        msgpack_pack_str(&pk, 8);
        msgpack_pack_str_body(&pk, "metadata", 8);
        msgpack_pack_map(&pk, 1);
        pack_str_kv(&pk, "tag", "vector");
    }

    *out_buf = malloc(sbuf.size);
    if (!*out_buf) {
        msgpack_sbuffer_destroy(&sbuf);
        return 0;
    }
    memcpy(*out_buf, sbuf.data, sbuf.size);
    *out_len = sbuf.size;
    msgpack_sbuffer_destroy(&sbuf);
    return 1;
}

static int build_wrp(const char *plane_val, int plane_is_string,
                     const char *plane_type_val, int plane_type_is_string,
                     const char *request_type,
                     const char *request_sub_type,
                     const char *payload, size_t payload_len,
                     char **out_buf, size_t *out_len)
{
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    int map_fields = 5;

    if (plane_val) map_fields++;
    if (plane_type_val) map_fields++;
    if (request_type) map_fields++;
    if (request_sub_type) map_fields++;

    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    msgpack_pack_map(&pk, map_fields);

    msgpack_pack_str(&pk, 8); msgpack_pack_str_body(&pk, "msg_type", 8);
    msgpack_pack_int(&pk, 3);
    pack_str_kv(&pk, "source", "test-source");
    pack_str_kv(&pk, "dest", "dns:multi-plane-runtime-manager");
    pack_str_kv(&pk, "transaction_uuid", "uuid-1234");

    if (plane_val) {
        msgpack_pack_str(&pk, 5); msgpack_pack_str_body(&pk, "plane", 5);
        if (plane_is_string) {
            msgpack_pack_str(&pk, strlen(plane_val));
            msgpack_pack_str_body(&pk, plane_val, strlen(plane_val));
        } else {
            msgpack_pack_int(&pk, 99);
        }
    }

    if (plane_type_val) {
        msgpack_pack_str(&pk, 10); msgpack_pack_str_body(&pk, "plane_type", 10);
        if (plane_type_is_string) {
            msgpack_pack_str(&pk, strlen(plane_type_val));
            msgpack_pack_str_body(&pk, plane_type_val, strlen(plane_type_val));
        } else {
            msgpack_pack_true(&pk);
        }
    }

    if (request_type)
        pack_str_kv(&pk, "request_type", request_type);
    if (request_sub_type)
        pack_str_kv(&pk, "request_sub_type", request_sub_type);

    msgpack_pack_str(&pk, 7); msgpack_pack_str_body(&pk, "payload", 7);
    msgpack_pack_bin(&pk, payload_len);
    msgpack_pack_bin_body(&pk, payload, payload_len);

    *out_buf = malloc(sbuf.size);
    if (!*out_buf) {
        msgpack_sbuffer_destroy(&sbuf);
        return 0;
    }
    memcpy(*out_buf, sbuf.data, sbuf.size);
    *out_len = sbuf.size;

    msgpack_sbuffer_destroy(&sbuf);
    return 1;
}

static int run_one(const char *id,
                   const char *plane, int plane_is_string,
                   const char *plane_type, int plane_type_is_string,
                   const char *command,
                   int include_static, int static_is_bool, int static_val,
                   int include_dynamic, int dynamic_is_bool, int dynamic_val,
                   int strict_mode,
                   meta_status_t expected_status,
                   meta_decision_t expected_decision,
                   int expected_allow_override,
                   const char *expected_token)
{
    char *payload = NULL;
    size_t payload_len = 0;
    char *wrp = NULL;
    size_t wrp_len = 0;
    wrp_test_req_t req;
    char *tool = NULL;
    char *decoded_cmd = NULL;
    char *req_plane = NULL;
    meta_cfg_t cfg;
    meta_status_t st;
    meta_decision_t decision = META_DECISION_BASELINE;
    int allow_override = 0;
    char reason[128];
    int rc = 1;

    if (!build_payload("device_uptime", command,
                       include_static, static_is_bool, static_val,
                       include_dynamic, dynamic_is_bool, dynamic_val,
                       1,
                       &payload, &payload_len)) {
        fprintf(stderr, "[%s] payload build failed\n", id);
        return 1;
    }

    if (!build_wrp(plane, plane_is_string,
                   plane_type, plane_type_is_string,
                   "triage", "manual",
                   payload, payload_len,
                   &wrp, &wrp_len)) {
        fprintf(stderr, "[%s] wrp build failed\n", id);
        free(payload);
        return 1;
    }

    if (!decode_wrp_for_test(wrp, wrp_len, &req)) {
        fprintf(stderr, "[%s] wrp decode failed\n", id);
        goto done;
    }

    if (!decode_payload_for_test(req.payload, req.payload_len,
                                 &tool, &decoded_cmd, &req_plane, &req.meta)) {
        fprintf(stderr, "[%s] payload decode failed\n", id);
        goto done;
    }

    cfg.enabled = 1;
    cfg.strict_mode = strict_mode;

    st = meta_validate(&req.meta, &cfg, reason, sizeof(reason));
    if (st == META_OK)
        st = meta_apply_policy(&req.meta, decoded_cmd, &allow_override, &decision, reason, sizeof(reason));

    if (st != expected_status) {
        fprintf(stderr, "[%s] status mismatch got=%d expected=%d\n", id, st, expected_status);
        goto done;
    }
    if (decision != expected_decision) {
        fprintf(stderr, "[%s] decision mismatch got=%d expected=%d\n", id, decision, expected_decision);
        goto done;
    }
    if (allow_override != expected_allow_override) {
        fprintf(stderr, "[%s] allow_override mismatch got=%d expected=%d\n", id, allow_override, expected_allow_override);
        goto done;
    }
    if (strcmp(meta_error_token(st), expected_token) != 0) {
        fprintf(stderr, "[%s] token mismatch got=%s expected=%s\n", id, meta_error_token(st), expected_token);
        goto done;
    }

    rc = 0;

done:
    if (rc == 0) printf("[PASS] %s\n", id);
    else printf("[FAIL] %s\n", id);

    free(tool);
    free(decoded_cmd);
    free(req_plane);
    wrp_test_req_free(&req);
    free(payload);
    free(wrp);
    return rc;
}

int main(void)
{
    int failed = 0;

    failed += run_one("VEC-MSGPACK-001-COMPAT-BASELINE",
                      "diagnostic", 1,
                      "exec", 1,
                      "cat /proc/uptime",
                      0, 1, 0,
                      0, 1, 0,
                      0,
                      META_OK,
                      META_DECISION_BASELINE,
                      1,
                      "");

    failed += run_one("VEC-MSGPACK-002-STATIC-FORCE",
                      "diagnostic", 1,
                      "exec", 1,
                      "cat /etc/shadow",
                      1, 1, 1,
                      0, 1, 0,
                      0,
                      META_OK,
                      META_DECISION_FORCE_CATALOG,
                      0,
                      "");

    failed += run_one("VEC-MSGPACK-003-DYNAMIC-FALSE-REJECT",
                      "diagnostic", 1,
                      "exec", 1,
                      "cat /proc/version",
                      0, 1, 0,
                      1, 1, 0,
                      0,
                      META_ERR_POLICY,
                      META_DECISION_REJECT_OVERRIDE,
                      0,
                      "ERR_METADATA_POLICY");

    failed += run_one("VEC-MSGPACK-004-CONFLICT",
                      "diagnostic", 1,
                      "exec", 1,
                      NULL,
                      1, 1, 1,
                      1, 1, 1,
                      0,
                      META_ERR_CONFLICT,
                      META_DECISION_BASELINE,
                      0,
                      "ERR_METADATA_CONFLICT");

    failed += run_one("VEC-MSGPACK-005-TYPE-ERROR-PAYLOAD",
                      "diagnostic", 1,
                      "exec", 1,
                      NULL,
                      1, 0, 1,
                      0, 1, 0,
                      0,
                      META_ERR_TYPE,
                      META_DECISION_BASELINE,
                      0,
                      "ERR_METADATA_TYPE");

    failed += run_one("VEC-MSGPACK-006-TYPE-ERROR-ENVELOPE",
                      "diagnostic", 1,
                      "exec", 0,
                      NULL,
                      0, 1, 0,
                      0, 1, 0,
                      0,
                      META_ERR_TYPE,
                      META_DECISION_BASELINE,
                      0,
                      "ERR_METADATA_TYPE");

    failed += run_one("VEC-MSGPACK-007-STRICT-INVALID-PLANE",
                      "foo", 1,
                      "exec", 1,
                      NULL,
                      0, 1, 0,
                      0, 1, 0,
                      1,
                      META_ERR_POLICY,
                      META_DECISION_BASELINE,
                      0,
                      "ERR_METADATA_POLICY");

    printf("\nmetadata_msgpack_vectors: %d failed\n", failed);
    return failed == 0 ? 0 : 1;
}

#else

/*
 * Fallback mode (no msgpack headers available on build host):
 * a tiny local serializer/parser pair is used to keep byte-level
 * metadata vector testing runnable.
 */
typedef struct {
    unsigned char *buf;
    size_t len;
    size_t cap;
} fb_buf_t;

static int fb_push(fb_buf_t *b, unsigned char c)
{
    if (b->len + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 64;
        unsigned char *nb = (unsigned char *)realloc(b->buf, ncap);
        if (!nb) return 0;
        b->buf = nb;
        b->cap = ncap;
    }
    b->buf[b->len++] = c;
    return 1;
}

static int fb_write_str(fb_buf_t *b, const char *s)
{
    size_t i;
    size_t n = strlen(s);
    if (n > 255) return 0;
    if (!fb_push(b, (unsigned char)n)) return 0;
    for (i = 0; i < n; i++) {
        if (!fb_push(b, (unsigned char)s[i])) return 0;
    }
    return 1;
}

static int fb_read_str(const unsigned char *p, size_t len, size_t *off, char **out)
{
    unsigned char n;
    if (*off + 1 > len) return 0;
    n = p[*off];
    *off += 1;
    if (*off + n > len) return 0;
    *out = (char *)malloc((size_t)n + 1);
    if (!*out) return 0;
    memcpy(*out, p + *off, n);
    (*out)[n] = '\0';
    *off += n;
    return 1;
}

static int run_fallback_case(const char *id,
                             int strict_mode,
                             const char *plane,
                             const char *plane_type,
                             int has_static,
                             int static_flag,
                             int has_dynamic,
                             int dynamic_flag,
                             const char *command,
                             meta_status_t expected_status,
                             meta_decision_t expected_decision,
                             int expected_allow_override,
                             const char *expected_token)
{
    req_metadata_t m;
    meta_cfg_t cfg;
    meta_status_t st;
    meta_decision_t dec = META_DECISION_BASELINE;
    int allow = 0;
    char reason[128];
    fb_buf_t b = {0};
    size_t off = 0;
    char *p_plane = NULL;
    char *p_plane_type = NULL;
    char *p_command = NULL;
    int rc = 1;

    meta_init(&m);

    /* Serialize three fields into a tiny byte stream. */
    if (!fb_write_str(&b, plane ? plane : "")) goto done;
    if (!fb_write_str(&b, plane_type ? plane_type : "")) goto done;
    if (!fb_write_str(&b, command ? command : "")) goto done;

    /* Parse back (fallback parser). */
    if (!fb_read_str(b.buf, b.len, &off, &p_plane)) goto done;
    if (!fb_read_str(b.buf, b.len, &off, &p_plane_type)) goto done;
    if (!fb_read_str(b.buf, b.len, &off, &p_command)) goto done;

    m.plane = p_plane;
    m.plane_type = p_plane_type;
    p_plane = NULL;
    p_plane_type = NULL;
    m.has_static = has_static;
    m.static_flag = static_flag;
    m.has_dynamic = has_dynamic;
    m.dynamic_flag = dynamic_flag;

    cfg.enabled = 1;
    cfg.strict_mode = strict_mode;

    st = meta_validate(&m, &cfg, reason, sizeof(reason));
    if (st == META_OK)
        st = meta_apply_policy(&m, p_command, &allow, &dec, reason, sizeof(reason));

    if (st != expected_status) goto done;
    if (dec != expected_decision) goto done;
    if (allow != expected_allow_override) goto done;
    if (strcmp(meta_error_token(st), expected_token) != 0) goto done;

    rc = 0;

done:
    printf("[%s] %s\n", rc == 0 ? "PASS" : "FAIL", id);
    free(p_plane);
    free(p_plane_type);
    free(p_command);
    free(b.buf);
    meta_free(&m);
    return rc;
}

int main(void)
{
    int failed = 0;

    failed += run_fallback_case("VEC-FALLBACK-001-COMPAT-BASELINE",
                                0, "diagnostic", "exec",
                                0, 0, 0, 0,
                                "cat /proc/uptime",
                                META_OK, META_DECISION_BASELINE, 1, "");

    failed += run_fallback_case("VEC-FALLBACK-002-STATIC-FORCE",
                                0, "diagnostic", "exec",
                                1, 1, 0, 0,
                                "cat /etc/shadow",
                                META_OK, META_DECISION_FORCE_CATALOG, 0, "");

    failed += run_fallback_case("VEC-FALLBACK-003-DYNAMIC-FALSE-REJECT",
                                0, "diagnostic", "exec",
                                0, 0, 1, 0,
                                "cat /proc/version",
                                META_ERR_POLICY, META_DECISION_REJECT_OVERRIDE, 0,
                                "ERR_METADATA_POLICY");

    failed += run_fallback_case("VEC-FALLBACK-004-STRICT-INVALID-PLANE",
                                1, "foo", "exec",
                                0, 0, 0, 0,
                                "",
                                META_ERR_POLICY, META_DECISION_BASELINE, 0,
                                "ERR_METADATA_POLICY");

    printf("\nmetadata_msgpack_vectors: fallback serializer/parser path\n");
    printf("metadata_msgpack_vectors: %d failed\n", failed);
    return failed == 0 ? 0 : 1;
}

#endif
