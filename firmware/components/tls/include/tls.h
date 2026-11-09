/*
 * The device's own TLS identity.
 *
 * Ported from IP-KVM32 (ipk_web/tls_cert.c). A dock ships with nobody to issue it a certificate, and an operator who must
 * supply a PEM before the web interface works has no way in at all. So the
 * device generates a self-signed one on first boot and keeps it in NVS. The
 * browser will warn once, which is the honest state of affairs: nothing has
 * vouched for this device except the device.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The server's certificate chain, its private key, and the CA to import. */
typedef struct {
    /**
     * What the server presents. For the device's own identity that is the leaf
     * alone: its CA is name-constrained, an extension mbedTLS cannot read and so
     * could not load into the TLS server, and a client that trusts the CA has it
     * already. For an operator's certificate it is their whole chain.
     */
    char *cert_pem;
    size_t cert_len; /**< including the terminator, as esp_https_server wants */
    char *key_pem;   /**< the leaf's private key */
    size_t key_len;
    char *ca_pem;    /**< the CA certificate to import, or NULL for an operator-supplied cert */
    size_t ca_len;
    bool byo;        /**< true when this is the operator's own certificate, not the self-signed one */
} tls_identity_t;

/**
 * Load the stored identity, generating one if there is none or if the stored
 * one no longer matches the configured hostname.
 *
 * Generation takes a few hundred milliseconds and happens once in the device's
 * life, so it is done inline at start-up rather than in the background: TLS
 * cannot start without it anyway.
 *
 * When @p allow_byo is true and the operator has installed their own
 * certificate (see tls_byo_set), that one is used instead of the self-signed
 * identity, with @p out->byo set and @p out->ca_pem left NULL. Pass false to
 * force the self-signed identity - the web server uses this as a fallback if the
 * operator's certificate is rejected by the TLS stack, so a bad upload can never
 * leave the console unreachable.
 *
 * @return ESP_OK with @p out filled, and the caller owning both buffers.
 */
esp_err_t tls_identity_get(tls_identity_t *out, bool allow_byo);

void tls_identity_free(tls_identity_t *id);

/** Discard the stored self-signed identity so the next start generates a new one. */
esp_err_t tls_identity_reset(void);

/**
 * Record the IPv4 address the device was given by DHCP, so the certificate can
 * name it from the next restart. Returns true if it changed anything.
 *
 * A no-op when the address is static: that one is named from the setting
 * already, and is known before the network is even up.
 */
bool tls_set_ip4(const char *ip);

/**
 * Install an operator-supplied certificate and matching private key (both PEM),
 * used in place of the self-signed identity from the next start. The pair is
 * validated (both parse, and the key matches the certificate) before it is
 * stored; on failure nothing is written and @p err carries a short reason.
 * @p cert_pem may be a chain (leaf first). Takes effect after a restart.
 */
esp_err_t tls_byo_set(const char *cert_pem, const char *key_pem, char *err, size_t errlen);

/** Remove any operator-supplied certificate, reverting to the self-signed one. */
esp_err_t tls_byo_clear(void);

/** True when an operator-supplied certificate is installed. */
bool tls_byo_present(void);

/*
 * What the certificates name, from the firmware: the hostname (without ".local"), and the static IPv4
 * address when there is one (true and @p out filled; false on DHCP).
 */
void tls_host_hostname(char *out, size_t len);
bool tls_host_static_ip(char *out, size_t len);

#ifdef __cplusplus
}
#endif
