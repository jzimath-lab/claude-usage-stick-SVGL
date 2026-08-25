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
// Credenciais da VPS. Texto simples na NVS, de proposito: elas so LEEM um
// snapshot de cota, e cifra-las com o PIN exigiria digita-lo para o aparelho
// voltar a mostrar percentuais apos queda de energia.
void save_vps();
void clear_vps();
void save_attempts();
void apply_brightness();
void bri_tick();     // regra do brilho noturno (1x/s)
void bri_manual();   // ajuste manual suspende o automatico ate o proximo 21h

// Apaga token, redes WiFi e preferencias; devolve o device ao onboarding.
void factory_reset();

#endif // STORAGE_H
