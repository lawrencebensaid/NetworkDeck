/*
 * What the device's certificates say about it. See tls_names.h.
 */
#include "tls_names.h"

#include <stdio.h>
#include <string.h>

void tls_ca_subject(char *out, size_t len, const char *ca_host)
{
    snprintf(out, len, "CN=%s " TLS_PRODUCT " CA,O=" TLS_PRODUCT, ca_host ? ca_host : "");
}

void tls_leaf_subject(char *out, size_t len, const char *host)
{
    snprintf(out, len, "CN=%s.local,O=" TLS_PRODUCT, host ? host : "");
}

void tls_leaf_id(char *out, size_t len, int version, const char *host, const char *ip)
{
    snprintf(out, len, "v%d|%s|%s", version, host ? host : "", ip ? ip : "");
}

bool tls_leaf_current(const char *stored_id, int version, const char *host, const char *ip)
{
    if (!stored_id || !stored_id[0]) {
        return false;
    }
    char want[TLS_NAME_MAX];
    tls_leaf_id(want, sizeof(want), version, host, ip);
    return strcmp(stored_id, want) == 0;
}

void tls_dns_label(char *out, size_t len, const char *host)
{
    if (!out || len == 0) {
        return;
    }
    size_t n = 0;
    bool ok = host && host[0];
    for (size_t i = 0; ok && host[i]; i++) {
        char c = host[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        const bool name_char = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                               c == '_' || c == '.';
        if (!name_char || (c == '.' && (i == 0 || host[i - 1] == '.')) || n + 1 >= len ||
            n + 1 >= TLS_LABEL_MAX) {
            ok = false;
            break;
        }
        out[n++] = c;
    }
    if (ok && n > 0 && out[n - 1] == '.') {
        ok = false;
    }
    if (!ok) {
        snprintf(out, len, "%s", TLS_DEFAULT_LABEL);
        return;
    }
    out[n] = '\0';
}

bool tls_ca_covers(const char *ca_label, const char *label)
{
    return ca_label && label && ca_label[0] && strcmp(ca_label, label) == 0;
}

bool tls_ip_in_scope(const uint8_t ip[4])
{
    return ip[0] == 10 || (ip[0] == 172 && (ip[1] & 0xf0) == 16) || (ip[0] == 192 && ip[1] == 168) ||
           (ip[0] == 169 && ip[1] == 254);
}

/* ---- the authority's name constraints, as DER -------------------------- */

/** Append a tag, a length and @p content to @p out at @p at; the new end, or 0 if it does not fit. */
static size_t der_put(uint8_t *out, size_t cap, size_t at, uint8_t tag, const uint8_t *content,
                      size_t len)
{
    const size_t hdr = len < 0x80 ? 2 : len < 0x100 ? 3 : 4;
    if (at + hdr + len > cap) {
        return 0;
    }
    out[at++] = tag;
    if (len < 0x80) {
        out[at++] = (uint8_t)len;
    } else if (len < 0x100) {
        out[at++] = 0x81;
        out[at++] = (uint8_t)len;
    } else {
        out[at++] = 0x82;
        out[at++] = (uint8_t)(len >> 8);
        out[at++] = (uint8_t)len;
    }
    memcpy(out + at, content, len);
    return at + len;
}

/** One GeneralSubtree: SEQUENCE { base GeneralName }, minimum and maximum left at their defaults. */
static size_t subtree(uint8_t *out, size_t cap, size_t at, uint8_t name_tag, const uint8_t *name,
                      size_t len)
{
    uint8_t base[2 + 255];
    if (len > 255 - 4) {
        return 0;
    }
    const size_t base_len = der_put(base, sizeof(base), 0, name_tag, name, len);
    return base_len ? der_put(out, cap, at, 0x30, base, base_len) : 0;
}

size_t tls_name_constraints(uint8_t *out, size_t cap, const char *label)
{
    static const uint8_t k_ranges[][8] = {
        {10, 0, 0, 0, 0xff, 0x00, 0x00, 0x00},     /* 10.0.0.0/8 */
        {172, 16, 0, 0, 0xff, 0xf0, 0x00, 0x00},   /* 172.16.0.0/12 */
        {192, 168, 0, 0, 0xff, 0xff, 0x00, 0x00},  /* 192.168.0.0/16 */
        {169, 254, 0, 0, 0xff, 0xff, 0x00, 0x00},  /* 169.254.0.0/16, link-local */
    };
    const size_t n = label ? strlen(label) : 0;
    if (!out || n == 0 || n >= TLS_LABEL_MAX) {
        return 0;
    }
    char with_local[TLS_LABEL_MAX + 8];
    snprintf(with_local, sizeof(with_local), "%s.local", label);

    /* permittedSubtrees [0] ::= SEQUENCE OF GeneralSubtree */
    uint8_t subtrees[512];
    size_t at = subtree(subtrees, sizeof(subtrees), 0, 0x82, (const uint8_t *)label, n); /* dNSName */
    at = at ? subtree(subtrees, sizeof(subtrees), at, 0x82, (const uint8_t *)with_local,
                      strlen(with_local))
            : 0;
    for (size_t i = 0; at && i < sizeof(k_ranges) / sizeof(k_ranges[0]); i++) {
        at = subtree(subtrees, sizeof(subtrees), at, 0x87, k_ranges[i], 8); /* iPAddress */
    }
    if (!at) {
        return 0;
    }
    uint8_t permitted[512 + 4];
    const size_t permitted_len = der_put(permitted, sizeof(permitted), 0, 0xa0, subtrees, at);
    return permitted_len ? der_put(out, cap, 0, 0x30, permitted, permitted_len) : 0;
}
