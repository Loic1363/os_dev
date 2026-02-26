#pragma once

#include <stdint.h>

void serial_init();
uint8_t serial_is_ready();
void serial_write_char(char ch);
void serial_write_str(const char* str);
