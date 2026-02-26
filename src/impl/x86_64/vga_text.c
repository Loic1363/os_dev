#include "x86_64/port.h"

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5

#define CRTC_MAX_SCAN_LINE 0x09
#define CRTC_CURSOR_START 0x0A
#define CRTC_CURSOR_END 0x0B

static uint8_t vga_crtc_read(uint8_t index) {
    port_outb(VGA_CRTC_INDEX, index);
    port_wait();
    return port_inb(VGA_CRTC_DATA);
}

static void vga_crtc_write(uint8_t index, uint8_t value) {
    port_outb(VGA_CRTC_INDEX, index);
    port_wait();
    port_outb(VGA_CRTC_DATA, value);
    port_wait();
}

void vga_text_set_mode_80x50() {
    // Switch text cell height from 16 to 8 scanlines. On standard VGA text mode,
    // this doubles visible rows from 25 to 50 while keeping 80 columns.
    uint8_t max_scan = vga_crtc_read(CRTC_MAX_SCAN_LINE);
    max_scan = (uint8_t) ((max_scan & 0xE0) | 0x07);
    vga_crtc_write(CRTC_MAX_SCAN_LINE, max_scan);

    // Adjust cursor shape for 8-scanline characters.
    vga_crtc_write(CRTC_CURSOR_START, 0x06);
    vga_crtc_write(CRTC_CURSOR_END, 0x07);
}
