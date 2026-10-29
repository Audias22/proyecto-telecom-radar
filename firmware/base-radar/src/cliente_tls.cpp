#include "cliente_tls.h"

#include "config.h"

ClienteTls::ClienteTls() {
    engancharBundle();
    // stop_ssl_socket() conserva handshake_timeout: alcanza con fijarlo una vez.
    setHandshakeTimeout(FIREBASE_TIMEOUT_TLS_MS / 1000);  // recibe segundos
}

int ClienteTls::connect(IPAddress ip, uint16_t puerto) {
    engancharBundle();
    return NetworkClientSecure::connect(ip, puerto);
}

int ClienteTls::connect(const char *host, uint16_t puerto) {
    engancharBundle();
    return NetworkClientSecure::connect(host, puerto);
}

// Lo mismo que setCACertBundle() hace con este cliente, sin esp_crt_bundle_set() (global).
void ClienteTls::engancharBundle() {
    attach_ssl_certificate_bundle(sslclient.get(), true);
    _use_ca_bundle = true;
}
