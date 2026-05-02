#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config/Config.h"
#include "scheduler/RTCManager.h"
#include "domain/IrrigationSystem.h"
#include "web/JsonHelpers.h"
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

WebServer        server(80);
RTCManager       rtcManager(Config::RTC_RST, Config::RTC_DAT, Config::RTC_CLK);
IrrigationSystem irrigationSystem;

unsigned long lastStatusPrint = 0;

// Último minuto procesado por el scheduler (evita disparar el mismo programa dos veces)
uint16_t lastScheduleYear   = 0;
uint8_t  lastScheduleMonth  = 0;
uint8_t  lastScheduleDay    = 0;
uint8_t  lastScheduleHour   = 255;
uint8_t  lastScheduleMinute = 255;

// ============================================================
// Helpers de serialización / debug
// ============================================================

// Formatea la máscara de sectores como texto legible para el monitor serial
String formatSectorMaskForSerial(uint16_t sectorMask) {
  if (sectorMask == 0) return "ninguno";
  String text;
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    if ((sectorMask & ((uint16_t)1U << (i - 1))) == 0) continue;
    if (text.length() > 0) text += ", ";
    text += "S";
    text += String(i);
  }
  return text;
}

// Construye el JSON de estado para GET /estado
String buildEstadoJson() {
  const SystemStateSnapshot snap = irrigationSystem.getStateSnapshot();
  String json = "{";
  json += "\"estado\":\""         + String(snap.stateName)                      + "\",";
  json += "\"programaActivo\":"   + String(snap.activeProgramId)                + ",";
  json += "\"sectorActivo\":"     + String(snap.activeSectorId)                 + ",";
  json += "\"sectoresActivos\":"  + buildSectorArrayJson(snap.activeSectorMask) + ",";
  json += "\"tiempoRestante\":"   + String(snap.remainingTimeSec)               + ",";
  json += "\"bomba\":"            + boolToJson(snap.pumpOn)                     + ",";
  json += "\"modoManual\":"       + boolToJson(snap.manualActive)               + ",";
  json += "\"manualSectorMask\":" + String(snap.manualSectorMask)               + ",";
  json += "\"manualSectorId\":"   + String(snap.firstManualSectorId)            + ",";
  json += "\"manualPumpOn\":false";
  json += "}";
  return json;
}

// Construye la lista completa de programas para GET /programas
String buildProgramasJson() {
  String json = "{\"programas\":[";
  bool firstProgram = true;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    const Program& p = irrigationSystem.programAt(i);
    if (!p.isValid()) continue;
    if (!firstProgram) json += ",";
    firstProgram = false;

    json += "{";
    json += "\"id\":"                   + String(p.getId())                             + ",";
    json += "\"horaInicio\":\""         + escapeJson(String(p.getStartTime()))          + "\",";
    json += "\"dias\":"                 + String(p.getDays())                           + ",";
    json += "\"retardoEntreSectores\":" + String(p.getSectorDelay())                    + ",";
    json += "\"ciclico\":"              + boolToJson(p.isCyclic())                      + ",";
    json += "\"sectores\":[";

    for (uint8_t s = 0; s < p.getSectorCount(); s++) {
      if (s > 0) json += ",";
      json += "{";
      json += "\"id\":"          + String(p.getNode(s).id)            + ",";
      json += "\"orden\":"       + String(p.getNode(s).order)         + ",";
      json += "\"tiempoRiego\":" + String(p.getNode(s).irrigationTime);
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
// Parser de programas — se mueve a ApiHandler en Fase C3
// ============================================================

bool parseProgramFromJson(const String& programJson, Program& outProgram) {
  outProgram.reset();
  outProgram.setValid(true);

  int id = 0;
  extractNullableId(programJson, id);
  outProgram.setId((uint16_t)id);

  String hora;
  uint8_t startHour = 0, startMinute = 0;
  if (!extractStringField(programJson, "horaInicio", hora) ||
      !RTCManager::parseHourMinute(hora.c_str(), startHour, startMinute)) {
    return false;
  }
  outProgram.setStartTime(hora.c_str());

  int dias = 0;
  if (!extractIntField(programJson, "dias", dias) || dias < 0 || dias > 0x7F) return false;
  outProgram.setDays((uint8_t)dias);

  int retardo = 0;
  if (!extractIntField(programJson, "retardoEntreSectores", retardo) ||
      retardo < 0 || retardo > 65535) {
    return false;
  }
  outProgram.setSectorDelay((uint16_t)retardo);

  bool ciclico = false;
  if (!extractBoolField(programJson, "ciclico", ciclico)) return false;
  outProgram.setCyclic(ciclico);

  String sectoresArray;
  if (!extractArrayField(programJson, "sectores", sectoresArray)) return false;

  int pos = 0;
  while (pos < (int)sectoresArray.length() &&
         outProgram.getSectorCount() < Config::NUM_SECTORES) {
    int objStart = sectoresArray.indexOf('{', pos);
    if (objStart < 0) break;

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
      ProgramNode node;
      node.id           = (uint8_t)sectorId;
      node.order        = (uint8_t)orden;
      node.irrigationTime = (uint32_t)tiempo;
      outProgram.addNode(node);
    }

    pos = objEnd + 1;
  }

  if (outProgram.getSectorCount() == 0) return false;

  outProgram.sortNodesByOrder();
  return true;
}

// ============================================================
// Scheduler — se mueve a Scheduler class en Fase C2
// ============================================================

bool shouldStartProgramNow(const Program& program, const Time& now) {
  if (!program.isValid() || program.getSectorCount() == 0) return false;
  if (!rtcManager.isValid(now)) return false;

  const uint8_t dayBit = RTCManager::dayMaskBitFromDate(now.yr, now.mon, now.date);
  if (dayBit > 6 || (program.getDays() & (1U << dayBit)) == 0) return false;

  uint8_t programHour = 0, programMinute = 0;
  if (!RTCManager::parseHourMinute(program.getStartTime(), programHour, programMinute)) return false;

  return programHour == now.hr && programMinute == now.min;
}

void rememberScheduleMinute(const Time& now) {
  lastScheduleYear   = now.yr;
  lastScheduleMonth  = now.mon;
  lastScheduleDay    = now.date;
  lastScheduleHour   = now.hr;
  lastScheduleMinute = now.min;
}

bool isSameScheduleMinute(const Time& now) {
  return now.yr   == lastScheduleYear   &&
         now.mon  == lastScheduleMonth  &&
         now.date == lastScheduleDay    &&
         now.hr   == lastScheduleHour   &&
         now.min  == lastScheduleMinute;
}

void checkScheduledPrograms() {
  const Time now = rtcManager.now();
  if (!rtcManager.isValid(now)) return;
  if (isSameScheduleMinute(now)) return;

  rememberScheduleMinute(now);

  if (irrigationSystem.isRunning() || irrigationSystem.isManualControlActive()) return;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    const Program& p = irrigationSystem.programAt(i);
    if (shouldStartProgramNow(p, now)) {
      irrigationSystem.startProgramById(p.getId());
      return;
    }
  }
}

// ============================================================
// Handlers HTTP
// ============================================================

String getRequestBody() {
  if (server.hasArg("plain")) return server.arg("plain");
  return "";
}

String buildRTCJson() {
  Time now = rtcManager.now();
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

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleEstado() {
  server.send(200, "application/json", buildEstadoJson());
}

void handleProgramas() {
  server.send(200, "application/json", buildProgramasJson());
}

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
    irrigationSystem.setManualSector((uint8_t)id, state != 0);
    server.send(200, "application/json", buildOkJson(true));

  } else if (type == "pump") {
    server.send(409, "application/json",
                buildOkJson(false, "\"error\":\"La bomba sigue automaticamente a los sectores activos\""));
  } else {
    server.send(400, "application/json", buildOkJson(false, "\"error\":\"type invalido\""));
  }
}

void handleParada() {
  irrigationSystem.stop();
  server.send(200, "application/json", buildOkJson(true));
}

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
    bool ok = irrigationSystem.startProgramById((uint16_t)runId);
    server.send(ok ? 200 : 404, "application/json", buildOkJson(ok));
    return;
  }

  if (body.indexOf("\"borrar\"") >= 0) {
    int deleteId = 0;
    if (!extractIntField(body, "borrar", deleteId) || deleteId <= 0) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"borrar invalido\""));
      return;
    }
    if (!irrigationSystem.deleteProgram((uint16_t)deleteId)) {
      server.send(404, "application/json", buildOkJson(false, "\"error\":\"programa no encontrado\""));
      return;
    }
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
    uint16_t assignedId = irrigationSystem.saveProgram(parsed);
    if (assignedId == 0) {
      server.send(507, "application/json",
                  buildOkJson(false, "\"error\":\"sin espacio para mas programas\""));
      return;
    }
    server.send(200, "application/json", buildOkJson(true, "\"id\":" + String(assignedId)));
    return;
  }

  server.send(400, "application/json", buildOkJson(false, "\"error\":\"accion no reconocida\""));
}

void handleFavicon() {
  server.send(204, "text/plain", "");
}

void handleNotFound() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

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

    if (!rtcManager.setTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                             (uint8_t)hour,  (uint8_t)minute, (uint8_t)second)) {
      server.send(400, "application/json", buildOkJson(false, "\"error\":\"fecha/hora invalida\""));
      return;
    }

    server.send(200, "application/json", buildOkJson(true, "\"rtc\":" + buildRTCJson()));
  }
}

// ============================================================
// Estado periódico por serial
// ============================================================

void printPeriodicStatus() {
  unsigned long now = millis();
  if (now - lastStatusPrint < Config::INTERVALO_ESTADO_SERIAL_MS) return;
  lastStatusPrint = now;

  Serial.println();
  Serial.println("===== ESTADO DEL SISTEMA =====");

  Time rtcNow = rtcManager.now();
  Serial.print("Hora RTC       : ");
  Serial.print(RTCManager::formatDate(rtcNow));
  Serial.print(" ");
  Serial.println(RTCManager::formatTime(rtcNow));

  const SystemStateSnapshot snap = irrigationSystem.getStateSnapshot();
  Serial.print("Estado         : ");
  Serial.println(irrigationSystem.isManualControlActive() ? "MANUAL" : snap.stateName);
  Serial.print("Programa activo: ");
  Serial.println(snap.activeProgramId);
  Serial.print("Sectores activos: ");
  Serial.println(formatSectorMaskForSerial(snap.activeSectorMask));
  Serial.print("Tiempo restante: ");
  Serial.print(snap.remainingTimeSec);
  Serial.println(" s");
  Serial.print("Bomba (GPIO");
  Serial.print(irrigationSystem.getPumpPin());
  Serial.print("): ");
  Serial.println(snap.pumpOn ? "ON" : "OFF");
  Serial.print("Modo manual    : ");
  Serial.println(snap.manualActive ? "SI" : "NO");

  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    Serial.print("- Sector ");
    Serial.print(i);
    Serial.print(" (GPIO");
    Serial.print(irrigationSystem.getSectorPin(i));
    Serial.print("): ");
    Serial.println(irrigationSystem.isSectorActive(i) ? "ACTIVO" : "inactivo");
  }

  Serial.println("==============================");
}

// ============================================================
// Inicialización y loop principal
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  irrigationSystem.begin();

  // Advertencia sobre pines de solo entrada en ESP32
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    uint8_t pin = irrigationSystem.getSectorPin(i);
    if (pin >= 34 && pin <= 39) {
      Serial.print("ADVERTENCIA: GPIO");
      Serial.print(pin);
      Serial.println(" es solo entrada y no puede manejar una salida.");
    }
  }

  // Inicializar RTC DS1302
  Serial.println("\n\nInicializando RTC...");
  rtcManager.begin();

  irrigationSystem.seedDefaultPrograms();

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
  Serial.println(irrigationSystem.getPumpPin());
  Serial.print("Pines sectores (1-8):");
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    Serial.print(" GPIO");
    Serial.print(irrigationSystem.getSectorPin(i));
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

void loop() {
  server.handleClient();
  checkScheduledPrograms();
  irrigationSystem.tick();
  printPeriodicStatus();
}
