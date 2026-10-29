// cliente_tls.h
// Cliente TLS de todas las conexiones HTTPS de la base (Firebase Auth, Realtime Database y
// Telegram). Verifica el certificado del servidor con el bundle de CA de ESP-IDF en cada conexión,
// nunca sin verificar.
//
// Por qué no basta NetworkClientSecure::setCACertBundle() una vez (arduino-esp32 3.3.7;
// docs/fallos.md, "Certificados TLS tras cerrar una conexión"):
//   - setCACertBundle() deja el callback del bundle (bundle_attach_cb) en el sslclient_context
//     del cliente y _use_ca_bundle = true.
//   - Cada stop() (cierre propio, del servidor o por error de lectura/escritura) termina en
//     stop_ssl_socket(), que hace memset del contexto entero: bundle_attach_cb queda en NULL, pero
//     _use_ca_bundle sigue en true.
//   - La siguiente connect() del mismo cliente arma el TLS sin cadena de CA: "useRootCABundle is
//     set, but attach_ssl_certificate_bundle(ssl, true); was not called!" y el handshake falla con
//     -0x7680 (MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED). El cliente queda inservible hasta recrearlo.
// ClienteTls vuelve a enganchar el bundle antes de cada connect().
//
// Tampoco llama a setCACertBundle(): esa función cambia el bundle global de ESP-IDF
// (esp_crt_bundle_set) que comparten todos los clientes, y con NULL lo suelta para todos
// (esp_crt_bundle_detach). Sin esp_crt_bundle_set(), esp_crt_bundle_attach() usa el bundle
// embebido de menuconfig (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL: la lista de Mozilla, con
// las raíces de Google Trust Services para googleapis.com y firebaseio.com y la de Go Daddy G2
// para api.telegram.org). Ningún cliente toca estado global, así que ninguno puede dejar sin
// certificados a otro.

#pragma once

#include <NetworkClientSecure.h>

class ClienteTls : public NetworkClientSecure {
public:
    // Con el límite del handshake de config.h (FIREBASE_TIMEOUT_TLS_MS).
    ClienteTls();

    // HTTPClient conecta con connect(host, puerto, timeout), que llega a estas dos.
    int connect(IPAddress ip, uint16_t puerto) override;
    int connect(const char *host, uint16_t puerto) override;
    using NetworkClientSecure::connect;

private:
    void engancharBundle();
};
