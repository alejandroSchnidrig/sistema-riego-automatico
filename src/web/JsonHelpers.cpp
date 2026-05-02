#include "JsonHelpers.h"
#include "config/Config.h"

// ============================================================
// Serialización
// ============================================================

// Convierte un booleano a su representación literal en JSON: "true" o "false"
String boolToJson(bool value) {
  return value ? "true" : "false";
}

// Recorre el string de entrada y escapa los caracteres que tienen significado
// especial en JSON: comillas dobles, barras invertidas y saltos de línea.
// Necesario para incrustar strings arbitrarios dentro de un objeto JSON.
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

// Recibe una máscara de bits donde bit0=sector1 … bit7=sector8 y devuelve
// un arreglo JSON con los IDs de los sectores activos.
// Ejemplo: sectorMask=0b00000101 → "[1,3]"
String buildSectorArrayJson(uint16_t sectorMask) {
  String json = "[";
  bool first = true;
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    // Equivale a sectorIdToMask(i): sector i ocupa el bit (i-1)
    if ((sectorMask & ((uint16_t)1U << (i - 1))) == 0) continue;
    if (!first) json += ",";
    first = false;
    json += String(i);
  }
  json += "]";
  return json;
}

// ============================================================
// Parser hand-rolled
// ============================================================

// Intenta leer un entero (con signo opcional) a partir de startPos dentro de src,
// salteando espacios en blanco iniciales. Si tiene éxito, escribe el valor en
// `value` y la posición del primer carácter después del número en `endPos`.
// Retorna false si no encuentra dígitos.
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

// Busca el campo `"key": <número>` en un objeto JSON plano y escribe el valor
// entero en `out`. Retorna false si el campo no existe o no es un número.
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

// Busca el campo `"key": true|false` en un objeto JSON plano y escribe el valor
// booleano en `out`. Retorna false si el campo no existe o no es un booleano.
bool extractBoolField(const String& json, const String& key, bool& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  // Saltear espacios entre los dos puntos y el valor
  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("true",  pos)) { out = true;  return true; }
  if (json.startsWith("false", pos)) { out = false; return true; }
  return false;
}

// Busca el campo `"key": "valor"` en un objeto JSON plano y escribe el string
// (sin las comillas) en `out`. Retorna false si el campo no existe o no es string.
bool extractStringField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  // Saltear espacios hasta la comilla de apertura
  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (pos >= (int)json.length() || json[pos] != '"') return false;

  pos++;  // saltar la comilla de apertura
  int end = json.indexOf('"', pos);
  if (end < 0) return false;

  out = json.substring(pos, end);
  return true;
}

// Caso especial para el campo "id": acepta null, ausencia del campo o un número.
// En los dos primeros casos escribe 0 en `out` (señal de "crear nuevo programa").
bool extractNullableId(const String& json, int& out) {
  String pattern = "\"id\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) { out = 0; return true; }  // campo ausente → id nuevo

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;
  pos++;

  // Saltear espacios
  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
    pos++;
  }

  if (json.startsWith("null", pos)) { out = 0; return true; }  // null → id nuevo

  int endPos = pos;
  return extractIntAt(json, pos, out, endPos);
}

// Busca el campo `"key": { ... }` en json y devuelve en `out` el substring
// completo del objeto anidado, respetando llaves balanceadas para no cortar
// en llaves internas de objetos más profundos. Retorna false si no lo encuentra.
bool extractObjectField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;

  int start = json.indexOf('{', pos);
  if (start < 0) return false;

  // Recorrer el string contando llaves para encontrar el cierre correcto
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

// Busca el campo `"key": [ ... ]` en json y devuelve en `out` el substring
// completo del arreglo, respetando corchetes balanceados para arreglos anidados.
// Retorna false si el campo no existe o no es un arreglo.
bool extractArrayField(const String& json, const String& key, String& out) {
  String pattern = "\"" + key + "\"";
  int pos = json.indexOf(pattern);
  if (pos < 0) return false;

  pos = json.indexOf(':', pos);
  if (pos < 0) return false;

  int start = json.indexOf('[', pos);
  if (start < 0) return false;

  // Recorrer el string contando corchetes para encontrar el cierre correcto
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
