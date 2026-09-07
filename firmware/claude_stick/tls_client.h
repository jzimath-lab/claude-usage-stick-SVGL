#pragma once

#include <WiFiClientSecure.h>
#include <lwip/netdb.h>
#include <string.h>
#include "certs.h"

// Arduino-ESP32 3.x Network.hostByName() prefere AAAA quando há IPv6
// global. O connect TLS com IPv6 no S3 costuma falhar com
// HTTPC_ERROR_CONNECTION_REFUSED (-1). Força A (IPv4) e mantém o SNI.
static inline bool resolve_ipv4(const char *host, IPAddress &out) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    int err = getaddrinfo(host, nullptr, &hints, &res);
    if (err != 0 || !res) {
        Serial.printf("[TLS] dns4 %s err=%d\n", host, err);
        return false;
    }
    auto *sin = reinterpret_cast<sockaddr_in *>(res->ai_addr);
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&sin->sin_addr.s_addr);
    out = IPAddress(p[0], p[1], p[2], p[3]);
    freeaddrinfo(res);
    return true;
}

class Ipv4SecureClient : public WiFiClientSecure {
public:
    int connect(const char *host, uint16_t port) override {
        IPAddress ip;
        if (!resolve_ipv4(host, ip)) return 0;
        Serial.printf("[TLS] %s -> %s\n", host, ip.toString().c_str());
        return WiFiClientSecure::connect(ip, port, host, _CA_cert, _cert, _private_key);
    }
    int connect(const char *host, uint16_t port, int32_t timeout) override {
        _timeout = timeout;
        return connect(host, port);
    }
};

static inline void attach_tls(WiFiClientSecure &client, bool insecure) {
    client.setHandshakeTimeout(20);
    if (insecure) {
        client.setInsecure();
        Serial.println("[TLS] insecure (no cert verify)");
    } else {
        client.setCACert(CA_BUNDLE);
    }
}
