#pragma once
#include <Arduino.h>
#include <DS1302.h>
#include <stdint.h>

class RTCManager {
public:
  RTCManager(uint8_t rst, uint8_t dat, uint8_t clk);

  void    begin();
  Time    now();
  bool    setTime(uint16_t year, uint8_t month, uint8_t day,
                  uint8_t hour,  uint8_t minute, uint8_t second);
  bool    isValid(const Time& t) const;

  static String  formatDate(const Time& t);
  static String  formatTime(const Time& t);
  static uint8_t dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day);
  static bool    parseHourMinute(const char* value, uint8_t& hour, uint8_t& minute);

private:
  DS1302 _rtc;

  static String     twoDigits(uint8_t value);
  static Time::Day  calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day);
  static bool       isLeapYear(uint16_t year);
  static uint8_t    daysInMonth(uint16_t year, uint8_t month);
  static bool       isValidDateTime(uint16_t year, uint8_t month, uint8_t day,
                                    uint8_t hour,  uint8_t minute, uint8_t second);
};
