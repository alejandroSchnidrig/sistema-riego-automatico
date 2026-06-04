#include "../core/Storage.h"
#include "StorageManager.h"
#include "../web/JsonHelpers.h"
#include "../config/Config.h"

// ============================================================
// Ciclo de vida
// ============================================================

bool StorageManager::begin() {
  if (!hal_storage_begin()) {
    Serial.println("[Storage] ERROR: FS no pudo montarse");
    return false;
  }
  Serial.println("[Storage] FS montado OK");
  return true;
}

// ============================================================
// Carga
// ============================================================

bool StorageManager::loadPrograms(IrrigationSystem& sys) {
  if (!hal_storage_exists(CONFIG_PATH)) {
    Serial.println("[Storage] config.json no existe — usando programas semilla");
    return false;
  }

  String json = hal_storage_read(CONFIG_PATH);
  if (json.length() == 0) {
    Serial.println("[Storage] config.json vacio");
    return false;
  }

  String programasArray;
  if (!extractArrayField(json, "programas", programasArray)) {
    Serial.println("[Storage] JSON invalido: sin campo 'programas'");
    return false;
  }

  // Restaurar el caudal de la bomba (límite global) si está presente.
  int caudalBomba = 0;
  if (extractIntField(json, "caudalBomba", caudalBomba) && caudalBomba > 0) {
    sys.setPumpFlow((uint16_t)caudalBomba);
  }

  // Restaurar el caudal manual por sector (arreglo de NUM_SECTORES valores).
  String caudalManualArr;
  if (extractArrayField(json, "caudalManual", caudalManualArr)) {
    int pos = 0;
    for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
      while (pos < (int)caudalManualArr.length() &&
             (caudalManualArr[pos] < '0' || caudalManualArr[pos] > '9')) pos++;
      if (pos >= (int)caudalManualArr.length()) break;
      int value = 0, endPos = pos;
      if (!extractIntAt(caudalManualArr, pos, value, endPos)) break;
      if (value > 0) sys.setManualSectorFlow(i, (uint16_t)value);
      pos = endPos;
    }
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
    if (parseOneProgram(item, p) && sys.saveProgram(p) > 0) {
      loaded++;
    } else {
      Serial.println("[Storage] Programa con JSON invalido, ignorado");
    }

    pos = objEnd + 1;
  }

  Serial.print("[Storage] ");
  Serial.print(loaded);
  Serial.println(" programa(s) cargados desde config.json");
  return loaded > 0;
}

// ============================================================
// Guardado
// ============================================================

bool StorageManager::savePrograms(const IrrigationSystem& sys) {
  String json = buildConfigJson(sys);
  
  if (!hal_storage_write(CONFIG_PATH, json)) {
    Serial.println("[Storage] ERROR: no se pudo escribir config.json");
    return false;
  }

  Serial.print("[Storage] config.json guardado (");
  Serial.print(json.length());
  Serial.println(" bytes)");
  return true;
}

// ============================================================
// Debug
// ============================================================

String StorageManager::readRaw() {
  return hal_storage_read(CONFIG_PATH);
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

  // nombre: opcional (formato viejo no lo tiene).
  String nombre;
  if (extractStringField(json, "nombre", nombre)) {
    out.setName(nombre.c_str());
  }

  // horaInicio: string "HH:MM" — se acepta tal cual, ya fue validado al guardarse
  String hora;
  if (!extractStringField(json, "horaInicio", hora) || hora.length() != 5) return false;
  out.setStartTime(hora.c_str());

  // horaFin: opcional, "HH:MM" o null/ausente ("").
  String horaFin;
  if (extractStringField(json, "horaFin", horaFin) && horaFin.length() == 5) {
    out.setEndTime(horaFin.c_str());
  }

  int dias = 0;
  if (!extractIntField(json, "dias", dias) || dias < 0 || dias > 0x7F) return false;
  out.setDays((uint8_t)dias);

  bool ciclico = false;
  if (!extractBoolField(json, "ciclico", ciclico)) return false;
  out.setCyclic(ciclico);

  String nodosArray;
  if (!extractArrayField(json, "nodos", nodosArray)) return false;

  int pos = 0;
  while (pos < (int)nodosArray.length() && out.getSectorCount() < Config::NUM_SECTORES) {
    int objStart = nodosArray.indexOf('{', pos);
    if (objStart < 0) break;

    int depth = 0, objEnd = -1;
    for (int i = objStart; i < (int)nodosArray.length(); i++) {
      if      (nodosArray[i] == '{') depth++;
      else if (nodosArray[i] == '}') {
        depth--;
        if (depth == 0) { objEnd = i; break; }
      }
    }
    if (objEnd < 0) break;

    String item     = nodosArray.substring(objStart, objEnd + 1);
    int sectorId = 0, tiempo = 0, retardo = 0, padre = 0, caudal = 0;

    if (extractIntField(item, "sectorId",    sectorId) &&
        extractIntField(item, "tiempoRiego", tiempo)   &&
        sectorId >= 1 && sectorId <= (int)Config::NUM_SECTORES &&
        tiempo   > 0) {
      extractIntField(item, "retardo", retardo);
      extractNullableIntField(item, "padre", padre); // null → 0 (raíz)
      extractIntField(item, "caudal",  caudal);
      ProgramNode node;
      node.sectorId       = (uint8_t)sectorId;
      node.irrigationTime = (uint32_t)tiempo;
      node.delay          = (retardo >= 0 && retardo <= 65535) ? (uint16_t)retardo : 0;
      node.parentSectorId = (padre >= 0 && padre <= (int)Config::NUM_SECTORES)
                              ? (uint8_t)padre : 0;
      node.flow           = (caudal > 0) ? (uint16_t)caudal : 0;
      out.addNode(node);
    }

    pos = objEnd + 1;
  }

  if (out.getSectorCount() == 0) return false;

  return true;
}

String StorageManager::buildConfigJson(const IrrigationSystem& sys) {
  String json = "{\"caudalBomba\":" + String(sys.getPumpFlow()) + ",";

  // Caudal manual por sector (arreglo de NUM_SECTORES valores, L/min).
  json += "\"caudalManual\":[";
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    if (i > 1) json += ",";
    json += String(sys.getManualSectorFlow(i));
  }
  json += "],\"programas\":[";
  bool firstProgram = true;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    const Program& p = sys.programAt(i);
    if (!p.isValid()) continue;
    if (!firstProgram) json += ",";
    firstProgram = false;

    json += "{";
    json += "\"id\":"           + String(p.getId())                    + ",";
    json += "\"nombre\":\""     + escapeJson(String(p.getName()))      + "\",";
    json += "\"horaInicio\":\"" + escapeJson(String(p.getStartTime())) + "\",";
    json += "\"horaFin\":"      + (p.getEndTime()[0] == '\0'
                                     ? String("null")
                                     : ("\"" + escapeJson(String(p.getEndTime())) + "\"")) + ",";
    json += "\"dias\":"         + String(p.getDays())                  + ",";
    json += "\"ciclico\":"      + boolToJson(p.isCyclic())             + ",";
    json += "\"nodos\":[";

    for (uint8_t s = 0; s < p.getSectorCount(); s++) {
      if (s > 0) json += ",";
      const ProgramNode& node = p.getNode(s);
      json += "{";
      json += "\"sectorId\":"    + String(node.sectorId)       + ",";
      json += "\"tiempoRiego\":" + String(node.irrigationTime) + ",";
      json += "\"retardo\":"     + String(node.delay)          + ",";
      // raíz → "padre": null; hijo → número del sector padre
      json += "\"padre\":"       + (node.parentSectorId == 0
                                      ? String("null")
                                      : String(node.parentSectorId)) + ",";
      json += "\"caudal\":"      + String(node.flow);
      json += "}";
    }

    json += "]}";
  }

  json += "]}";
  return json;
}
