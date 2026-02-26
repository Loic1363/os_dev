#include <stddef.h>

#include "x86_64/port.h"
#include "x86_64/serial.h"

#define COM1_BASE 0x3F8

#define REG_DATA 0
#define REG_INTERRUPT_ENABLE 1
#define REG_FIFO_CONTROL 2
#define REG_LINE_CONTROL 3
#define REG_MODEM_CONTROL 4
#define REG_LINE_STATUS 5

#define LCR_DLAB (1 << 7)
#define LSR_TX_EMPTY (1 << 5)

static uint8_t g_serial_ready = 0;

static uint16_t com1_reg(uint16_t reg) {
    return (uint16_t) (COM1_BASE + reg);
}

void serial_init() {
    port_outb(com1_reg(REG_INTERRUPT_ENABLE), 0x00);
    port_outb(com1_reg(REG_LINE_CONTROL), LCR_DLAB);
    port_outb(com1_reg(REG_DATA), 0x03); // divisor low  (38400 baud)
    port_outb(com1_reg(REG_INTERRUPT_ENABLE), 0x00); // divisor high
    port_outb(com1_reg(REG_LINE_CONTROL), 0x03); // 8N1
    port_outb(com1_reg(REG_FIFO_CONTROL), 0xC7); // enable FIFO, clear, 14-byte threshold
    port_outb(com1_reg(REG_MODEM_CONTROL), 0x0B); // IRQs enabled, RTS/DSR set
    g_serial_ready = 1;
}

uint8_t serial_is_ready() {
    return g_serial_ready;
}

void serial_write_char(char ch) {
    if (!g_serial_ready) {
        return;
    }
    while ((port_inb(com1_reg(REG_LINE_STATUS)) & LSR_TX_EMPTY) == 0) {
    }
    port_outb(com1_reg(REG_DATA), (uint8_t) ch);
}

void serial_write_str(const char* str) {
    if (!g_serial_ready || str == 0) {
        return;
    }
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(str[i]);
    }
}
