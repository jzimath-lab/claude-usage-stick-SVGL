/**
 * lv_conf.h — config do LVGL 9.2 para o Claude Usage Stick (JC4832W535).
 *
 * Compile com -DLV_CONF_INCLUDE_SIMPLE (ver build.sh) para que o LVGL ache
 * este arquivo pelo include path do sketch. Alternativa: copiar este arquivo
 * para a pasta de libraries do Arduino (um nível acima da pasta `lvgl`).
 *
 * Arquivo parcial: o que não estiver definido aqui usa o default do
 * lv_conf_internal.h. As opções abaixo são as que importam para esta placa.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Guarda obrigatória: lv_conf_internal.h inclui este arquivo também durante a
   montagem dos .S do LVGL (ex.: draw/sw/blend/helium/lv_blend_helium.S). Sem a
   guarda, o assembler recebe os typedef de stdint.h e falha com
   "unknown opcode or format name 'typedef'". Ver o aviso no lv_conf_internal.h. */
#ifndef __ASSEMBLY__
#include <stdint.h>
#endif

/*====================
   COLOR
 *====================*/
#define LV_COLOR_DEPTH 16
/* Pipeline validado no bring-up usa cópia direta RGB565 (sem swap).
   Se vermelho/azul saírem trocados, mude para 1. */
#define LV_COLOR_16_SWAP 0

/*=========================
   MEMÓRIA
   Pool interno do LVGL (objetos/estilos). O buffer de render full-screen
   (480x320x2) é alocado à parte na PSRAM, dentro do sketch.
 *=========================*/
#define LV_USE_STDLIB_MALLOC   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING   LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF  LV_STDLIB_BUILTIN
// 96K bastava para 9 tiles. O deck do GitHub acrescentou ~137 objetos (70 so
// no grafico diario empilhado, 14 dias x 5 projetos) e o pool estourava
// DURANTE ui_main(): o LVGL aborta na falha de alocacao e o ESP reinicia — o
// sintoma e "pisca a tela e volta ao PIN", que parece crash de logica e nao e.
#define LV_MEM_SIZE            (144 * 1024U)

// O pool vai para a PSRAM, nao para a RAM interna.
//
// Subir 96K -> 144K de RAM INTERNA consertou o reboot (estouro de pool ao
// montar os tiles do GitHub) e criou outro problema: a RAM interna foi de 54%
// para 69%, e o handshake TLS ficou sem bloco grande — o fetch do Claude
// passou a falhar com http_-1 (CONNECTION_REFUSED), que e como falta de heap
// se manifesta. Uma correcao gerando a seguinte.
//
// A placa tem 8 MB de PSRAM octal (PSRAM=opi) ociosa. Metadados de objeto
// toleram bem a latencia da PSRAM; o buffer de desenho e separado e continua
// na RAM interna.
#define LV_MEM_ADR             0
#define LV_MEM_POOL_INCLUDE    <esp_heap_caps.h>
#define LV_MEM_POOL_ALLOC(sz)  heap_caps_malloc(sz, MALLOC_CAP_SPIRAM)

/*====================
   HAL / SISTEMA
 *====================*/
#define LV_USE_OS LV_OS_NONE
/* tick vem de lv_tick_set_cb(millis) no sketch */

/*====================
   RENDER
 *====================*/
#define LV_USE_DRAW_SW 1

/*====================
   FONTES (Montserrat usadas na UI)
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGETS usados
 *====================*/
#define LV_USE_LABEL        1
#define LV_USE_BUTTON       1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_BAR          1
#define LV_USE_LIST         1
#define LV_USE_TEXTAREA     1
#define LV_USE_KEYBOARD     1
#define LV_USE_CANVAS       1
#define LV_USE_IMAGE        1
#define LV_USE_LINE         1
#define LV_USE_ARC          1
#define LV_USE_SPINNER      1
#define LV_USE_TILEVIEW     1
#define LV_USE_CHART        1

#endif /*LV_CONF_H*/
