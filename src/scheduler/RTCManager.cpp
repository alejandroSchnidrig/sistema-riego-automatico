#include "RTCManager.h"
#include <Arduino.h>

RTCManager::RTCManager(uint8_t rst, uint8_t dat, uint8_t clk)
  : _rtc(rst, dat, clk)
{}

// ============================================================
// Inicialización de hardware
// ============================================================

void RTCManager::begin() {
  _rtc.writeProtect(false);
  _rtc.halt(false);
  delay(50);

  Time t = _rtc.time();
  Serial.print("Lectura RTC: ");
  Serial.print(formatDate(t));
  Serial.print(" ");
  Serial.println(formatTime(t));

  if (!isValid(t)) {
    Serial.println("RTC con datos invalidos, intentando inicializar...");

    bool initOk = false;
    const Time defaultTime(2024, 1, 1, 12, 0, 0, calculateDayOfWeek(2024, 1, 1));

    for (int attempt = 0; attempt < 5; attempt++) {
      Serial.print("Intento ");
      Serial.println(attempt + 1);
      _rtc.halt(false);
      delay(20);
      _rtc.time(defaultTime);
      delay(100);

      Time verify = _rtc.time();
      Serial.print("  Verificacion: ");
      Serial.print(formatDate(verify));
      Serial.print(" ");
      Serial.println(formatTime(verify));

      if (isValid(verify)) {
        Serial.println("RTC inicializado correctamente.");
        initOk = true;
        break;
      }
    }

    if (!initOk) Serial.println("ADVERTENCIA: El RTC sigue reportando datos invalidos.");
  } else {
    Serial.println("RTC con datos validos, no se requiere inicializacion.");
  }

  _rtc.writeProtect(true);
}

// ============================================================
// API pública
// ============================================================

Time RTCManager::now() {
  return _rtc.time();
}

bool RTCManager::setTime(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second) {
  if (!isValidDateTime(year, month, day, hour, minute, second)) return false;

  Time::Day dow     = calculateDayOfWeek(year, month, day);
  Time      newTime(year, month, day, hour, minute, second, dow);

  // Secuencia requerida por el DS1302: deshabilitar write-protect, limpiar halt, escribir, limpiar halt, re-habilitar write-protect
  _rtc.writeProtect(false);
  delay(10);
  _rtc.halt(false);
  delay(10);
  _rtc.time(newTime);
  delay(50);
  _rtc.halt(false);
  _rtc.writeProtect(true);
  return true;
}

bool RTCManager::isValid(const Time& t) const {
  return isValidDateTime(t.yr, t.mon, t.date, t.hr, t.min, t.sec);
}

// ============================================================
// Helpers de formateo (estáticos)
// ============================================================

String RTCManager::formatDate(const Time& t) {
  return String(t.yr) + "/" + twoDigits(t.mon) + "/" + twoDigits(t.date);
}

String RTCManager::formatTime(const Time& t) {
  return twoDigits(t.hr) + ":" + twoDigits(t.min) + ":" + twoDigits(t.sec);
}

// ============================================================
// Helpers de calendario (estáticos)
// ============================================================

uint8_t RTCManager::dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day) {
  // El DS1302 numera: Domingo=1, Lunes=2 … Sábado=7. Se convierte al bitmask interno (lun=bit0 … dom=bit6).
  const uint8_t dow = (uint8_t)calculateDayOfWeek(year, month, day);
  switch (dow) {
    case 2: return 0;   // Lunes
    case 3: return 1;   // Martes
    case 4: return 2;   // Miércoles
    case 5: return 3;   // Jueves
    case 6: return 4;   // Viernes
    case 7: return 5;   // Sábado
    case 1: return 6;   // Domingo
    default: return 255;
  }
}

bool RTCManager::parseHourMinute(const char* value, uint8_t& hour, uint8_t& minute) {
  if (value == nullptr    ||
      strlen(value) != 5  ||
      !isDigit(value[0])  ||
      !isDigit(value[1])  ||
      value[2] != ':'     ||
      !isDigit(value[3])  ||
      !isDigit(value[4])) {
    return false;
  }
  hour   = (uint8_t)((value[0] - '0') * 10 + (value[1] - '0'));
  minute = (uint8_t)((value[3] - '0') * 10 + (value[4] - '0'));
  return hour < 24 && minute < 60;
}

// ============================================================
// Helpers privados
// ============================================================

String RTCManager::twoDigits(uint8_t value) {
  return value < 10 ? "0" + String(value) : String(value);
}

Time::Day RTCManager::calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day) {
  // Algoritmo de Tomohiko Sakamoto — devuelve 0=Dom … 6=Sáb; se ajusta a Time::Day sumando 1.
  static const int monthTable[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (month < 3) y -= 1;
  int dow = (y + y / 4 - y / 100 + y / 400 + monthTable[month - 1] + day) % 7;
  return static_cast<Time::Day>(dow + 1);
}

bool RTCManager::isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t RTCManager::daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && isLeapYear(year)) return 29;
  return daysPerMonth[month - 1];
}

bool RTCManager::isValidDateTime(uint16_t year, uint8_t month, uint8_t day,
                                  uint8_t hour, uint8_t minute, uint8_t second) {
  if (year   < 2000 || year > 2099)                    return false;
  if (month  < 1    || month > 12)                     return false;
  if (day    < 1    || day > daysInMonth(year, month)) return false;
  if (hour   > 23)                                     return false;
  if (minute > 59)                                     return false;
  if (second > 59)                                     return false;
  return true;
}
