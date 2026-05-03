#pragma once

#include "../domain/IrrigationSystem.h"

// Único componente del sistema que interactúa con LittleFS.
// Ninguna otra clase debe incluir <LittleFS.h> ni abrir archivos directamente.
class StorageManager {
public:
  // Monta LittleFS. Si falla el montaje, el sistema sigue funcionando sin
  // persistencia (los programas semilla se usan en cada arranque).
  // Retorna true si el sistema de archivos quedó listo.
  bool begin();

  // Lee /config.json y carga los programas en `sys` via sys.saveProgram().
  // Retorna false si el archivo no existe, está vacío o el JSON es inválido.
  bool loadPrograms(IrrigationSystem& sys);

  // Serializa todos los programas válidos de `sys` y escribe /config.json.
  // Retorna false si no se pudo abrir el archivo para escritura.
  bool savePrograms(const IrrigationSystem& sys);

  // Devuelve el contenido crudo de /config.json como String.
  // Retorna string vacío si el archivo no existe o no se puede leer.
  String readRaw();

private:
  static bool   parseOneProgram(const String& json, Program& out);
  static String buildConfigJson(const IrrigationSystem& sys);

  static constexpr const char* CONFIG_PATH = "/config.json";
};
