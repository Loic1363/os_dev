#include "print.h"
#include "bool.h"
#include "keyboard.h"
#include "x86_64/rtc.h"

#define KEY_CODE_1 0x02
#define KEY_CODE_2 0x03
#define KEY_CODE_3 0x04
#define KEY_CODE_4 0x05
#define KEY_CODE_5 0x06
#define KEY_CODE_6 0x07
#define KEY_CODE_7 0x08
#define KEY_CODE_8 0x09
#define KEY_CODE_9 0x0A
#define KEY_CODE_0 0x0B

#define KEY_CODE_A 0x1E
#define KEY_CODE_B 0x30
#define KEY_CODE_C 0x2E
#define KEY_CODE_D 0x20
#define KEY_CODE_E 0x12
#define KEY_CODE_F 0x21
#define KEY_CODE_G 0x22
#define KEY_CODE_H 0x23
#define KEY_CODE_I 0x17
#define KEY_CODE_J 0x24
#define KEY_CODE_K 0x25
#define KEY_CODE_L 0x26
#define KEY_CODE_M 0x32
#define KEY_CODE_N 0x31
#define KEY_CODE_O 0x18
#define KEY_CODE_P 0x19
#define KEY_CODE_Q 0x10
#define KEY_CODE_R 0x13
#define KEY_CODE_S 0x1F
#define KEY_CODE_T 0x14
#define KEY_CODE_U 0x16
#define KEY_CODE_V 0x2F
#define KEY_CODE_W 0x11
#define KEY_CODE_X 0x2D
#define KEY_CODE_Y 0x15
#define KEY_CODE_Z 0x2C

#define KEY_CODE_LSHIFT 0x2A
#define KEY_CODE_RSHIFT 0x36
#define KEY_CODE_CAPSLOCK 0x3A
#define KEY_CODE_ALTGR 0xE038
#define KEY_CODE_SPACE 0x39
#define KEY_CODE_ENTER 0x1C

static bool g_shift_left = false;
static bool g_shift_right = false;
static bool g_altgr = false;
static bool g_caps_lock = false;

static bool is_alpha(char c) {
    return c >= 'a' && c <= 'z';
}

static char apply_case(char c, bool shift, bool caps_lock) {
    if (!is_alpha(c)) {
        return c;
    }

    if ((shift && !caps_lock) || (!shift && caps_lock)) {
        return c - ('a' - 'A');
    }

    return c;
}

char to_ascii(uint16_t code, bool shift, bool altgr, bool caps_lock) {
    if (altgr) {
        switch (code) {
            case KEY_CODE_1: return '|';
            case KEY_CODE_2: return '@';
            case KEY_CODE_3: return '#';
            case KEY_CODE_7: return '{';
            case KEY_CODE_8: return '[';
            case KEY_CODE_9: return ']';
            case KEY_CODE_0: return '}';
            default: return 0;
        }
    }

    switch (code) {
        // Belgian AZERTY letter positions (set 1 scan codes)
        case KEY_CODE_Q: return apply_case('a', shift, caps_lock);
        case KEY_CODE_W: return apply_case('z', shift, caps_lock);
        case KEY_CODE_A: return apply_case('q', shift, caps_lock);
        case KEY_CODE_Z: return apply_case('w', shift, caps_lock);
        case KEY_CODE_E: return apply_case('e', shift, caps_lock);
        case KEY_CODE_R: return apply_case('r', shift, caps_lock);
        case KEY_CODE_T: return apply_case('t', shift, caps_lock);
        case KEY_CODE_Y: return apply_case('y', shift, caps_lock);
        case KEY_CODE_U: return apply_case('u', shift, caps_lock);
        case KEY_CODE_I: return apply_case('i', shift, caps_lock);
        case KEY_CODE_O: return apply_case('o', shift, caps_lock);
        case KEY_CODE_P: return apply_case('p', shift, caps_lock);
        case KEY_CODE_S: return apply_case('s', shift, caps_lock);
        case KEY_CODE_D: return apply_case('d', shift, caps_lock);
        case KEY_CODE_F: return apply_case('f', shift, caps_lock);
        case KEY_CODE_G: return apply_case('g', shift, caps_lock);
        case KEY_CODE_H: return apply_case('h', shift, caps_lock);
        case KEY_CODE_J: return apply_case('j', shift, caps_lock);
        case KEY_CODE_K: return apply_case('k', shift, caps_lock);
        case KEY_CODE_L: return apply_case('l', shift, caps_lock);
        case KEY_CODE_M: return apply_case('m', shift, caps_lock);
        case KEY_CODE_X: return apply_case('x', shift, caps_lock);
        case KEY_CODE_C: return apply_case('c', shift, caps_lock);
        case KEY_CODE_V: return apply_case('v', shift, caps_lock);
        case KEY_CODE_B: return apply_case('b', shift, caps_lock);
        case KEY_CODE_N: return apply_case('n', shift, caps_lock);

        // Number row (Belgian: digits with Shift)
        case KEY_CODE_1: return shift ? '1' : '&';
        case KEY_CODE_2: return shift ? '2' : 'e'; // 'é' fallback in ASCII-only text mode
        case KEY_CODE_3: return shift ? '3' : '"';
        case KEY_CODE_4: return shift ? '4' : '\'';
        case KEY_CODE_5: return shift ? '5' : '(';
        case KEY_CODE_6: return shift ? '6' : 0;   // '§' not ASCII
        case KEY_CODE_7: return shift ? '7' : 'e'; // 'è' fallback
        case KEY_CODE_8: return shift ? '8' : '!';
        case KEY_CODE_9: return shift ? '9' : 'c'; // 'ç' fallback
        case KEY_CODE_0: return shift ? '0' : 'a'; // 'à' fallback

        case KEY_CODE_SPACE: return ' ';
        case KEY_CODE_ENTER: return '\n';
        default: return 0;
    }
}

void handle_input(struct KeyboardEvent event) {
    if (event.code == KEY_CODE_LSHIFT) {
        g_shift_left = (event.type == KEYBOARD_EVENT_TYPE_MAKE);
        return;
    }

    if (event.code == KEY_CODE_RSHIFT) {
        g_shift_right = (event.type == KEYBOARD_EVENT_TYPE_MAKE);
        return;
    }

    if (event.code == KEY_CODE_ALTGR) {
        g_altgr = (event.type == KEYBOARD_EVENT_TYPE_MAKE);
        return;
    }

    if (event.code == KEY_CODE_CAPSLOCK && event.type == KEYBOARD_EVENT_TYPE_MAKE) {
        g_caps_lock = !g_caps_lock;
        return;
    }

    if (event.type == KEYBOARD_EVENT_TYPE_MAKE) {
        bool shift = g_shift_left || g_shift_right;
        char ch = to_ascii(event.code, shift, g_altgr, g_caps_lock);
        if (ch == 0) {
            return;
        }

        print_set_color(PRINT_COLOR_BLUE, PRINT_COLOR_WHITE);
        print_char(ch);
    } else if (event.type == KEYBOARD_EVENT_TYPE_BREAK) {
    }
}

void kernel_main() {
    print_clear();
    print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
    print_str("Welcome to our 64-bit kernel!");

    keyboard_init();
    keyboard_set_handler(handle_input);

    for (uint64_t i = 0; i < 100; i++) {
        print_uint64_dec(i);
        print_char(' ');
    }

    uint8_t prev_seconds = 0;

    for (uint8_t i = 0; i < 5;) {
        uint8_t seconds = rtc_seconds();

        if (seconds != prev_seconds) {
            i++;
            print_set_color(PRINT_COLOR_GREEN, PRINT_COLOR_BLACK);
            print_str("\nSeconds: ");
            print_uint64_dec(seconds);
        }

        prev_seconds = seconds;
    }

    print_str(" - Seconds loop disabled.\n");

    while (1) {
    }
}
