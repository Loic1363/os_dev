#include <stdint.h>

#include "x86_64/idt.h"
#include "x86_64/pit.h"
#include "x86_64/port.h"

#define PORT_PIT_CHANNEL0_DATA 0x40
#define PORT_PIT_COMMAND 0x43

#define PIT_INPUT_CLOCK_HZ 1193182
#define PIT_COMMAND_CHANNEL0 0x00
#define PIT_COMMAND_LOHI 0x30
#define PIT_COMMAND_MODE2 0x04

static volatile uint64_t g_pit_ticks = 0;

static void pit_irq_handler() {
    g_pit_ticks++;
}

void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        frequency_hz = 100;
    }

    uint32_t divisor = PIT_INPUT_CLOCK_HZ / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFF) {
        divisor = 0xFFFF;
    }

    idt_init();
    idt_set_handler_timer(pit_irq_handler);

    port_outb(PORT_PIT_COMMAND, PIT_COMMAND_CHANNEL0 | PIT_COMMAND_LOHI | PIT_COMMAND_MODE2);
    port_wait();
    port_outb(PORT_PIT_CHANNEL0_DATA, (uint8_t) (divisor & 0xFF));
    port_wait();
    port_outb(PORT_PIT_CHANNEL0_DATA, (uint8_t) ((divisor >> 8) & 0xFF));
    port_wait();
}

uint64_t pit_ticks() {
    return g_pit_ticks;
}
