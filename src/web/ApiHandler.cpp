#include "ApiHandler.h"
#include "JsonHelpers.h"
#include "../pages/index_html.h"
#include "../config/Config.h"

ApiHandler::ApiHandler(IrrigationSystem& sys, RTCManager& rtc,
                       StorageManager& storage, WebServer& server)
  : _sys(sys), _rtc(rtc), _storage(storage), _server(server)
{}

// ============================================================
// Handlers HTTP
// ============================================================

void ApiHandler::handleRoot() {
  _server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void ApiHandler::handleStatus() {
  _server.send(200, "application/json", buildStatusJson());
}

void ApiHandler::handlePrograms() {
  _server.send(200, "application/json", buildProgramsJson());
}

void ApiHandler::handleControl() {
  if (!_server.hasArg("type")) {
    _server.send(400, "application/json", buildOkJson(false, "\"error\":\"falta type\""));
    return;
  }

  String type  = _server.arg("type");
  int    state = _server.hasArg("state") ? _server.arg("state").toInt() : 0;

  if (type == "sector") {
    if (!_server.hasArg("id")) {
      _server.send(400, "application/json", buildOkJson(false, "\"error\":\"falta id\""));
      return;
    }
    int id = _server.arg("id").toInt();
    if (id < 1 || id > (int)Config::NUM_SECTORES) {
      _server.send(400, "application/json", buildOkJson(false, "\"error\":\"id invalido\""));
      return;
    }
    _sys.setManualSector((uint8_t)id, state != 0);
    _server.send(200, "application/json", buildOkJson(true));

  } else if (type == "pump") {
    _server.send(409, "application/json",
                 buildOkJson(false, "\"error\":\"La bomba sigue automaticamente a los sectores activos\""));
  } else {
    _server.send(400, "application/json", buildOkJson(false, "\"error\":\"type invalido\""));
  }
}

void ApiHandler::handleStop() {
  _sys.stop();
  _server.send(200, "application/json", buildOkJson(true));
}

void ApiHandler::handleConfig() {
  String body = getRequestBody();
  if (body.length() == 0) {
    _server.send(400, "application/json", buildOkJson(false, "\"error\":\"body vacio\""));
    return;
  }

  if (body.indexOf("\"ejecutar\"") >= 0) {
    int runId = 0;
    if (!extractIntField(body, "ejecutar", runId) || runId <= 0) {
      _server.send(400, "application/json", buildOkJson(false, "\"error\":\"ejecutar invalido\""));
      return;
    }
    bool ok = _sys.startProgramById((uint16_t)runId);
    _server.send(ok ? 200 : 404, "application/json", buildOkJson(ok));
    return;
  }

  if (body.indexOf("\"borrar\"") >= 0) {
    int deleteId = 0;
    if (!extractIntField(body, "borrar", deleteId) || deleteId <= 0) {
      _server.send(400, "application/json", buildOkJson(false, "\"error\":\"borrar invalido\""));
      return;
    }
    if (!_sys.deleteProgram((uint16_t)deleteId)) {
      _server.send(404, "application/json", buildOkJson(false, "\"error\":\"programa no encontrado\""));
      return;
    }
    _storage.savePrograms(_sys);
    _server.send(200, "application/json", buildOkJson(true));
    return;
  }

  if (body.indexOf("\"programa\"") >= 0) {
    String programJson;
    if (!extractObjectField(body, "programa", programJson)) {
      _server.send(400, "application/json", buildOkJson(false, "\"error\":\"programa invalido\""));
      return;
    }
    Program parsed;
    if (!parseProgramFromJson(programJson, parsed)) {
      _server.send(400, "application/json",
                   buildOkJson(false, "\"error\":\"no se pudo parsear programa\""));
      return;
    }
    uint16_t assignedId = _sys.saveProgram(parsed);
    if (assignedId == 0) {
      _server.send(507, "application/json",
                   buildOkJson(false, "\"error\":\"sin espacio para mas programas\""));
      return;
    }
    _storage.savePrograms(_sys);
    _server.send(200, "application/json", buildOkJson(true, "\"id\":" + String(assignedId)));
    return;
  }

  _server.send(400, "application/json", buildOkJson(false, "\"error\":\"accion no reconocida\""));
}

void ApiHandler::handleDebugConfig() {
  String raw = _storage.readRaw();
  if (raw.length() == 0) {
    _server.send(404, "application/json", "{\"error\":\"config.json no existe\"}");
    return;
  }
  _server.send(200, "application/json", raw);
}

void ApiHandler::handleFavicon() {
  _server.send(204, "text/plain", "");
}

void ApiHandler::handleNotFound() {
  _server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void ApiHandler::handleRTC() {
  if (_server.method() == HTTP_GET) {
    _server.send(200, "application/json", buildRTCJson());
    return;
  }

  if (_server.method() == HTTP_POST) {
    if (!_server.hasArg("year")   || !_server.hasArg("month")  || !_server.hasArg("day") ||
        !_server.hasArg("hour")   || !_server.hasArg("minute") || !_server.hasArg("second")) {
      _server.send(400, "application/json", buildOkJson(false, "\"error\":\"faltan parametros\""));
      return;
    }

    int year   = _server.arg("year").toInt();
    int month  = _server.arg("month").toInt();
    int day    = _server.arg("day").toInt();
    int hour   = _server.arg("hour").toInt();
    int minute = _server.arg("minute").toInt();
    int second = _server.arg("second").toInt();

    if (!_rtc.setTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                       (uint8_t)hour, (uint8_t)minute, (uint8_t)second)) {
      _server.send(400, "application/json", buildOkJson(false, "\"error\":\"fecha/hora invalida\""));
      return;
    }

    _server.send(200, "application/json", buildOkJson(true, "\"rtc\":" + buildRTCJson()));
  }
}

// ============================================================
// Builders de JSON
// ============================================================

String ApiHandler::buildStatusJson() const {
  const SystemStateSnapshot snap = _sys.getStateSnapshot();
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

String ApiHandler::buildProgramsJson() const {
  String json = "{\"programas\":[";
  bool firstProgram = true;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    const Program& p = _sys.programAt(i);
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

String ApiHandler::buildRTCJson() {
  Time now = _rtc.now();
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

String ApiHandler::buildOkJson(bool ok, const String& extra) const {
  String json = "{\"ok\":" + boolToJson(ok);
  if (extra.length() > 0) json += "," + extra;
  json += "}";
  return json;
}

String ApiHandler::getRequestBody() {
  if (_server.hasArg("plain")) return _server.arg("plain");
  return "";
}

// ============================================================
// Parser de programas — se mueve a formato árbol en Fase D1
// ============================================================

bool ApiHandler::parseProgramFromJson(const String& programJson, Program& outProgram) {
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
      node.id             = (uint8_t)sectorId;
      node.order          = (uint8_t)orden;
      node.irrigationTime = (uint32_t)tiempo;
      outProgram.addNode(node);
    }

    pos = objEnd + 1;
  }

  if (outProgram.getSectorCount() == 0) return false;

  outProgram.sortNodesByOrder();
  return true;
}
