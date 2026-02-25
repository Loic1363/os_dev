#include <stdint.h>

#include "x86_64/port.h"

#define PORT_RTC_COMMAND 0x70
#define PORT_RTC_DATA 0x71

#define RTC_REGISTER_SECONDS 0x00
#define RTC_REGISTER_MINUTES 0x02
#define RTC_REGISTER_HOURS 0x04
#define RTC_REGISTER_STATUS_A 0x0A
#define RTC_REGISTER_STATUS_B 0x0B

#define RTC_UPDATE_IN_PROGRESS 0x80
#define RTC_DATA_MODE (1 << 2)

uint8_t rtc_read_register(uint8_t reg) {
    port_outb(PORT_RTC_COMMAND, reg);
    return port_inb(PORT_RTC_DATA);
}

void rtc_wait() {
    while (rtc_read_register(RTC_REGISTER_STATUS_A) & RTC_UPDATE_IN_PROGRESS) {
    }
}

uint8_t rtc_is_bcd() {
    return !(rtc_read_register(RTC_REGISTER_STATUS_B) & RTC_DATA_MODE);
}

static uint8_t rtc_bcd_to_bin(uint8_t value) {
    return (value & 0x0F) + ((value & 0xF0) >> 4) * 10;
}

static uint8_t rtc_read_stable(uint8_t reg) {
    uint8_t value_a;
    uint8_t value_b;
    uint8_t is_bcd = rtc_is_bcd();

    do {
        rtc_wait();
        value_a = rtc_read_register(reg);
        rtc_wait();
        value_b = rtc_read_register(reg);
    } while (value_a != value_b);

    if (is_bcd) {
        return rtc_bcd_to_bin(value_b);
    }

    return value_b;
}

uint8_t rtc_hours() {
    uint8_t hours = rtc_read_stable(RTC_REGISTER_HOURS);
    return hours & 0x7F;
}

uint8_t rtc_minutes() {
    return rtc_read_stable(RTC_REGISTER_MINUTES);
}

uint8_t rtc_seconds() {
    return rtc_read_stable(RTC_REGISTER_SECONDS);
}
