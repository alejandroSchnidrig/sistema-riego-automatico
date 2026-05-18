#include "../core/RTC.h"

#ifndef NATIVE_TEST

#include <DS1302.h>
#include <Arduino.h>

static DS1302* rtc_instance = nullptr;

void hal_rtc_begin(uint8_t rst, uint8_t dat, uint8_t clk) {
    if (rtc_instance == nullptr) {
        rtc_instance = new DS1302(rst, dat, clk);
    }
}

RTC_Time hal_rtc_now() {
    if (rtc_instance) {
        Time t = rtc_instance->time();
        return RTC_Time(t.yr, t.mon, t.date, t.hr, t.min, t.sec);
    }
    return RTC_Time();
}

bool hal_rtc_set_time(const RTC_Time& t) {
    if (rtc_instance) {
        Time d(t.year, t.month, t.day, t.hour, t.minute, t.second, Time::kSunday); // Day of week doesn't matter for setting exact date if not used strictly
        rtc_instance->time(d);
        return true;
    }
    return false;
}

bool hal_rtc_writeProtect(bool enable) {
    if (rtc_instance) {
        rtc_instance->writeProtect(enable);
        return true;
    }
    return false;
}

#endif // ARDUINO
