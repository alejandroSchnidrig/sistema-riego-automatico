#include "RTCManager.h"

RTCManager::RTCManager(uint8_t rst, uint8_t dat, uint8_t clk)
{
    hal_rtc_begin(rst, dat, clk);
}

// ============================================================
// Inicialización de hardware
// ============================================================

void RTCManager::begin() {
  // HAL already began in constructor or could be initialized here again
  // Some HAL might need re-init, but we assume it's ready.
  // Delay for stabilization
  delay(50);

  RTC_Time t = hal_rtc_now();
  Serial.print("Lectura RTC: ");
  Serial.print(formatDate(t));
  Serial.print(" ");
  Serial.println(formatTime(t));

  if (!isValid(t)) {
    Serial.println("RTC con datos invalidos, intentando inicializar...");

    bool initOk = false;
    const RTC_Time defaultTime(2024, 1, 1, 12, 0, 0);

    for (int attempt = 0; attempt < 5; attempt++) {
      Serial.print("Intento ");
      Serial.println(attempt + 1);
      
      delay(20);
      hal_rtc_set_time(defaultTime);
      delay(100);

      RTC_Time verify = hal_rtc_now();
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

  hal_rtc_writeProtect(true);
}

// ============================================================
// API pública
// ============================================================

RTC_Time RTCManager::now() {
  return hal_rtc_now();
}

bool RTCManager::setTime(uint16_t year, uint8_t month, uint8_t day,
                          uint8_t hour, uint8_t minute, uint8_t second) {
  if (!isValidDateTime(year, month, day, hour, minute, second)) return false;

  RTC_Time newTime(year, month, day, hour, minute, second);
  return hal_rtc_set_time(newTime);
}

bool RTCManager::isValid(const RTC_Time& t) const {
  return isValidDateTime(t.year, t.month, t.day, t.hour, t.minute, t.second);
}

// ============================================================
// Helpers de formateo (estáticos)
// ============================================================

String RTCManager::formatDate(const RTC_Time& t) {
  return String(t.year) + "/" + twoDigits(t.month) + "/" + twoDigits(t.day);
}

String RTCManager::formatTime(const RTC_Time& t) {
  return twoDigits(t.hour) + ":" + twoDigits(t.minute) + ":" + twoDigits(t.second);
}

// ============================================================
// Helpers de calendario (estáticos)
// ============================================================

uint8_t RTCManager::dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day) {
  // 0=Dom … 6=Sáb. DS1302 usually expects 1=Sun. We'll use calculateDayOfWeek directly.
  const uint8_t dow = calculateDayOfWeek(year, month, day); // 1..7 (1=Sun)
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

uint8_t RTCManager::calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day) {
  // Algoritmo de Tomohiko Sakamoto — devuelve 0=Dom … 6=Sáb; se ajusta a 1=Sun .. 7=Sat
  static const int monthTable[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (month < 3) y -= 1;
  int dow = (y + y / 4 - y / 100 + y / 400 + monthTable[month - 1] + day) % 7;
  return static_cast<uint8_t>(dow + 1);
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
