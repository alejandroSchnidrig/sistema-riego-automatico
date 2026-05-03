#include "StorageManager.h"
#include <LittleFS.h>
#include "../web/JsonHelpers.h"
#include "../config/Config.h"

// ============================================================
// Ciclo de vida
// ============================================================

bool StorageManager::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("[Storage] ERROR: LittleFS no pudo montarse");
    return false;
  }
  Serial.println("[Storage] LittleFS montado OK");
  return true;
}

// ============================================================
// Carga
// ============================================================

bool StorageManager::loadPrograms(IrrigationSystem& sys) {
  if (!LittleFS.exists(CONFIG_PATH)) {
    Serial.println("[Storage] config.json no existe — usando programas semilla");
    return false;
  }

  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    Serial.println("[Storage] ERROR: no se pudo abrir config.json");
    return false;
  }

  String json = f.readString();
  f.close();

  if (json.length() == 0) {
    Serial.println("[Storage] config.json vacio");
    return false;
  }

  String programasArray;
  if (!extractArrayField(json, "programas", programasArray)) {
    Serial.println("[Storage] JSON invalido: sin campo 'programas'");
    return false;
  }

  int loaded = 0;
  int pos    = 0;
  while (pos < (int)programasArray.length() && loaded < Config::MAX_PROGRAMAS) {
    int objStart = programasArray.indexOf('{', pos);
    if (objStart < 0) break;

    int depth = 0, objEnd = -1;
    for (int i = objStart; i < (int)programasArray.length(); i++) {
      if      (programasArray[i] == '{') depth++;
      else if (programasArray[i] == '}') {
        depth--;
        if (depth == 0) { objEnd = i; break; }
      }
    }
    if (objEnd < 0) break;

    String item = programasArray.substring(objStart, objEnd + 1);
    Program p;
    if (parseOneProgram(item, p)) {
      sys.saveProgram(p);
      loaded++;
    } else {
      Serial.println("[Storage] Programa con JSON invalido, ignorado");
    }

    pos = objEnd + 1;
  }

  Serial.printf("[Storage] %d programa(s) cargados desde config.json\n", loaded);
  return loaded > 0;
}

// ============================================================
// Guardado
// ============================================================

bool StorageManager::savePrograms(const IrrigationSystem& sys) {
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) {
    Serial.println("[Storage] ERROR: no se pudo escribir config.json");
    return false;
  }

  String json = buildConfigJson(sys);
  f.print(json);
  f.close();

  Serial.printf("[Storage] config.json guardado (%d bytes)\n", json.length());
  return true;
}

// ============================================================
// Debug
// ============================================================

String StorageManager::readRaw() {
  if (!LittleFS.exists(CONFIG_PATH)) return "";
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return "";
  String content = f.readString();
  f.close();
  return content;
}

// ============================================================
// Privados
// ============================================================

bool StorageManager::parseOneProgram(const String& json, Program& out) {
  out.reset();
  out.setValid(true);

  // ID: puede ser null o estar ausente (programa nuevo)
  int id = 0;
  extractNullableId(json, id);
  out.setId((uint16_t)id);

  // horaInicio: string "HH:MM" — se acepta tal cual, ya fue validado al guardarse
  String hora;
  if (!extractStringField(json, "horaInicio", hora) || hora.length() != 5) return false;
  out.setStartTime(hora.c_str());

  int dias = 0;
  if (!extractIntField(json, "dias", dias) || dias < 0 || dias > 0x7F) return false;
  out.setDays((uint8_t)dias);

  int retardo = 0;
  if (!extractIntField(json, "retardoEntreSectores", retardo) ||
      retardo < 0 || retardo > 65535) {
    return false;
  }
  out.setSectorDelay((uint16_t)retardo);

  bool ciclico = false;
  if (!extractBoolField(json, "ciclico", ciclico)) return false;
  out.setCyclic(ciclico);

  String sectoresArray;
  if (!extractArrayField(json, "sectores", sectoresArray)) return false;

  int pos = 0;
  while (pos < (int)sectoresArray.length() && out.getSectorCount() < Config::NUM_SECTORES) {
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

    String item     = sectoresArray.substring(objStart, objEnd + 1);
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
      out.addNode(node);
    }

    pos = objEnd + 1;
  }

  if (out.getSectorCount() == 0) return false;

  out.sortNodesByOrder();
  return true;
}

String StorageManager::buildConfigJson(const IrrigationSystem& sys) {
  String json = "{\"programas\":[";
  bool firstProgram = true;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    const Program& p = sys.programAt(i);
    if (!p.isValid()) continue;
    if (!firstProgram) json += ",";
    firstProgram = false;

    json += "{";
    json += "\"id\":"                   + String(p.getId())                    + ",";
    json += "\"horaInicio\":\""         + escapeJson(String(p.getStartTime())) + "\",";
    json += "\"dias\":"                 + String(p.getDays())                  + ",";
    json += "\"retardoEntreSectores\":" + String(p.getSectorDelay())           + ",";
    json += "\"ciclico\":"              + boolToJson(p.isCyclic())             + ",";
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
