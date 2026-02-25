#pragma once

#include <stdint.h>

void pit_init(uint32_t frequency_hz);
uint64_t pit_ticks();
