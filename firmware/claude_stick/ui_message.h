/**
 * ui_message.h — telas de mensagem e loading
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_MESSAGE_H
#define UI_MESSAGE_H

#include "app_state.h"
#include "ui_helpers.h"

void ui_message(const char *title, const char *sub, uint32_t color);
void ui_loading(const char *sub);

#endif // UI_MESSAGE_H
