#pragma once

// Root CA bundle para os endpoints HTTPS (api.anthropic.com, status.claude.com).
// Múltiplas âncoras: a cadeia atual da API é ECDSA (WE1 ← GTS Root R4),
// com cross-sign RSA em GlobalSign. Confiar em GTS R4 + WE1 evita o
// mbedTLS do ESP32 falhar o path-building só com GlobalSign.
//   GlobalSign Root CA      — cross-sign histórico (expira 2028-01-28)
//   GTS Root R4             — âncora atual da api.anthropic.com (expira 2036-06-22)
//   GTS WE1                 — intermediária atual da API (expira 2029-02-20)
//   ISRG Root X1            — Let's Encrypt, status.claude.com (expira 2035-06-04)
//   DigiCert Global Root G2 — alvo comum de rotação (expira 2038-01-15)
extern const char CA_BUNDLE[];
