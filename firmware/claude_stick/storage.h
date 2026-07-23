/**
 * storage.h — persistencia em NVS (namespace `claude`).
 *
 * Guarda o blob cifrado do token, o contador de tentativas de PIN e as
 * preferencias de UI (brilho, intervalo de poll, fuso, slideshow, heatmap,
 * idioma). O PIN em si nunca e gravado — ver crypto.h.
 *
 * O historico/heatmap NAO vem daqui: fica em LittleFS (ver history.h).
 */
#ifndef STORAGE_H
#define STORAGE_H

#include "app_state.h"

// Le tudo do NVS para os globais, aplicando faixa valida a cada preferencia
// (valor fora da faixa cai no default, protege contra NVS corrompido).
void load_persisted();

void save_blob();
void save_attempts();
void apply_brightness();

// Apaga token, redes WiFi e preferencias; devolve o device ao onboarding.
void factory_reset();

#endif // STORAGE_H
