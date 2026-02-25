#include <stddef.h>
#include <stdint.h>

#include "print.h"
#include "x86_64/gdt.h"
#include "x86_64/idt.h"
#include "x86_64/pic.h"

#define IDT_EXC_DE 0x00
#define IDT_EXC_GP 0x0D
#define IDT_EXC_PF 0x0E
#define IDT_IRQ0_TIMER 0x20
#define IDT_IRQ1_KEYBOARD 0x21

#define IDT_GATE_PRESENT (1 << 7)
#define IDT_GATE_DPL0 (0b00 << 5)
#define IDT_GATE_DPL1 (0b01 << 5)
#define IDT_GATE_DPL2 (0b10 << 5)
#define IDT_GATE_DPL3 (0b11 << 5)
#define IDT_GATE_TYPE_INTERRUPT 0xE

#define IDT_ENTRY_TYPE_INTERRUPT (IDT_GATE_PRESENT | IDT_GATE_DPL0 | IDT_GATE_TYPE_INTERRUPT)

struct IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct IdtPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct IdtEntry idt[256] __attribute__((aligned(16)));
struct IdtPtr idt_ptr;

static uint8_t idt_is_initialized = 0;
void (*idt_handler_timer_user)();
void (*idt_handler_keyboard_user)();

extern void idt_load(struct IdtPtr* idt_ptr);
extern void idt_handler_exception_de_wrapped();
extern void idt_handler_exception_gp_wrapped();
extern void idt_handler_exception_pf_wrapped();
extern void idt_handler_timer_wrapped();
extern void idt_handler_keyboard_wrapped();

void idt_set_entry(uint8_t vector, uint64_t isr_addr, uint16_t selector, uint8_t type) {
    idt[vector] = (struct IdtEntry) {
        .offset_low = (uint16_t) (isr_addr >> 0),
        .selector = selector,
        .ist = 0,
        .type = type,
        .offset_mid = (uint16_t) (isr_addr >> 16),
        .offset_high = (uint32_t) (isr_addr >> 32),
        .reserved = 0,
    };
}

static void idt_panic_exception(char* name, uint64_t error_code, uint8_t has_error_code, uint8_t has_fault_address) {
    asm volatile("cli");

    print_clear();
    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_RED);
    print_str(" KERNEL PANIC ");
    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_str("\nCPU exception: ");
    print_str(name);
    print_char('\n');

    if (has_error_code) {
        print_str("error_code=0x");
        print_uint64_hex(error_code);
        print_char('\n');
    }

    if (has_fault_address) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        print_str("cr2=0x");
        print_uint64_hex(cr2);
        print_char('\n');
    }

    print_str("System halted.\n");

    while (1) {
        asm volatile("hlt");
    }
}

void idt_handler_exception_de() {
    idt_panic_exception("#DE Divide Error", 0, 0, 0);
}

void idt_handler_exception_gp(uint64_t error_code) {
    idt_panic_exception("#GP General Protection Fault", error_code, 1, 0);
}

void idt_handler_exception_pf(uint64_t error_code) {
    idt_panic_exception("#PF Page Fault", error_code, 1, 1);
}

void idt_handler_keyboard() {
    if (idt_handler_keyboard_user != NULL) {
        idt_handler_keyboard_user();
    }

    pic_eoi_master();
}

void idt_handler_timer() {
    if (idt_handler_timer_user != NULL) {
        idt_handler_timer_user();
    }

    pic_eoi_master();
}

void idt_init() {
    if (idt_is_initialized) {
        return;
    }

    pic_remap();

    idt_ptr.limit = (sizeof(struct IdtEntry) * 256) - 1;
    idt_ptr.base = (uint64_t) &idt;

    idt_set_entry(IDT_EXC_DE, (uint64_t) idt_handler_exception_de_wrapped, GDT_SELECTOR_CS_KERNEL, IDT_ENTRY_TYPE_INTERRUPT);
    idt_set_entry(IDT_EXC_GP, (uint64_t) idt_handler_exception_gp_wrapped, GDT_SELECTOR_CS_KERNEL, IDT_ENTRY_TYPE_INTERRUPT);
    idt_set_entry(IDT_EXC_PF, (uint64_t) idt_handler_exception_pf_wrapped, GDT_SELECTOR_CS_KERNEL, IDT_ENTRY_TYPE_INTERRUPT);
    idt_set_entry(IDT_IRQ0_TIMER, (uint64_t) idt_handler_timer_wrapped, GDT_SELECTOR_CS_KERNEL, IDT_ENTRY_TYPE_INTERRUPT);
    idt_set_entry(IDT_IRQ1_KEYBOARD, (uint64_t) idt_handler_keyboard_wrapped, GDT_SELECTOR_CS_KERNEL, IDT_ENTRY_TYPE_INTERRUPT);

    idt_load(&idt_ptr);

    asm volatile("sti");
    idt_is_initialized = 1;
}

void idt_set_handler_timer(void (*handler)()) {
    idt_handler_timer_user = handler;
}

void idt_set_handler_keyboard(void (*handler)()) {
    idt_handler_keyboard_user = handler;
}
