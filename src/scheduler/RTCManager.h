#pragma once
#include "../core/RTC.h"
#include <stdint.h>

class RTCManager {
public:
  RTCManager(uint8_t rst, uint8_t dat, uint8_t clk);

  // Estabiliza el RTC, lee la hora y, si es inválida, la inicializa. Llamar desde setup().
  void begin();
  // hora actual leída del RTC
  RTC_Time now();  
  // Fija la hora del RTC. Devuelve false si la fecha/hora es inválida.
  bool setTime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour,  uint8_t minute, uint8_t second);
  bool isValid(const RTC_Time& t) const;   // ¿la fecha/hora es válida?

  static String formatDate(const RTC_Time& t);  // "YYYY/MM/DD"
  static String formatTime(const RTC_Time& t);  // "HH:MM:SS"
  // Fecha → bit de día del bitmask de programa (0=lunes … 6=domingo).
  static uint8_t dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day);
  // Parsea "HH:MM" a hora y minuto. Devuelve false si el formato es inválido.
  static bool parseHourMinute(const char* value, uint8_t& hour, uint8_t& minute);

private:
  static String twoDigits(uint8_t value);    // formatea un número a 2 dígitos ("07")
  // Día de la semana de una fecha (1=domingo … 7=sábado), por algoritmo de Sakamoto.
  static uint8_t calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day);
  static bool isLeapYear(uint16_t year);  // ¿año bisiesto?
  static uint8_t daysInMonth(uint16_t year, uint8_t month); // días del mes (contempla bisiestos)
  // Valida un conjunto fecha/hora completo (rangos de año, mes, día, hora, etc.).
  static bool isValidDateTime(uint16_t year, uint8_t month, uint8_t day,
                                uint8_t hour,  uint8_t minute, uint8_t second);
};
