#pragma once

#include <stdint.h>

uint8_t nat_is_active();
uint8_t nat_open(const char* path);
void nat_handle_char(char ch);
void nat_handle_backspace();
void nat_handle_enter();
void nat_handle_tab();
void nat_handle_arrow_left();
void nat_handle_arrow_right();
void nat_handle_arrow_up();
void nat_handle_arrow_down();
void nat_handle_ctrl(uint8_t key_lower);
void nat_tick();
