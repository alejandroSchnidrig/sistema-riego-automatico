#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DS1302.h>
#include "config/Config.h"
#include "pages/index_html.h"

/*
  ESP32 - Sistema de Riego Automático

  Pines (ver Config.h para valores exactos):
  - RTC DS1302 : CLK=GPIO18, DAT=GPIO19, RST=GPIO21
  - Sectores 1-8: GPIO 13, 14, 16, 17, 32, 33, 25, 26
  - Bomba      : GPIO27

  Endpoints HTTP:
      GET  /estado
      GET  /programas
      POST /configuracion
      GET  /control?type=sector&id=N&state=0|1
      POST /parada
      GET  /rtc
      POST /rtc?year=...&month=...&day=...&hour=...&minute=...&second=...

  Compilar con PlatformIO: pio run --target upload
*/

WebServer server(80);

// Módulo RTC DS1302 — constructor recibe (RST, DAT, CLK) según la librería arduino-ds1302
DS1302 rtc(Config::RTC_RST, Config::RTC_DAT, Config::RTC_CLK);

// ============================================================
// Enumeraciones y estructuras de dominio
// ============================================================

// Estados posibles del sistema
enum SystemState {
  STATE_IDLE,          // sin programa en ejecución
  STATE_RUNNING,       // programa en curso
  STATE_MANUAL_STOP    // detenido manualmente por el usuario
};

// Un paso de ejecución dentro de un programa (modelo lineal — se reemplaza en Fase D)
struct SectorStep {
  uint8_t  id;           // ID del sector (1-8)
  uint8_t  orden;        // posición en la secuencia del programa
  uint32_t tiempoRiego;  // duración de riego en segundos
};

// Programa de riego: lista ordenada de sectores con configuración de horario
struct Program {
  bool       valid;                              // slot ocupado en el arreglo
  uint16_t   id;
  char       horaInicio[6];                      // "HH:MM\0"
  uint8_t    dias;                               // bitmask: lun=bit0 … dom=bit6
  uint16_t   retardoEntreSectores;               // segundos de pausa entre sectores
  bool       ciclico;                            // ¿vuelve a empezar al terminar?
  uint8_t    sectorCount;
  SectorStep sectores[Config::NUM_SECTORES];     // lista de pasos ordenados
};

// ============================================================
// Variables de estado global
// ============================================================

Program  programs[Config::MAX_PROGRAMAS];
uint16_t nextProgramId = 3;  // IDs 1 y 2 son los programas semilla

SystemState systemState      = STATE_IDLE;
uint16_t    activeProgramId  = 0;
uint8_t     activeSectorId   = 0;
uint32_t    remainingTimeSec = 0;
bool        pumpOn           = false;

// Máscara de control manual: bit0=sector1 … bit7=sector8
uint16_t manualSectorMask = 0;

// Variables de ejecución del paso en curso
int           runningProgramIndex   = -1;
int           runningStepIndex      = -1;
bool          waitingBetweenSectors = false;
unsigned long stepStartMs           = 0;
unsigned long delayStartMs          = 0;
unsigned long lastStatusPrint       = 0;

// Último minuto procesado por el scheduler (evita disparar el mismo programa dos veces)
uint16_t lastScheduleYear   = 0;
uint8_t  lastScheduleMonth  = 0;
uint8_t  lastScheduleDay    = 0;
uint8_t  lastScheduleHour   = 255;
uint8_t  lastScheduleMinute = 255;

// ============================================================
// Funciones de hardware
// ============================================================

// Enciende o apaga la bomba de agua
void setPump(bool on) {
  pumpOn = on;
  digitalWrite(Config::PIN_BOMBA, on ? HIGH : LOW);
}

// ============================================================
// Operaciones sobre máscaras de sectores
// ============================================================

// Convierte un ID de sector (1-8) en su bit correspondiente en la máscara
uint16_t sectorIdToMask(uint8_t sectorId) {
  if (sectorId < 1 || sectorId > Config::NUM_SECTORES) return 0;
  return (uint16_t)1U << (sectorId - 1);
}

// Devuelve el ID del primer sector activo en la máscara (0 si ninguno)
uint8_t firstSectorFromMask(uint16_t sectorMask) {
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    if ((sectorMask & sectorIdToMask(i)) != 0) return i;
  }
  return 0;
}

// Combina los sectores del programa en curso y los sectores en modo manual
uint16_t getOutputSectorMask() {
  return manualSectorMask | sectorIdToMask(activeSectorId);
}

// Devuelve true si el sector indicado está activo (programa o manual)
bool isSectorActive(uint8_t sectorId) {
  return (getOutputSectorMask() & sectorIdToMask(sectorId)) != 0;
}

// Escribe la máscara de sectores directamente en los pines GPIO
void setSectorHardware(uint16_t sectorMask) {
  for (uint8_t i = 0; i < Config::NUM_SECTORES; i++) {
    digitalWrite(Config::PINES_SECTORES[i],
                 (sectorMask & sectorIdToMask(i + 1)) != 0 ? HIGH : LOW);
  }
}

// Borra todos los sobreescrituras manuales para ceder el control al programa automático
void clearManualOverrides() {
  manualSectorMask = 0;
}

// Aplica el estado combinado (programa + manual) a los pines de hardware y la bomba
void applyOutputsFromState() {
  const uint16_t sectorMask = getOutputSectorMask();
  setSectorHardware(sectorMask);
  setPump(sectorMask != 0);  // la bomba sigue automáticamente a los sectores
}

// Devuelve true si hay sectores activos en modo manual
bool isManualControlActive() {
  return manualSectorMask != 0;
}

// Primer sector activo combinando programa y manual
uint8_t getEffectiveSectorId() {
  return firstSectorFromMask(getOutputSectorMask());
}

// Primer sector activo solo por control manual
uint8_t getFirstManualSectorId() {
  return firstSectorFromMask(manualSectorMask);
}

// ============================================================
// Serialización JSON — helpers
// ============================================================

// Construye un arreglo JSON con los IDs de sectores activos en la máscara
String buildSectorArrayJson(uint16_t sectorMask) {
  String json = "[";
  bool first = true;
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    if ((sectorMask & sectorIdToMask(i)) == 0) continue;
    if (!first) json += ",";
    first = false;
    json += String(i);
  }
  json += "]";
  return json;
}

// Formatea la máscara de sectores como texto legible para el monitor serial
String formatSectorMaskForSerial(uint16_t sectorMask) {
  if (sectorMask == 0) return "ninguno";
  String text;
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    if ((sectorMask & sectorIdToMask(i)) == 0) continue;
    if (text.length() > 0) text += ", ";
    text += "S";
    text += String(i);
  }
  return text;
}

// Convierte el enum SystemState a string para JSON y serial
const char* stateToString(SystemState state) {
  switch (state) {
    case STATE_RUNNING:     return "RUNNING";
    case STATE_MANUAL_STOP: return "MANUAL_STOP";
    case STATE_IDLE:
    default:                return "IDLE";
  }
}

// Serializa un booleano como literal JSON
String boolToJson(bool value) {
  return value ? "true" : "false";
}

// Escapa caracteres especiales para incrustar en un string JSON
String escapeJson(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if      (c == '"')  out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else                out += c;
  }
  return out;
}

// ============================================================
// Parser JSON hand-rolled
// (decisión de diseño: no se usa ArduinoJson en este sprint)
// ============================================================

// Devuelve el cuerpo crudo del request HTTP actual
String getRequestBody() {
  if (server.hasArg("plain")) return server.arg("plain");
  return "";
}

// Parsea un entero a partir de startPos en src; actualiza endPos al carácter siguiente
bool extractIntAt(const String& src, int startPos, int& value, int& endPos) {
  int i = startPos;
  // Saltear espacios en blanco
  while (i < (int)src.length() &&
         (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) {
    i++;
  }

  bool negative = false;
  if (i < (int)src.length() && src[i] == '-') {
    negative = true;
    i++;
  }

  if (i >= (int)src.length() || !isDigit(src[i])) return false;

  long result = 0;
  while (i < (int)src.length() && isDigit(src[i])) {
    result = result * 10 + (src[i] - '0');
    i++;
  }

  value  = negative ? (int)-result : (int)result;
  endPos = i;
  return true;
}

// Extrae el valor entero de un campo JSON por clave
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

// Extrae el valor booleano de un campo JSON por clave
bool extractBoolField(const String& json, const String& key, bool& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  // Saltear espacios
  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("true",  pos)) { out = true;  return true; }
  if (json.startsWith("false", pos)) { out = false; return true; }
  return false;
}

// Extrae el valor string de un campo JSON por clave
bool extractStringField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (pos >= (int)json.length() || json[pos] != '"') return false;

  pos++;
  int end = json.indexOf('"', pos);
  if (end < 0) return false;

  out = json.substring(pos, end);
  return true;
}

// Extrae el campo "id" que puede ser null o estar ausente (retorna 0 en ese caso)
bool extractNullableId(const String& json, int& out) {
  String pattern = "\"id\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) { out = 0; return true; }

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("null", pos)) { out = 0; return true; }

  int endPos = pos;
  return extractIntAt(json, pos, out, endPos);
}

// Extrae un objeto JSON anidado por clave (busca el {} balanceado)
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
    if      (json[i] == '{') depth++;
    else if (json[i] == '}') {
      depth--;
      if (depth == 0) { out = json.substring(start, i + 1); return true; }
    }
  }
  return false;
}

// Extrae un arreglo JSON por clave (busca el [] balanceado)
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
    if      (json[i] == '[') depth++;
    else if (json[i] == ']') {
      depth--;
      if (depth == 0) { out = json.substring(start, i + 1); return true; }
    }
  }
  return false;
}

// ============================================================
// Operaciones sobre programas
// ============================================================

// Ordena los sectores de un programa por su campo `orden` (burbuja simple)
void sortProgramSectors(Program& program) {
  for (uint8_t i = 0; i < program.sectorCount; i++) {
    for (uint8_t j = i + 1; j < program.sectorCount; j++) {
      if (program.sectores[j].orden < program.sectores[i].orden) {
        SectorStep tmp      = program.sectores[i];
        program.sectores[i] = program.sectores[j];
        program.sectores[j] = tmp;
      }
    }
  }
}

// Valida y parsea una hora en formato "HH:MM"
bool parseHourMinute(const char* value, uint8_t& hour, uint8_t& minute) {
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

// Parsea un programa completo desde su representación JSON
bool parseProgramFromJson(const String& programJson, Program& outProgram) {
  memset(&outProgram, 0, sizeof(outProgram));
  outProgram.valid = true;

  // ID: null o ausente significa "crear nuevo"
  int id = 0;
  extractNullableId(programJson, id);
  outProgram.id = (uint16_t)id;

  // Hora de inicio obligatoria en formato "HH:MM"
  String hora;
  uint8_t startHour = 0, startMinute = 0;
  if (!extractStringField(programJson, "horaInicio", hora) ||
      !parseHourMinute(hora.c_str(), startHour, startMinute)) {
    return false;
  }
  hora.toCharArray(outProgram.horaInicio, sizeof(outProgram.horaInicio));

  // Días de ejecución como bitmask (0–127)
  int dias = 0;
  if (!extractIntField(programJson, "dias", dias) || dias < 0 || dias > 0x7F) return false;
  outProgram.dias = (uint8_t)dias;

  // Retardo en segundos entre un sector y el siguiente
  int retardo = 0;
  if (!extractIntField(programJson, "retardoEntreSectores", retardo) ||
      retardo < 0 || retardo > 65535) {
    return false;
  }
  outProgram.retardoEntreSectores = (uint16_t)retardo;

  // ¿El programa se repite al terminar el último sector?
  bool ciclico = false;
  if (!extractBoolField(programJson, "ciclico", ciclico)) return false;
  outProgram.ciclico = ciclico;

  // Lista de sectores del programa
  String sectoresArray;
  if (!extractArrayField(programJson, "sectores", sectoresArray)) return false;

  int pos = 0;
  while (pos < (int)sectoresArray.length() && outProgram.sectorCount < Config::NUM_SECTORES) {
    int objStart = sectoresArray.indexOf('{', pos);
    if (objStart < 0) break;

    // Encontrar el cierre del objeto balanceando llaves
    int depth = 0, objEnd = -1;
    for (int i = objStart; i < (int)sectoresArray.length(); i++) {
      if      (sectoresArray[i] == '{') depth++;
      else if (sectoresArray[i] == '}') {
        depth--;
        if (depth == 0) { objEnd = i; break; }
      }
    }
    if (objEnd < 0) break;

    String item = sectoresArray.substring(objStart, objEnd + 1);
    int sectorId = 0, orden = 0, tiempo = 0;

    if (extractIntField(item, "id",          sectorId) &&
        extractIntField(item, "orden",        orden)    &&
        extractIntField(item, "tiempoRiego",  tiempo)   &&
        sectorId >= 1 && sectorId <= (int)Config::NUM_SECTORES &&
        orden    >= 1 && orden    <= (int)Config::NUM_SECTORES &&
        tiempo   > 0) {
      SectorStep& s  = outProgram.sectores[outProgram.sectorCount++];
      s.id           = (uint8_t)sectorId;
      s.orden        = (uint8_t)orden;
      s.tiempoRiego  = (uint32_t)tiempo;
    }

    pos = objEnd + 1;
  }

  if (outProgram.sectorCount == 0) return false;

  sortProgramSectors(outProgram);
  return true;
}

// Busca un programa por ID y retorna su índice (-1 si no existe)
int findProgramIndexById(uint16_t id) {
  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    if (programs[i].valid && programs[i].id == id) return i;
  }
  return -1;
}

// Retorna el primer slot libre del arreglo de programas (-1 si está lleno)
int findFreeProgramSlot() {
  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    if (!programs[i].valid) return i;
  }
  return -1;
}

// ============================================================
// Motor de ejecución de programas
// ============================================================

// Detiene la ejecución en curso y limpia el estado de runtime
void stopRuntime(SystemState newState) {
  systemState           = newState;
  activeProgramId       = 0;
  activeSectorId        = 0;
  remainingTimeSec      = 0;
  runningProgramIndex   = -1;
  runningStepIndex      = -1;
  waitingBetweenSectors = false;
  stepStartMs           = 0;
  delayStartMs          = 0;
  applyOutputsFromState();
}

// Inicia un paso específico del programa (activa el sector correspondiente)
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

  runningProgramIndex   = programIndex;
  runningStepIndex      = stepIndex;
  waitingBetweenSectors = false;
  systemState           = STATE_RUNNING;
  activeProgramId       = p.id;
  activeSectorId        = p.sectores[stepIndex].id;
  remainingTimeSec      = p.sectores[stepIndex].tiempoRiego;
  stepStartMs           = millis();

  applyOutputsFromState();
}

// Inicia la ejecución de un programa por su ID (retorna false si no existe)
bool startProgramById(uint16_t id) {
  int index = findProgramIndexById(id);
  if (index < 0) return false;

  Program& p = programs[index];
  if (p.sectorCount == 0) return false;

  clearManualOverrides();
  startStep(index, 0);
  return true;
}

// Actualiza el estado del programa en ejecución — debe llamarse en cada iteración del loop
void updateRuntime() {
  if (systemState != STATE_RUNNING || runningProgramIndex < 0) return;

  Program& p        = programs[runningProgramIndex];
  unsigned long now = millis();

  // Fase de espera entre sectores (retardo configurado)
  if (waitingBetweenSectors) {
    remainingTimeSec = 0;
    if (now - delayStartMs >= (unsigned long)p.retardoEntreSectores * 1000UL) {
      int nextStep = runningStepIndex + 1;
      if (nextStep >= p.sectorCount) {
        p.ciclico ? startStep(runningProgramIndex, 0) : stopRuntime(STATE_IDLE);
      } else {
        startStep(runningProgramIndex, nextStep);
      }
    }
    return;
  }

  // Fase de riego activo: verificar si se agotó el tiempo del sector actual
  uint32_t      stepDurationSec = p.sectores[runningStepIndex].tiempoRiego;
  unsigned long elapsedMs       = now - stepStartMs;
  uint32_t      elapsedSec      = (uint32_t)(elapsedMs / 1000UL);

  if (elapsedSec >= stepDurationSec) {
    // Sector terminado: apagar y decidir qué sigue
    activeSectorId = 0;
    applyOutputsFromState();

    int nextStep = runningStepIndex + 1;

    if (nextStep >= p.sectorCount) {
      // Era el último sector del programa
      if (p.ciclico) {
        if (p.retardoEntreSectores > 0) {
          waitingBetweenSectors = true;
          delayStartMs          = now;
        } else {
          startStep(runningProgramIndex, 0);
        }
      } else {
        stopRuntime(STATE_IDLE);
      }
    } else {
      // Quedan más sectores
      if (p.retardoEntreSectores > 0) {
        waitingBetweenSectors = true;
        delayStartMs          = now;
        remainingTimeSec      = 0;
      } else {
        startStep(runningProgramIndex, nextStep);
      }
    }
    return;
  }

  // Actualizar tiempo restante del sector en curso
  remainingTimeSec = stepDurationSec - elapsedSec;
}

// ============================================================
// Constructores de respuestas JSON
// ============================================================

// Construye el JSON de estado para GET /estado
String buildEstadoJson() {
  const uint16_t activeSectorMask = getOutputSectorMask();
  String json = "{";
  json += "\"estado\":\""          + String(stateToString(systemState))    + "\",";
  json += "\"programaActivo\":"    + String(activeProgramId)               + ",";
  json += "\"sectorActivo\":"      + String(getEffectiveSectorId())         + ",";
  json += "\"sectoresActivos\":"   + buildSectorArrayJson(activeSectorMask) + ",";
  json += "\"tiempoRestante\":"    + String(remainingTimeSec)              + ",";
  json += "\"bomba\":"             + boolToJson(pumpOn)                    + ",";
  json += "\"modoManual\":"        + boolToJson(isManualControlActive())   + ",";
  json += "\"manualSectorMask\":"  + String(manualSectorMask)              + ",";
  json += "\"manualSectorId\":"    + String(getFirstManualSectorId())      + ",";
  json += "\"manualPumpOn\":false";
  json += "}";
  return json;
}

// Construye la lista completa de programas para GET /programas
String buildProgramasJson() {
  String json = "{\"programas\":[";
  bool firstProgram = true;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    if (!programs[i].valid) continue;
    if (!firstProgram) json += ",";
    firstProgram = false;

    Program& p = programs[i];
    json += "{";
    json += "\"id\":"                   + String(p.id)                              + ",";
    json += "\"horaInicio\":\""         + escapeJson(String(p.horaInicio))          + "\",";
    json += "\"dias\":"                 + String(p.dias)                            + ",";
    json += "\"retardoEntreSectores\":" + String(p.retardoEntreSectores)            + ",";
    json += "\"ciclico\":"              + boolToJson(p.ciclico)                     + ",";
    json += "\"sectores\":[";

    for (uint8_t s = 0; s < p.sectorCount; s++) {
      if (s > 0) json += ",";
      json += "{";
      json += "\"id\":"          + String(p.sectores[s].id)         + ",";
      json += "\"orden\":"       + String(p.sectores[s].orden)      + ",";
      json += "\"tiempoRiego\":" + String(p.sectores[s].tiempoRiego);
      json += "}";
    }

    json += "]}";
  }

  json += "]}";
  return json;
}

// Construye una respuesta JSON de éxito/error con datos opcionales
String buildOkJson(bool ok, const String& extra = "") {
  String json = "{\"ok\":" + boolToJson(ok);
  if (extra.length() > 0) json += "," + extra;
  json += "}";
  return json;
}

// ============================================================
// Funciones auxiliares del RTC DS1302
// ============================================================

// Agrega cero a la izquierda si el número tiene un solo dígito
String twoDigits(uint8_t value) {
  return value < 10 ? "0" + String(value) : String(value);
}

// Formatea la fecha del RTC como "AAAA/MM/DD"
String formatRTCDate(const Time& t) {
  return String(t.yr) + "/" + twoDigits(t.mon) + "/" + twoDigits(t.date);
}

// Formatea la hora del RTC como "HH:MM:SS"
String formatRTCTime(const Time& t) {
  return twoDigits(t.hr) + ":" + twoDigits(t.min) + ":" + twoDigits(t.sec);
}

// Calcula el día de la semana a partir de una fecha (algoritmo de Tomohiko Sakamoto)
Time::Day calculateDayOfWeek(uint16_t year, uint8_t month, uint8_t day) {
  static const int monthTable[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year;
  if (month < 3) y -= 1;
  int dow = (y + y / 4 - y / 100 + y / 400 + monthTable[month - 1] + day) % 7;
  return static_cast<Time::Day>(dow + 1);
}

bool isLeapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && isLeapYear(year)) return 29;
  return daysPerMonth[month - 1];
}

// Valida que los campos de fecha/hora estén en rangos correctos
bool isValidDateTime(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t minute, uint8_t second) {
  if (year   < 2000 || year > 2099)              return false;
  if (month  < 1    || month > 12)               return false;
  if (day    < 1    || day > daysInMonth(year, month)) return false;
  if (hour   > 23)                               return false;
  if (minute > 59)                               return false;
  if (second > 59)                               return false;
  return true;
}

// Convierte una fecha al índice de bit del bitmask de días (lun=0 … dom=6)
uint8_t dayMaskBitFromDate(uint16_t year, uint8_t month, uint8_t day) {
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

// ============================================================
// Scheduler — verifica si debe iniciarse algún programa
// ============================================================

// Devuelve true si el programa debe iniciarse en el minuto actual del RTC
bool shouldStartProgramNow(const Program& program, const Time& now) {
  if (!program.valid || program.sectorCount == 0) return false;
  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) return false;

  // Verificar que el día de la semana esté habilitado en la máscara del programa
  const uint8_t dayBit = dayMaskBitFromDate(now.yr, now.mon, now.date);
  if (dayBit > 6 || (program.dias & (1U << dayBit)) == 0) return false;

  uint8_t programHour = 0, programMinute = 0;
  if (!parseHourMinute(program.horaInicio, programHour, programMinute)) return false;

  return programHour == now.hr && programMinute == now.min;
}

// Almacena el minuto actual para no disparar el mismo programa dos veces
void rememberScheduleMinute(const Time& now) {
  lastScheduleYear   = now.yr;
  lastScheduleMonth  = now.mon;
  lastScheduleDay    = now.date;
  lastScheduleHour   = now.hr;
  lastScheduleMinute = now.min;
}

// Devuelve true si este minuto ya fue procesado por el scheduler
bool isSameScheduleMinute(const Time& now) {
  return now.yr   == lastScheduleYear   &&
         now.mon  == lastScheduleMonth  &&
         now.date == lastScheduleDay    &&
         now.hr   == lastScheduleHour   &&
         now.min  == lastScheduleMinute;
}

// Consulta el RTC y lanza el primer programa cuya hora/día coincida con el momento actual
void checkScheduledPrograms() {
  const Time now = rtc.time();
  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) return;
  if (isSameScheduleMinute(now)) return;

  rememberScheduleMinute(now);

  // No iniciar si ya hay un programa corriendo o hay control manual activo
  if (systemState == STATE_RUNNING || isManualControlActive()) return;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    if (shouldStartProgramNow(programs[i], now)) {
      startProgramById(programs[i].id);
      return;
    }
  }
}

// ============================================================
// Handlers HTTP
// ============================================================

// Construye el JSON del RTC para GET /rtc
String buildRTCJson() {
  Time now = rtc.time();
  String json = "{";
  json += "\"year\":"   + String(now.yr)   + ",";
  json += "\"month\":"  + String(now.mon)  + ",";
  json += "\"day\":"    + String(now.date) + ",";
  json += "\"hour\":"   + String(now.hr)   + ",";
  json += "\"minute\":" + String(now.min)  + ",";
  json += "\"second\":" + String(now.sec);
  json += "}";
  return json;
}

// Sirve la interfaz web principal (SPA)
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// GET /estado — retorna el estado actual del sistema
void handleEstado() {
  server.send(200, "application/json", buildEstadoJson());
}

// GET /programas — retorna todos los programas almacenados
void handleProgramas() {
  server.send(200, "application/json", buildProgramasJson());
}

// GET /control?type=sector&id=N&state=0|1 — toggle manual de un sector
void handleControl() {
  if (!server.hasArg("type")) {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"falta type\""));
    return;
  }

  String type  = server.arg("type");
  int    state = server.hasArg("state") ? server.arg("state").toInt() : 0;

  if (type == "sector") {
    if (!server.hasArg("id")) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"falta id\""));
      return;
    }
    int id = server.arg("id").toInt();
    if (id < 1 || id > (int)Config::NUM_SECTORES) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"id invalido\""));
      return;
    }

    if (state) {
      // Activar modo manual en este sector: detiene cualquier programa automático
      manualSectorMask |= sectorIdToMask((uint8_t)id);
      stopRuntime(STATE_IDLE);
    } else {
      // Desactivar modo manual en este sector
      manualSectorMask &= (uint16_t)~sectorIdToMask((uint8_t)id);
      applyOutputsFromState();
    }
    server.send(200, "application/json", buildOkJson(true));

  } else if (type == "pump") {
    // La bomba no tiene control independiente: sigue automáticamente a los sectores
    server.send(409, "application/json",
                buildOkJson(false, "\"error\":\"La bomba sigue automaticamente a los sectores activos\""));
  } else {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"type invalido\""));
  }
}

// POST /parada — detiene toda ejecución y limpia el modo manual
void handleParada() {
  clearManualOverrides();
  stopRuntime(STATE_MANUAL_STOP);
  server.send(200, "application/json", buildOkJson(true));
}

// POST /configuracion — crea/actualiza/borra/ejecuta programas
void handleConfiguracion() {
  String body = getRequestBody();
  if (body.length() == 0) {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"body vacio\""));
    return;
  }

  // Acción: ejecutar un programa por ID
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

  // Acción: borrar un programa por ID
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
    if (activeProgramId == (uint16_t)deleteId) stopRuntime(STATE_IDLE);
    programs[idx].valid = false;
    server.send(200, "application/json", buildOkJson(true));
    return;
  }

  // Acción: guardar (crear o actualizar) un programa
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

    // Slot existente (edición) o slot libre (creación)
    int slot = (parsed.id > 0) ? findProgramIndexById(parsed.id) : -1;
    if (slot < 0) {
      slot = findFreeProgramSlot();
      if (slot < 0) {
        server.send(507, "application/json",
                    buildOkJson(false, "\"error\":\"sin espacio para mas programas\""));
        return;
      }
      parsed.id = nextProgramId++;
    }

    parsed.valid  = true;
    programs[slot] = parsed;
    server.send(200, "application/json", buildOkJson(true, "\"id\":" + String(parsed.id)));
    return;
  }

  server.send(400, "application/json", buildOkJson(false, "\"error\":\"accion no reconocida\""));
}

// Responde 204 sin contenido para solicitudes de favicon
void handleFavicon() {
  server.send(204, "text/plain", "");
}

// Ruta no encontrada: sirve la SPA para que el router del cliente maneje la navegación
void handleNotFound() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// Imprime el estado del sistema por Serial periódicamente
void printPeriodicStatus() {
  unsigned long now = millis();
  if (now - lastStatusPrint < Config::INTERVALO_ESTADO_SERIAL_MS) return;
  lastStatusPrint = now;

  Serial.println();
  Serial.println("===== ESTADO DEL SISTEMA =====");

  Time rtcNow = rtc.time();
  Serial.print("Hora RTC       : ");
  Serial.print(formatRTCDate(rtcNow));
  Serial.print(" ");
  Serial.println(formatRTCTime(rtcNow));

  Serial.print("Estado         : ");
  Serial.println(isManualControlActive() ? "MANUAL" : stateToString(systemState));
  Serial.print("Programa activo: ");
  Serial.println(activeProgramId);
  Serial.print("Sectores activos: ");
  Serial.println(formatSectorMaskForSerial(getOutputSectorMask()));
  Serial.print("Tiempo restante: ");
  Serial.print(remainingTimeSec);
  Serial.println(" s");
  Serial.print("Bomba (GPIO");
  Serial.print(Config::PIN_BOMBA);
  Serial.print("): ");
  Serial.println(pumpOn ? "ON" : "OFF");
  Serial.print("Modo manual    : ");
  Serial.println(isManualControlActive() ? "SI" : "NO");

  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    Serial.print("- Sector ");
    Serial.print(i);
    Serial.print(" (GPIO");
    Serial.print(Config::PINES_SECTORES[i - 1]);
    Serial.print("): ");
    Serial.println(isSectorActive(i) ? "ACTIVO" : "inactivo");
  }

  Serial.println("==============================");
}

// GET /rtc  — retorna la hora actual del RTC
// POST /rtc — establece la hora del RTC mediante parámetros de query string
void handleRTC() {
  if (server.method() == HTTP_GET) {
    server.send(200, "application/json", buildRTCJson());
    return;
  }

  if (server.method() == HTTP_POST) {
    if (!server.hasArg("year")   || !server.hasArg("month")  || !server.hasArg("day") ||
        !server.hasArg("hour")   || !server.hasArg("minute") || !server.hasArg("second")) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"faltan parametros\""));
      return;
    }

    int year   = server.arg("year").toInt();
    int month  = server.arg("month").toInt();
    int day    = server.arg("day").toInt();
    int hour   = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();
    int second = server.arg("second").toInt();

    if (!isValidDateTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                         (uint8_t)hour,  (uint8_t)minute, (uint8_t)second)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"fecha/hora invalida\""));
      return;
    }

    Time::Day dow     = calculateDayOfWeek((uint16_t)year, (uint8_t)month, (uint8_t)day);
    Time      newTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                      (uint8_t)hour,  (uint8_t)minute, (uint8_t)second, dow);

    // Secuencia de escritura del DS1302: desproteger → desactivar halt → escribir
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

// ============================================================
// Inicialización
// ============================================================

// Carga dos programas de demostración en memoria (en Fase F se reemplaza por LittleFS)
void seedDefaultPrograms() {
  memset(programs, 0, sizeof(programs));

  // Programa 1: lunes a viernes a las 07:00, cíclico, 4 sectores
  programs[0].valid                = true;
  programs[0].id                   = 1;
  strncpy(programs[0].horaInicio, "07:00", sizeof(programs[0].horaInicio));
  programs[0].dias                 = 0b0111110;  // lun=bit0 … vie=bit4
  programs[0].retardoEntreSectores = 5;
  programs[0].ciclico              = true;
  programs[0].sectorCount          = 4;
  programs[0].sectores[0]          = {1, 1,  60};
  programs[0].sectores[1]          = {2, 2,  90};
  programs[0].sectores[2]          = {3, 3, 120};
  programs[0].sectores[3]          = {5, 4,  45};

  // Programa 2: sábado y domingo a las 19:30, no cíclico, 3 sectores
  programs[1].valid                = true;
  programs[1].id                   = 2;
  strncpy(programs[1].horaInicio, "19:30", sizeof(programs[1].horaInicio));
  programs[1].dias                 = 0b1100000;  // sáb=bit5, dom=bit6
  programs[1].retardoEntreSectores = 10;
  programs[1].ciclico              = false;
  programs[1].sectorCount          = 3;
  programs[1].sectores[0]          = {4, 1, 180};
  programs[1].sectores[1]          = {6, 2, 180};
  programs[1].sectores[2]          = {8, 3,  90};
}

// Configura GPIO, RTC, Wi-Fi y servidor HTTP
void setup() {
  Serial.begin(115200);
  delay(300);

  // Configurar pines de sectores como salidas (todos apagados al inicio)
  for (uint8_t i = 0; i < Config::NUM_SECTORES; i++) {
    pinMode(Config::PINES_SECTORES[i], OUTPUT);
    digitalWrite(Config::PINES_SECTORES[i], LOW);

    // GPIO34-39 son entrada-only en el ESP32 y no pueden manejar cargas
    if (Config::PINES_SECTORES[i] >= 34 && Config::PINES_SECTORES[i] <= 39) {
      Serial.print("ADVERTENCIA: GPIO");
      Serial.print(Config::PINES_SECTORES[i]);
      Serial.println(" es solo entrada y no puede manejar una salida.");
    }
  }

  // Configurar pin de la bomba como salida
  pinMode(Config::PIN_BOMBA, OUTPUT);
  digitalWrite(Config::PIN_BOMBA, LOW);

  // Inicializar RTC DS1302
  Serial.println("\n\nInicializando RTC...");
  rtc.writeProtect(false);
  rtc.halt(false);
  delay(50);

  Time now = rtc.time();
  Serial.print("Lectura RTC: ");
  Serial.print(formatRTCDate(now));
  Serial.print(" ");
  Serial.println(formatRTCTime(now));

  if (!isValidDateTime(now.yr, now.mon, now.date, now.hr, now.min, now.sec)) {
    Serial.println("RTC con datos invalidos, intentando inicializar...");

    bool initOk = false;
    const Time defaultTime(2024, 1, 1, 12, 0, 0, calculateDayOfWeek(2024, 1, 1));

    for (int attempt = 0; attempt < 5; attempt++) {
      Serial.print("Intento ");
      Serial.println(attempt + 1);
      rtc.halt(false);
      delay(20);
      rtc.time(defaultTime);
      delay(100);

      Time verify = rtc.time();
      Serial.print("  Verificacion: ");
      Serial.print(formatRTCDate(verify));
      Serial.print(" ");
      Serial.println(formatRTCTime(verify));

      if (isValidDateTime(verify.yr, verify.mon, verify.date,
                          verify.hr,  verify.min,  verify.sec)) {
        Serial.println("RTC inicializado correctamente.");
        initOk = true;
        break;
      }
    }

    if (!initOk) Serial.println("ADVERTENCIA: El RTC sigue reportando datos invalidos.");
  } else {
    Serial.println("RTC con datos validos, no se requiere inicializacion.");
  }

  rtc.writeProtect(true);

  seedDefaultPrograms();
  stopRuntime(STATE_IDLE);

  // Levantar el Access Point Wi-Fi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::AP_SSID, Config::AP_PASSWORD);

  Serial.println();
  Serial.println("==================================");
  Serial.println("ESP32 SISTEMA DE RIEGO INICIADO");
  Serial.print("SSID       : ");
  Serial.println(Config::AP_SSID);
  Serial.print("Contrasena : ");
  Serial.println(Config::AP_PASSWORD);
  Serial.print("IP         : ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Abrir en navegador: http://192.168.4.1");
  Serial.print("Pines RTC - CLK: GPIO");
  Serial.print(Config::RTC_CLK);
  Serial.print(", DAT: GPIO");
  Serial.print(Config::RTC_DAT);
  Serial.print(", RST: GPIO");
  Serial.println(Config::RTC_RST);
  Serial.print("Pin bomba  : GPIO");
  Serial.println(Config::PIN_BOMBA);
  Serial.print("Pines sectores (1-8):");
  for (uint8_t i = 0; i < Config::NUM_SECTORES; i++) {
    Serial.print(" GPIO");
    Serial.print(Config::PINES_SECTORES[i]);
  }
  Serial.println();
  Serial.println("==================================");

  // Registrar rutas HTTP
  server.on("/",              HTTP_GET,  handleRoot);
  server.on("/index.html",    HTTP_GET,  handleRoot);
  server.on("/estado",        HTTP_GET,  handleEstado);
  server.on("/programas",     HTTP_GET,  handleProgramas);
  server.on("/configuracion", HTTP_POST, handleConfiguracion);
  server.on("/control",       HTTP_GET,  handleControl);
  server.on("/rtc",           HTTP_GET,  handleRTC);
  server.on("/rtc",           HTTP_POST, handleRTC);
  server.on("/parada",        HTTP_POST, handleParada);
  server.on("/favicon.ico",   HTTP_GET,  handleFavicon);
  server.onNotFound(handleNotFound);
  server.begin();

  lastStatusPrint = millis();
}

// Loop principal: atiende HTTP, verifica el scheduler y actualiza el runtime
void loop() {
  server.handleClient();
  checkScheduledPrograms();
  updateRuntime();
  printPeriodicStatus();
}
