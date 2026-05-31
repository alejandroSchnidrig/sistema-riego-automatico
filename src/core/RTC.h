#pragma once
#include "Arduino.h"

// Abstracción de funciones de hardware para permitir tests unitarios en entorno nativo (PC) sin DS1302.

struct RTC_Time {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    
    // Default constructor for valid default state
    RTC_Time(uint16_t y = 2000, uint8_t m = 1, uint8_t d = 1, 
             uint8_t h = 0, uint8_t min = 0, uint8_t s = 0)
        : year(y), month(m), day(d), hour(h), minute(min), second(s) {}
};

void hal_rtc_begin(uint8_t rst, uint8_t dat, uint8_t clk);
RTC_Time hal_rtc_now();
bool hal_rtc_set_time(const RTC_Time& t);
bool hal_rtc_writeProtect(bool enable);