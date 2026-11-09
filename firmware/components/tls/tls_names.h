/*
 * What the device's certificates say about it, and when a stored one no
 * longer says the right thing.
 *
 * Plain C with no IDF headers, so it runs in a host test: these strings
 * decide whether a browser's imported CA still vouches for the page, and a
 * mistake shows up only as a certificate warning on somebody else's desk.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The name the certificates carry as their organisation. */
#define TLS_PRODUCT "NetworkDeck"

/** Room for any subject or leaf id below, with a hostname at its longest. */
#define TLS_NAME_MAX 96

/**
 * The device CA's subject, from the name it was generated for (the hostname
 * plus a per-device suffix). The leaf's issuer is built by the same call
 * from the stored name, so the two match byte for byte.
 */
void tls_ca_subject(char *out, size_t len, const char *ca_host);

/** The leaf's subject: the mDNS name of @p host. */
void tls_leaf_subject(char *out, size_t len, const char *host);

/**
 * What a leaf is issued for: its layout @p version, @p host and @p ip ("" for
 * none). Stored beside the leaf, and compared with the current one on every
 * start: a leaf whose id differs is issued again.
 */
void tls_leaf_id(char *out, size_t len, int version, const char *host, const char *ip);

/**
 * Whether the leaf stored under @p stored_id may be served for @p host and
 * @p ip. False for a leaf issued under another name, another address or an
 * older layout, and for no stored id at all.
 */
bool tls_leaf_current(const char *stored_id, int version, const char *host, const char *ip);

/** The name a certificate uses for a device whose hostname cannot be one. */
#define TLS_DEFAULT_LABEL "networkdeck"

/** The longest name tls_dns_label() will produce, with its terminator. */
#define TLS_LABEL_MAX 64

/**
 * The name the certificates carry for the hostname @p host: lower case, made
 * of letters, digits, '-', '_' and '.' with no empty label. A hostname that is
 * none of that (or empty, or too long) is TLS_DEFAULT_LABEL: the names go into
 * a certificate as ASCII and into a name constraint that a client enforces, so
 * a name that is not plain would make a certificate every client rejects.
 */
void tls_dns_label(char *out, size_t len, const char *host);

/**
 * Whether the authority made for @p ca_label can vouch for a leaf named for
 * @p label: only when they are the same name. The authority is constrained to
 * one device's names (tls_name_constraints()), so a device that has been
 * renamed needs another.
 */
bool tls_ca_covers(const char *ca_label, const char *label);

/**
 * Whether @p ip, four bytes in network order, is an address a certificate of
 * ours may name: private (10/8, 172.16/12, 192.168/16) or link-local
 * (169.254/16). The authority's constraint on addresses is these ranges - the
 * address a DHCP lease will give is not known when it is made - and a leaf
 * that named an address outside them would be rejected whole.
 */
bool tls_ip_in_scope(const uint8_t ip[4]);

/**
 * The value of the authority's nameConstraints extension, DER, for the device
 * named @p label: it may vouch for @p label, "@p label.local" and addresses in
 * the ranges of tls_ip_in_scope(), and for nothing else. Without it a stolen
 * copy of the authority's key, which sits in flash, could sign for any site.
 * Returns the length, or 0 when @p cap is too small or @p label is empty or
 * longer than TLS_LABEL_MAX - 1.
 */
size_t tls_name_constraints(uint8_t *out, size_t cap, const char *label);

#ifdef __cplusplus
}
#endif
