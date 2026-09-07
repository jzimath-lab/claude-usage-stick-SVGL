#pragma once

// Bundles PEM mínimos. Um PEM gordo (RSA 4096 ISRG + GlobalSign + DigiCert)
// faz o mbedTLS do S3 falhar o handshake com "SSL - Memory allocation failed".
//
// CA_API — api.anthropic.com: leaf ECDSA ← WE1 ← GTS Root R4 (self-signed).
//   GTS R4 como âncora; WE1 no bundle caso o servidor omita o intermediário.
//   GlobalSign (cross-sign de R4) não é necessário se R4 já é trust anchor.
// CA_STATUS — status.claude.com (Let's Encrypt / ISRG Root X1), só no GET
//   de status; não entra no verify da API.
extern const char CA_API[];
extern const char CA_STATUS[];
