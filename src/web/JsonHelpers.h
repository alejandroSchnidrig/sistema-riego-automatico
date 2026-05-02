#pragma once

#include <Arduino.h>

// ============================================================
// JsonHelpers.h — Utilidades de serialización y parsing JSON.
//
// Regla de uso: solo este módulo y ApiHandler tocan JSON.
// Las clases del dominio (Sector, Program, etc.) nunca deben
// incluir este header ni armar strings JSON directamente.
// ============================================================

// ----------------------------------------------------------
// Serialización
// ----------------------------------------------------------

// Serializa un booleano como literal JSON ("true" / "false")
String boolToJson(bool value);

// Escapa caracteres especiales de un string para incrustarlo en JSON
String escapeJson(const String& input);

// Construye un arreglo JSON con los IDs de los sectores activos en sectorMask
// Ejemplo: máscara 0b00000101 → "[1,3]"
String buildSectorArrayJson(uint16_t sectorMask);

// ----------------------------------------------------------
// Parser hand-rolled (sin ArduinoJson — decisión de sprint)
// ----------------------------------------------------------

// Parsea un entero a partir de startPos en src.
// Actualiza endPos al primer carácter después del número.
bool extractIntAt(const String& src, int startPos, int& value, int& endPos);

// Extrae el valor entero del campo "key" de un objeto JSON plano
bool extractIntField(const String& json, const String& key, int& out);

// Extrae el valor booleano del campo "key" de un objeto JSON plano
bool extractBoolField(const String& json, const String& key, bool& out);

// Extrae el valor string del campo "key" de un objeto JSON plano
bool extractStringField(const String& json, const String& key, String& out);

// Extrae el campo "id" que puede ser null o estar ausente (retorna 0 en esos casos)
bool extractNullableId(const String& json, int& out);

// Extrae un objeto JSON anidado bajo "key" (devuelve el substring {...} balanceado)
bool extractObjectField(const String& json, const String& key, String& out);

// Extrae un arreglo JSON bajo "key" (devuelve el substring [...] balanceado)
bool extractArrayField(const String& json, const String& key, String& out);
