#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DS1302.h>
#include "pages/index_html.h"

/*
  ESP32 - Sistema de Riego Automático
  - Sirve el HTML subido por el usuario
  - GPIO18 = RTC CLK
  - GPIO19 = RTC DAT
  - GPIO21 = RTC RST
  - Sectores 1-8 en GPIO 13, 14, 16, 17, 32, 33, 25, 26
  - Estado por Serial cada 20 segundos
  - Backend compatible con:
      GET  /estado
      GET  /programas
      POST /configuracion
      POST /parada

  Pensado para PlatformIO:
  - guardar como src/main_riego.cpp
*/

WebServer server(80);

static const char* AP_SSID = "Riego-ESP32";
static const char* AP_PASS = "riego12345";

// RTC Configuration
static const uint8_t RTC_CLK_PIN = 18;
static const uint8_t RTC_DAT_PIN = 19;
static const uint8_t RTC_RST_PIN = 21;
DS1302 rtc(RTC_RST_PIN, RTC_DAT_PIN, RTC_CLK_PIN);

// Sector Pin Configuration (GPIO pins for sectors 1-8)
static const uint8_t SECTOR_PINS[8] = {13, 14, 16, 17, 32, 33, 25, 26};

// Pump relay pin
static const uint8_t PUMP_PIN = 27;

static const uint8_t NUM_SECTORS = 8;
static const uint8_t MAX_PROGRAMS = 10;
static const unsigned long STATUS_PRINT_INTERVAL_MS = 20000UL;

enum SystemState {
  STATE_IDLE,
  STATE_RUNNING,
  STATE_MANUAL_STOP
};

struct SectorStep {
  uint8_t id;
  uint8_t orden;
  uint32_t tiempoRiego;
};

struct Program {
  bool valid;
  uint16_t id;
  char horaInicio[6];
  uint8_t dias;
  uint16_t retardoEntreSectores;
  bool ciclico;
  uint8_t sectorCount;
  SectorStep sectores[NUM_SECTORS];
};

Program programs[MAX_PROGRAMS];
uint16_t nextProgramId = 3;

SystemState systemState = STATE_IDLE;
uint16_t activeProgramId = 0;
uint8_t activeSectorId = 0;
uint32_t remainingTimeSec = 0;
bool pumpOn = false;

// Manual control override
uint16_t manualSectorMask = 0;  // bit0=sector1 ... bit7=sector8

int runningProgramIndex = -1;
int runningStepIndex = -1;
bool waitingBetweenSectors = false;
unsigned long stepStartMs = 0;
unsigned long delayStartMs = 0;
unsigned long lastStatusPrint = 0;
uint16_t lastScheduleYear = 0;
uint8_t lastScheduleMonth = 0;
uint8_t lastScheduleDay = 0;
uint8_t lastScheduleHour = 255;
uint8_t lastScheduleMinute = 255;

// Controls pump relay output
void setPump(bool on) {
  pumpOn = on;
  digitalWrite(PUMP_PIN, on ? HIGH : LOW);
}

uint16_t sectorIdToMask(uint8_t sectorId) {
  if (sectorId < 1 || sectorId > NUM_SECTORS) {
    return 0;
  }

  return (uint16_t)1U << (sectorId - 1);
}

uint8_t firstSectorFromMask(uint16_t sectorMask) {
  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    if ((sectorMask & sectorIdToMask(i)) != 0) {
      return i;
    }
  }

  return 0;
}

uint16_t getOutputSectorMask() {
  return manualSectorMask | sectorIdToMask(activeSectorId);
}

bool isSectorActive(uint8_t sectorId) {
  return (getOutputSectorMask() & sectorIdToMask(sectorId)) != 0;
}

// Applies the full sector mask to the hardware outputs
void setSectorHardware(uint16_t sectorMask) {
  for (uint8_t i = 0; i < NUM_SECTORS; i++) {
    digitalWrite(SECTOR_PINS[i], (sectorMask & sectorIdToMask(i + 1)) != 0 ? HIGH : LOW);
  }
}

// Clears all manual overrides so automatic programs can own the outputs
void clearManualOverrides() {
  manualSectorMask = 0;
}

// Applies the current runtime/manual state to the hardware outputs
void applyOutputsFromState() {
  const uint16_t sectorMask = getOutputSectorMask();
  setSectorHardware(sectorMask);
  setPump(sectorMask != 0);
}

bool isManualControlActive() {
  return manualSectorMask != 0;
}

uint8_t getEffectiveSectorId() {
  return firstSectorFromMask(getOutputSectorMask());
}

uint8_t getFirstManualSectorId() {
  return firstSectorFromMask(manualSectorMask);
}

String buildSectorArrayJson(uint16_t sectorMask) {
  String json = "[";
  bool first = true;

  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    if ((sectorMask & sectorIdToMask(i)) == 0) {
      continue;
    }

    if (!first) {
      json += ",";
    }
    first = false;
    json += String(i);
  }

  json += "]";
  return json;
}

String formatSectorMaskForSerial(uint16_t sectorMask) {
  if (sectorMask == 0) {
    return "ninguno";
  }

  String text;
  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    if ((sectorMask & sectorIdToMask(i)) == 0) {
      continue;
    }

    if (text.length() > 0) {
      text += ", ";
    }
    text += "S";
    text += String(i);
  }

  return text;
}

// Converts system state enum to string representation
const char* stateToString(SystemState state) {
  switch (state) {
    case STATE_RUNNING: return "RUNNING";
    case STATE_MANUAL_STOP: return "MANUAL_STOP";
    case STATE_IDLE:
    default: return "IDLE";
  }
}

// Converts boolean to JSON string format
String boolToJson(bool value) {
  return value ? "true" : "false";
}

// Escapes special characters for JSON encoding
String escapeJson(const String& input) {
  String out;
  out.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

// Retrieves request body from WebServer
String getRequestBody() {
  if (server.hasArg("plain")) {
    return server.arg("plain");
  }
  return "";
}

// Extracts integer from string at specified position
bool extractIntAt(const String& src, int startPos, int& value, int& endPos) {
  int i = startPos;
  while (i < (int)src.length() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) {
    i++;
  }

  bool negative = false;
  if (i < (int)src.length() && src[i] == '-') {
    negative = true;
    i++;
  }

  if (i >= (int)src.length() || !isDigit(src[i])) {
    return false;
  }

  long result = 0;
  while (i < (int)src.length() && isDigit(src[i])) {
    result = result * 10 + (src[i] - '0');
    i++;
  }

  value = negative ? -result : result;
  endPos = i;
  return true;
}

// Extracts integer field from JSON object
bool extractIntField(const String& json, const String& key, int& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  int endPos = pos;
  return extractIntAt(json, pos, out, endPos);
}

// Extracts boolean field from JSON object
bool extractBoolField(const String& json, const String& key, bool& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("true", pos)) {
    out = true;
    return true;
  }
  if (json.startsWith("false", pos)) {
    out = false;
    return true;
  }
  return false;
}

// Extracts string field from JSON object
bool extractStringField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (pos >= (int)json.length() || json[pos] != '"') {
    return false;
  }

  pos++;
  int end = json.indexOf('"', pos);
  if (end < 0) return false;

  out = json.substring(pos, end);
  return true;
}

// Extracts optional ID field from JSON, defaults to zero if missing
bool extractNullableId(const String& json, int& out) {
  String pattern = "\"id\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) {
    out = 0;
    return true;
  }

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("null", pos)) {
    out = 0;
    return true;
  }

  int endPos = pos;
  return extractIntAt(json, pos, out, endPos);
}

// Extracts nested object field from JSON
bool extractObjectField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;

  int start = json.indexOf('{', pos);
  if (start < 0) return false;

  int depth = 0;
  for (int i = start; i < (int)json.length(); i++) {
    if (json[i] == '{') depth++;
    else if (json[i] == '}') {
      depth--;
      if (depth == 0) {
        out = json.substring(start, i + 1);
        return true;
      }
    }
  }
  return false;
}

// Extracts array field from JSON
bool extractArrayField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;

  int start = json.indexOf('[', pos);
  if (start < 0) return false;

  int depth = 0;
  for (int i = start; i < (int)json.length(); i++) {
    if (json[i] == '[') depth++;
    else if (json[i] == ']') {
      depth--;
      if (depth == 0) {
        out = json.substring(start, i + 1);
        return true;
      }
    }
  }
  return false;
}

// Sorts program sectors by execution order
void sortProgramSectors(Program& program) {
  for (uint8_t i = 0; i < program.sectorCount; i++) {
    for (uint8_t j = i + 1; j < program.sectorCount; j++) {
      if (program.sectores[j].orden < program.sectores[i].orden) {
        SectorStep tmp = program.sectores[i];
        program.sectores[i] = program.sectores[j];
        program.sectores[j] = tmp;
      }
    }
  }
}

// Parses HH:MM and validates the range
bool parseHourMinute(const char* value, uint8_t& hour, uint8_t& minute) {
  if (value == nullptr ||
      strlen(value) != 5 ||
      !isDigit(value[0]) ||
      !isDigit(value[1]) ||
      value[2] != ':' ||
      !isDigit(value[3]) ||
      !isDigit(value[4])) {
    return false;
  }

  hour = (uint8_t)((value[0] - '0') * 10 + (value[1] - '0'));
  minute = (uint8_t)((value[3] - '0') * 10 + (value[4] - '0'));
  return hour < 24 && minute < 60;
}

// Parses program configuration from JSON string
bool parseProgramFromJson(const String& programJson, Program& outProgram) {
  memset(&outProgram, 0, sizeof(outProgram));
  outProgram.valid = true;

  int id = 0;
  extractNullableId(programJson, id);
  outProgram.id = (uint16_t)id;

  String hora;
  uint8_t startHour = 0;
  uint8_t startMinute = 0;
  if (!extractStringField(programJson, "horaInicio", hora) ||
      !parseHourMinute(hora.c_str(), startHour, startMinute)) {
    return false;
  }
  hora.toCharArray(outProgram.horaInicio, sizeof(outProgram.horaInicio));

  int dias = 0;
  if (!extractIntField(programJson, "dias", dias) || dias < 0 || dias > 0x7F) return false;
  outProgram.dias = (uint8_t)dias;

  int retardo = 0;
  if (!extractIntField(programJson, "retardoEntreSectores", retardo) ||
      retardo < 0 ||
      retardo > 65535) {
    return false;
  }
  outProgram.retardoEntreSectores = (uint16_t)retardo;

  bool ciclico = false;
  if (!extractBoolField(programJson, "ciclico", ciclico)) return false;
  outProgram.ciclico = ciclico;

  String sectoresArray;
  if (!extractArrayField(programJson, "sectores", sectoresArray)) return false;

  int pos = 0;
  while (pos < (int)sectoresArray.length() && outProgram.sectorCount < NUM_SECTORS) {
    int objStart = sectoresArray.indexOf('{', pos);
    if (objStart < 0) break;

    int depth = 0;
    int objEnd = -1;
    for (int i = objStart; i < (int)sectoresArray.length(); i++) {
      if (sectoresArray[i] == '{') depth++;
      else if (sectoresArray[i] == '}') {
        depth--;
        if (depth == 0) {
          objEnd = i;
          break;
        }
      }
    }

    if (objEnd < 0) break;

    String item = sectoresArray.substring(objStart, objEnd + 1);
    int sectorId = 0, orden = 0, tiempo = 0;
    if (extractIntField(item, "id", sectorId) &&
        extractIntField(item, "orden", orden) &&
        extractIntField(item, "tiempoRiego", tiempo)) {
      if (sectorId >= 1 &&
          sectorId <= NUM_SECTORS &&
          orden >= 1 &&
          orden <= NUM_SECTORS &&
          tiempo > 0) {
        SectorStep& s = outProgram.sectores[outProgram.sectorCount++];
        s.id = (uint8_t)sectorId;
        s.orden = (uint8_t)orden;
        s.tiempoRiego = (uint32_t)tiempo;
      }
    }

    pos = objEnd + 1;
  }

  if (outProgram.sectorCount == 0) return false;

  sortProgramSectors(outProgram);
  return true;
}

// Finds program storage index by ID
int findProgramIndexById(uint16_t id) {
  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (programs[i].valid && programs[i].id == id) {
      return i;
    }
  }
  return -1;
}

// Finds next available program storage slot
int findFreeProgramSlot() {
  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (!programs[i].valid) {
      return i;
    }
  }
  return -1;
}

// Stops current program execution and resets runtime state
void stopRuntime(SystemState newState) {
  systemState = newState;
  activeProgramId = 0;
  activeSectorId = 0;
  remainingTimeSec = 0;
  runningProgramIndex = -1;
  runningStepIndex = -1;
  waitingBetweenSectors = false;
  stepStartMs = 0;
  delayStartMs = 0;
  applyOutputsFromState();
}

// Starts specified program step and activates sector
void startStep(int programIndex, int stepIndex) {
  if (programIndex < 0 || !programs[programIndex].valid) {
    stopRuntime(STATE_IDLE);
    return;
  }

  Program& p = programs[programIndex];
  if (stepIndex < 0 || stepIndex >= p.sectorCount) {
    stopRuntime(STATE_IDLE);
    return;
  }

  runningProgramIndex = programIndex;
  runningStepIndex = stepIndex;
  waitingBetweenSectors = false;
  systemState = STATE_RUNNING;
  activeProgramId = p.id;
  activeSectorId = p.sectores[stepIndex].id;
  remainingTimeSec = p.sectores[stepIndex].tiempoRiego;
  stepStartMs = millis();

  applyOutputsFromState();
}

// Starts program execution by ID
bool startProgramById(uint16_t id) {
  int index = findProgramIndexById(id);
  if (index < 0) return false;

  Program& p = programs[index];
  if (p.sectorCount == 0) return false;

  clearManualOverrides();
  startStep(index, 0);
  return true;
}

// Updates program execution state and manages sector timing
void updateRuntime() {
  if (systemState != STATE_RUNNING || runningProgramIndex < 0) {
    return;
  }

  Program& p = programs[runningProgramIndex];
  unsigned long now = millis();

  if (waitingBetweenSectors) {
    remainingTimeSec = 0;
    if (now - delayStartMs >= (unsigned long)p.retardoEntreSectores * 1000UL) {
      int nextStep = runningStepIndex + 1;
      if (nextStep >= p.sectorCount) {
        if (p.ciclico) {
          startStep(runningProgramIndex, 0);
        } else {
          stopRuntime(STATE_IDLE);
        }
      } else {
        startStep(runningProgramIndex, nextStep);
      }
    }
    return;
  }

  uint32_t stepDurationSec = p.sectores[runningStepIndex].tiempoRiego;
  unsigned long elapsedMs = now - stepStartMs;
  uint32_t elapsedSec = (uint32_t)(elapsedMs / 1000UL);

  if (elapsedSec >= stepDurationSec) {
    activeSectorId = 0;
    applyOutputsFromState();

    int nextStep = runningStepIndex + 1;
    if (nextStep >= p.sectorCount) {
      if (p.ciclico) {
        if (p.retardoEntreSectores > 0) {
          waitingBetweenSectors = true;
          delayStartMs = now;
        } else {
          startStep(runningProgramIndex, 0);
        }
      } else {
        stopRuntime(STATE_IDLE);
      }
    } else {
      if (p.retardoEntreSectores > 0) {
        waitingBetweenSectors = true;
        delayStartMs = now;
        remainingTimeSec = 0;
      } else {
        startStep(runningProgramIndex, nextStep);
      }
    }
    return;
  }

  remainingTimeSec = stepDurationSec - elapsedSec;
}

// Builds system status as JSON
String buildEstadoJson() {
  const uint16_t activeSectorMask = getOutputSectorMask();
  String json = "{";
  json += "\"estado\":\"" + String(stateToString(systemState)) + "\",";
  json += "\"programaActivo\":" + String(activeProgramId) + ",";
  json += "\"sectorActivo\":" + String(getEffectiveSectorId()) + ",";
  json += "\"sectoresActivos\":" + buildSectorArrayJson(activeSectorMask) + ",";
  json += "\"tiempoRestante\":" + String(remainingTimeSec) + ",";
  json += "\"bomba\":" + boolToJson(pumpOn) + ",";
  json += "\"modoManual\":" + boolToJson(isManualControlActive()) + ",";
  json += "\"manualSectorMask\":" + String(manualSectorMask) + ",";
  json += "\"manualSectorId\":" + String(getFirstManualSectorId()) + ",";
  json += "\"manualPumpOn\":false";
  json += "}";
  return json;
}

// Builds all programs list as JSON
String buildProgramasJson() {
  String json = "{\"programas\":[";
  bool firstProgram = true;

  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (!programs[i].valid) continue;

    if (!firstProgram) json += ",";
    firstProgram = false;

    Program& p = programs[i];
    json += "{";
    json += "\"id\":" + String(p.id) + ",";
    json += "\"horaInicio\":\"" + escapeJson(String(p.horaInicio)) + "\",";
    json += "\"dias\":" + String(p.dias) + ",";
    json += "\"retardoEntreSectores\":" + String(p.retardoEntreSectores) + ",";
    json += "\"ciclico\":" + boolToJson(p.ciclico) + ",";
    json += "\"sectores\":[";

    for (uint8_t s = 0; s < p.sectorCount; s++) {
      if (s > 0) json += ",";
      json += "{";
      json += "\"id\":" + String(p.sectores[s].id) + ",";
      json += "\"orden\":" + String(p.sectores[s].orden) + ",";
      json += "\"tiempoRiego\":" + String(p.sectores[s].tiempoRiego);
      json += "}";
    }

    json += "]";
    json += "}";
  }

  json += "]}";
  return json;
}

// Builds JSON response with status and optional extra data
String buildOkJson(bool ok, const String& extra = "") {
  String json = "{\"ok\":" + boolToJson(ok);
  if (extra.length() > 0) {
    json += "," + extra;
  }
  json += "}";
  return json;
}

// RTC Helper Functions
// Pads single digit with leading zero
String twoDigits(uint8_t value) {
  if (value < 10) {
    return "0" + String(value);
  }
  return String(value);
}

// Formats RTC date as YYYY/MM/DD
String formatRTCDate(const Time& t) {
  return String(t.yr) + "/" + twoDigits(t.mon) + "/" + twoDigits(t.date);
}

// Formats RTC time as HH:MM:SS
String formatRTCTime(const Time& t) {
  return twoDigits(t.hr) + ":" + twoDigits(t.min) + ":" + twoDigits(t.sec);
}

// Calculates day of week from date
Time::Day calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day) {
  static const int monthTable[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (month < 3) {
    y -= 1;
  }
  int dow = (y + y / 4 - y / 100 + y / 400 + monthTable[month - 1] + day) % 7;
  return static_cast<Time::Day>(dow + 1);
}

bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  if (month < 1 || month > 12) {
    return 0;
  }

  if (month == 2 && isLeapYear(year)) {
    return 29;
  }

  return daysPerMonth[month - 1];
}

bool isValidDateTime(uint16_t year,
                     uint8_t month,
                     uint8_t day,
                     uint8_t hour,
                     uint8_t minute,
                     uint8_t second) {
  if (year < 2000 || year > 2099) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > daysInMonth(year, month)) return false;
  if (hour > 23) return false;
  if (minute > 59) return false;
  if (second > 59) return false;
  return true;
}

// Converts the selected date into the program bitmask used by the UI (Mon=bit0 ... Sun=bit6)
uint8_t dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day) {
  const uint8_t dow = (uint8_t)calculateDayOfWeek(year, month, day);

  switch (dow) {
    case 2: return 0;  // Monday
    case 3: return 1;  // Tuesday
    case 4: return 2;  // Wednesday
    case 5: return 3;  // Thursday
    case 6: return 4;  // Friday
    case 7: return 5;  // Saturday
    case 1: return 6;  // Sunday
    default: return 255;
  }
}

bool shouldStartProgramNow(const Program& program, const Time& now) {
  if (!program.valid || program.sectorCount == 0) {
    return false;
  }

  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) {
    return false;
  }

  const uint8_t dayBit = dayMaskBitFromDate(now.yr, now.mon, now.date);
  if (dayBit > 6 || (program.dias & (1U << dayBit)) == 0) {
    return false;
  }

  uint8_t programHour = 0;
  uint8_t programMinute = 0;
  if (!parseHourMinute(program.horaInicio, programHour, programMinute)) {
    return false;
  }

  return programHour == now.hr && programMinute == now.min;
}

void rememberScheduleMinute(const Time& now) {
  lastScheduleYear = now.yr;
  lastScheduleMonth = now.mon;
  lastScheduleDay = now.date;
  lastScheduleHour = now.hr;
  lastScheduleMinute = now.min;
}

bool isSameScheduleMinute(const Time& now) {
  return now.yr == lastScheduleYear &&
         now.mon == lastScheduleMonth &&
         now.date == lastScheduleDay &&
         now.hr == lastScheduleHour &&
         now.min == lastScheduleMinute;
}

void checkScheduledPrograms() {
  const Time now = rtc.time();
  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) {
    return;
  }

  if (isSameScheduleMinute(now)) {
    return;
  }

  rememberScheduleMinute(now);

  if (systemState == STATE_RUNNING || isManualControlActive()) {
    return;
  }

  for (uint8_t i = 0; i < MAX_PROGRAMS; i++) {
    if (shouldStartProgramNow(programs[i], now)) {
      startProgramById(programs[i].id);
      return;
    }
  }
}

// Builds RTC state as JSON
String buildRTCJson() {
  Time now = rtc.time();
  String json = "{";
  json += "\"year\":" + String(now.yr) + ",";
  json += "\"month\":" + String(now.mon) + ",";
  json += "\"day\":" + String(now.date) + ",";
  json += "\"hour\":" + String(now.hr) + ",";
  json += "\"minute\":" + String(now.min) + ",";
  json += "\"second\":" + String(now.sec);
  json += "}";
  return json;
}

// Serves main HTML interface
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// Returns current system status as JSON
void handleEstado() {
  server.send(200, "application/json", buildEstadoJson());
}

// Returns list of all stored programs as JSON
void handleProgramas() {
  server.send(200, "application/json", buildProgramasJson());
}

// Handles manual control of sectors and pump
void handleControl() {
  if (!server.hasArg("type")) {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"Missing type\""));
    return;
  }

  String type = server.arg("type");
  int state = server.hasArg("state") ? server.arg("state").toInt() : 0;

  if (type == "sector") {
    if (!server.hasArg("id")) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Missing id\""));
      return;
    }
    int id = server.arg("id").toInt();
    if (id < 1 || id > NUM_SECTORS) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Invalid id\""));
      return;
    }

    if (state) {
      manualSectorMask |= sectorIdToMask((uint8_t)id);
      stopRuntime(STATE_IDLE);
    } else {
      manualSectorMask &= (uint16_t)~sectorIdToMask((uint8_t)id);
      applyOutputsFromState();
    }
    server.send(200, "application/json", buildOkJson(true));
  } else if (type == "pump") {
    server.send(409, "application/json",
                buildOkJson(false, "\"error\":\"La bomba sigue automaticamente a los sectores activos\""));
  } else {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"Invalid type\""));
  }
}

// Immediately stops current program execution
void handleParada() {
  clearManualOverrides();
  stopRuntime(STATE_MANUAL_STOP);
  server.send(200, "application/json", buildOkJson(true));
}

// Handles program configuration: save, delete, or execute
void handleConfiguracion() {
  String body = getRequestBody();
  if (body.length() == 0) {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"body vacio\""));
    return;
  }

  if (body.indexOf("\"ejecutar\"") >= 0) {
    int runId = 0;
    if (!extractIntField(body, "ejecutar", runId) || runId <= 0) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"ejecutar invalido\""));
      return;
    }

    bool ok = startProgramById((uint16_t)runId);
    server.send(ok ? 200 : 404, "application/json", buildOkJson(ok));
    return;
  }

  if (body.indexOf("\"borrar\"") >= 0) {
    int deleteId = 0;
    if (!extractIntField(body, "borrar", deleteId) || deleteId <= 0) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"borrar invalido\""));
      return;
    }

    int idx = findProgramIndexById((uint16_t)deleteId);
    if (idx < 0) {
      server.send(404, "application/json", buildOkJson(false, "\"error\":\"programa no encontrado\""));
      return;
    }

    if (activeProgramId == (uint16_t)deleteId) {
      stopRuntime(STATE_IDLE);
    }

    programs[idx].valid = false;
    server.send(200, "application/json", buildOkJson(true));
    return;
  }

  if (body.indexOf("\"programa\"") >= 0) {
    String programJson;
    if (!extractObjectField(body, "programa", programJson)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"programa invalido\""));
      return;
    }

    Program parsed;
    if (!parseProgramFromJson(programJson, parsed)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"no se pudo parsear programa\""));
      return;
    }

    int slot = -1;
    if (parsed.id > 0) {
      slot = findProgramIndexById(parsed.id);
    }

    if (slot < 0) {
      slot = findFreeProgramSlot();
      if (slot < 0) {
        server.send(507, "application/json", buildOkJson(false, "\"error\":\"sin espacio para mas programas\""));
        return;
      }
      parsed.id = nextProgramId++;
    }

    parsed.valid = true;
    programs[slot] = parsed;

    String extra = "\"id\":" + String(parsed.id);
    server.send(200, "application/json", buildOkJson(true, extra));
    return;
  }

  server.send(400, "application/json", buildOkJson(false, "\"error\":\"accion no reconocida\""));
}

// Returns empty response for favicon requests
void handleFavicon() {
  server.send(204, "text/plain", "");
}

// Serves main HTML for unknown routes
void handleNotFound() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// Prints system status to serial every 20 seconds
void printPeriodicStatus() {
  unsigned long now = millis();
  if (now - lastStatusPrint < STATUS_PRINT_INTERVAL_MS) {
    return;
  }

  lastStatusPrint = now;

  Serial.println();
  Serial.println("===== ESTADO CADA 20s =====");

  // Print RTC time
  Time rtcNow = rtc.time();
  Serial.print("Hora RTC: ");
  Serial.print(formatRTCDate(rtcNow));
  Serial.print(" ");
  Serial.println(formatRTCTime(rtcNow));

  Serial.print("Estado: ");
  Serial.println(isManualControlActive() ? "MANUAL" : stateToString(systemState));
  Serial.print("Programa activo: ");
  Serial.println(activeProgramId);
  Serial.print("Sectores activos: ");
  Serial.println(formatSectorMaskForSerial(getOutputSectorMask()));
  Serial.print("Tiempo restante: ");
  Serial.print(remainingTimeSec);
  Serial.println(" s");
  Serial.print("Bomba (GPIO");
  Serial.print(PUMP_PIN);
  Serial.print("): ");
  Serial.println(pumpOn ? "ON" : "OFF");
  Serial.print("Modo manual: ");
  Serial.println(isManualControlActive() ? "SI" : "NO");

  for (uint8_t i = 1; i <= NUM_SECTORS; i++) {
    Serial.print("- Sector ");
    Serial.print(i);
    Serial.print(" (GPIO");
    Serial.print(SECTOR_PINS[i - 1]);
    Serial.print("): ");
    Serial.println(isSectorActive(i) ? "ACTIVO" : "inactivo");
  }

  Serial.println("===========================");
}

// Gets or sets RTC date and time
void handleRTC() {
  if (server.method() == HTTP_GET) {
    // GET request for current RTC time
    server.send(200, "application/json", buildRTCJson());
    return;
  }

  if (server.method() == HTTP_POST) {
    // POST request to set RTC time
    if (!server.hasArg("year") || !server.hasArg("month") || !server.hasArg("day") ||
        !server.hasArg("hour") || !server.hasArg("minute") || !server.hasArg("second")) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Missing parameters\""));
      return;
    }

    int year = server.arg("year").toInt();
    int month = server.arg("month").toInt();
    int day = server.arg("day").toInt();
    int hour = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();
    int second = server.arg("second").toInt();

    if (!isValidDateTime((uint16_t)year,
                         (uint8_t)month,
                         (uint8_t)day,
                         (uint8_t)hour,
                         (uint8_t)minute,
                         (uint8_t)second)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"Invalid date/time\""));
      return;
    }

    Time::Day dow = calculateDayOfWeek((uint16_t)year, (uint8_t)month, (uint8_t)day);
    Time newTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                 (uint8_t)hour, (uint8_t)minute, (uint8_t)second, dow);

    rtc.writeProtect(false);
    delay(10);
    rtc.halt(false);
    delay(10);
    rtc.time(newTime);
    delay(50);
    rtc.halt(false);
    rtc.writeProtect(true);

    server.send(200, "application/json", buildOkJson(true, "\"rtc\":" + buildRTCJson()));
  }
}

// Initializes default irrigation programs
void seedDefaultPrograms() {
  memset(programs, 0, sizeof(programs));

  programs[0].valid = true;
  programs[0].id = 1;
  strncpy(programs[0].horaInicio, "07:00", sizeof(programs[0].horaInicio));
  programs[0].dias = 0b0111110;
  programs[0].retardoEntreSectores = 5;
  programs[0].ciclico = true;
  programs[0].sectorCount = 4;
  programs[0].sectores[0] = {1, 1, 60};
  programs[0].sectores[1] = {2, 2, 90};
  programs[0].sectores[2] = {3, 3, 120};
  programs[0].sectores[3] = {5, 4, 45};

  programs[1].valid = true;
  programs[1].id = 2;
  strncpy(programs[1].horaInicio, "19:30", sizeof(programs[1].horaInicio));
  programs[1].dias = 0b1100000;
  programs[1].retardoEntreSectores = 10;
  programs[1].ciclico = false;
  programs[1].sectorCount = 3;
  programs[1].sectores[0] = {4, 1, 180};
  programs[1].sectores[1] = {6, 2, 180};
  programs[1].sectores[2] = {8, 3, 90};
}

// Initializes ESP32, RTC, WiFi and HTTP server
void setup() {
  Serial.begin(115200);
  delay(300);

  // Configure all sector GPIO pins as outputs
  for (uint8_t i = 0; i < NUM_SECTORS; i++) {
    pinMode(SECTOR_PINS[i], OUTPUT);
    digitalWrite(SECTOR_PINS[i], LOW);

    if (SECTOR_PINS[i] >= 34 && SECTOR_PINS[i] <= 39) {
      Serial.print("WARNING: GPIO");
      Serial.print(SECTOR_PINS[i]);
      Serial.println(" is input-only on ESP32 and cannot drive a relay output.");
    }
  }

  // Configure pump GPIO pin
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  // Initialize RTC
  Serial.println("\n\nStarting RTC initialization...");
  rtc.writeProtect(false);
  rtc.halt(false);
  delay(50);

  Time now = rtc.time();
  Serial.print("RTC read: ");
  Serial.print(formatRTCDate(now));
  Serial.print(" ");
  Serial.println(formatRTCTime(now));

  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) {
    Serial.println("RTC contains invalid data, attempting to initialize...");

    bool initOk = false;
    const Time defaultTime(2024, 1, 1, 12, 0, 0, calculateDayOfWeek(2024, 1, 1));

    for (int attempt = 0; attempt < 5; attempt++) {
      Serial.print("RTC init attempt ");
      Serial.println(attempt + 1);

      rtc.halt(false);
      delay(20);
      rtc.time(defaultTime);
      delay(100);

      Time verify = rtc.time();
      Serial.print("  Read back: ");
      Serial.print(formatRTCDate(verify));
      Serial.print(" ");
      Serial.println(formatRTCTime(verify));

      if (isValidDateTime(verify.yr, verify.mon, verify.date, verify.hr, verify.min, verify.sec)) {
        Serial.println("RTC initialization successful!");
        initOk = true;
        break;
      }
    }

    if (!initOk) {
      Serial.println("WARNING: RTC still reports invalid data after initialization attempts.");
    }
  } else {
    Serial.println("RTC has valid data, no initialization needed");
  }

  rtc.writeProtect(true);

  seedDefaultPrograms();
  stopRuntime(STATE_IDLE);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("==================================");
  Serial.println("ESP32 SISTEMA DE RIEGO INICIADO");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Abrir en navegador: http://192.168.4.1");
  Serial.println("RTC pins - CLK: GPIO18, DAT: GPIO19, RST: GPIO21");
  Serial.print("Pump pin: GPIO");
  Serial.println(PUMP_PIN);
  Serial.println("Sector pins (1-8): GPIO 13, 14, 16, 17, 32, 33, 25, 26");
  Serial.println("==================================");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/estado", HTTP_GET, handleEstado);
  server.on("/programas", HTTP_GET, handleProgramas);
  server.on("/configuracion", HTTP_POST, handleConfiguracion);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/rtc", HTTP_GET, handleRTC);
  server.on("/rtc", HTTP_POST, handleRTC);
  server.on("/parada", HTTP_POST, handleParada);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);
  server.onNotFound(handleNotFound);
  server.begin();

  lastStatusPrint = millis();
}

// Main program loop: handles HTTP requests and updates runtime state
void loop() {
  server.handleClient();
  checkScheduledPrograms();
  updateRuntime();
  printPeriodicStatus();
}
