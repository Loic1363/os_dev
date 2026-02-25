#pragma once

#include <stdint.h>

void console_init();
void console_handle_char(char ch);
void console_handle_backspace();
void console_history_prev();
void console_history_next();
void console_render_status_line();
void console_tick();
