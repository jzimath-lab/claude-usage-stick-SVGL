/**
 * history.h — historico em ring buffer e heatmap por hora, persistidos em LittleFS
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef HISTORY_H
#define HISTORY_H

#include "app_state.h"
#include "ui_helpers.h"

void hist_push(float h5, float d7);
int hist_idx(int i);
uint32_t day_key();
int day_slot(uint32_t dk);
void accumulate_heat(float h5);
void heat_mode_data(int mode, float out[24]);
void save_history();
void load_history();

#endif // HISTORY_H
