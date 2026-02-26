#include <drivers/rtc.h>

#include <drivers/serial.h>
#include <kernel/io.h>
#include <kernel/string.h>

#define CMOS_ADDR_PORT 0x70u
#define CMOS_DATA_PORT 0x71u

#define CMOS_REG_SECONDS 0x00u
#define CMOS_REG_MINUTES 0x02u
#define CMOS_REG_HOURS   0x04u
#define CMOS_REG_WEEKDAY 0x06u
#define CMOS_REG_DAY     0x07u
#define CMOS_REG_MONTH   0x08u
#define CMOS_REG_YEAR    0x09u
#define CMOS_REG_STATUS_A 0x0Au
#define CMOS_REG_STATUS_B 0x0Bu
#define CMOS_REG_CENTURY  0x32u

static uint8_t rtc_read_cmos(uint8_t reg) {
    outb(CMOS_ADDR_PORT, reg);
    return inb(CMOS_DATA_PORT);
}

static bool rtc_update_in_progress(void) {
    return (rtc_read_cmos(CMOS_REG_STATUS_A) & 0x80u) != 0u;
}

static uint8_t rtc_bcd_to_bin(uint8_t value) {
    return (uint8_t)((value & 0x0Fu) + ((value >> 4) * 10u));
}

static void rtc_capture_raw(rtc_time_t* out_time, uint8_t* out_status_b, uint8_t* out_century) {
    out_time->second = rtc_read_cmos(CMOS_REG_SECONDS);
    out_time->minute = rtc_read_cmos(CMOS_REG_MINUTES);
    out_time->hour = rtc_read_cmos(CMOS_REG_HOURS);
    out_time->day = rtc_read_cmos(CMOS_REG_DAY);
    out_time->month = rtc_read_cmos(CMOS_REG_MONTH);
    out_time->year = rtc_read_cmos(CMOS_REG_YEAR);
    (void)rtc_read_cmos(CMOS_REG_WEEKDAY);
    *out_status_b = rtc_read_cmos(CMOS_REG_STATUS_B);
    *out_century = rtc_read_cmos(CMOS_REG_CENTURY);
}

static void rtc_serial_write_2d(uint8_t value) {
    serial_write_char((char)('0' + (value / 10u)));
    serial_write_char((char)('0' + (value % 10u)));
}

bool rtc_read_time(rtc_time_t* out_time) {
    if (!out_time) {
        return false;
    }

    rtc_time_t first;
    rtc_time_t second;
    uint8_t status_b_first = 0u;
    uint8_t status_b_second = 0u;
    uint8_t century_first = 0u;
    uint8_t century_second = 0u;

    bool stable = false;
    for (uint8_t retry = 0u; retry < 8u; retry++) {
        while (rtc_update_in_progress()) {
            __asm__ volatile("pause");
        }
        rtc_capture_raw(&first, &status_b_first, &century_first);

        while (rtc_update_in_progress()) {
            __asm__ volatile("pause");
        }
        rtc_capture_raw(&second, &status_b_second, &century_second);

        if (memcmp(&first, &second, sizeof(rtc_time_t)) == 0 &&
            status_b_first == status_b_second &&
            century_first == century_second) {
            stable = true;
            break;
        }
    }

    if (!stable) {
        return false;
    }

    bool binary_mode = (status_b_second & 0x04u) != 0u;
    bool hour_24_mode = (status_b_second & 0x02u) != 0u;
    bool pm_flag = (second.hour & 0x80u) != 0u;

    uint8_t year = (uint8_t)(second.year & 0xFFu);
    uint8_t month = second.month;
    uint8_t day = second.day;
    uint8_t hour = (uint8_t)(second.hour & 0x7Fu);
    uint8_t minute = second.minute;
    uint8_t second_value = second.second;
    uint8_t century = century_second;

    if (!binary_mode) {
        year = rtc_bcd_to_bin(year);
        month = rtc_bcd_to_bin(month);
        day = rtc_bcd_to_bin(day);
        hour = rtc_bcd_to_bin(hour);
        minute = rtc_bcd_to_bin(minute);
        second_value = rtc_bcd_to_bin(second_value);
        if (century != 0u && century != 0xFFu) {
            century = rtc_bcd_to_bin(century);
        }
    }

    if (!hour_24_mode) {
        if (pm_flag && hour < 12u) {
            hour = (uint8_t)(hour + 12u);
        }
        if (!pm_flag && hour == 12u) {
            hour = 0u;
        }
    }

    uint16_t full_year;
    if (century != 0u && century != 0xFFu) {
        full_year = (uint16_t)((uint16_t)century * 100u + year);
    } else {
        full_year = (year >= 70u) ? (uint16_t)(1900u + year) : (uint16_t)(2000u + year);
    }

    out_time->year = full_year;
    out_time->month = month;
    out_time->day = day;
    out_time->hour = hour;
    out_time->minute = minute;
    out_time->second = second_value;

    return true;
}

void rtc_init(void) {
    rtc_time_t now;
    if (!rtc_read_time(&now)) {
        serial_write("[RTC] CMOS clock read failed\n");
        return;
    }

    serial_write("[RTC] CMOS time ");
    serial_write_dec(now.year);
    serial_write_char('-');
    rtc_serial_write_2d(now.month);
    serial_write_char('-');
    rtc_serial_write_2d(now.day);
    serial_write_char(' ');
    rtc_serial_write_2d(now.hour);
    serial_write_char(':');
    rtc_serial_write_2d(now.minute);
    serial_write_char(':');
    rtc_serial_write_2d(now.second);
    serial_write("\n");
}
