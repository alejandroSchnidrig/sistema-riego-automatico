#pragma once
#include "../core/RTC.h"
#include <stdint.h>

class RTCManager {
public:
  RTCManager(uint8_t rst, uint8_t dat, uint8_t clk);

  void       begin();
  RTC_Time   now();
  bool       setTime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour,  uint8_t minute, uint8_t second);
  bool       isValid(const RTC_Time& t) const;

  static String  formatDate(const RTC_Time& t);
  static String  formatTime(const RTC_Time& t);
  static uint8_t dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day);
  static bool    parseHourMinute(const char* value, uint8_t& hour, uint8_t& minute);

private:
  static String     twoDigits(uint8_t value);
  static uint8_t    calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day);
  static bool       isLeapYear(uint16_t year);
  static uint8_t    daysInMonth(uint16_t year, uint8_t month);
  static bool       isValidDateTime(uint16_t year, uint8_t month, uint8_t day,
                                    uint8_t hour,  uint8_t minute, uint8_t second);
};
