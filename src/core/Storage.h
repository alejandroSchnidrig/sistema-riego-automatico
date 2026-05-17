#pragma once
#include "Arduino.h"

// Abstracción de funciones de hardware para permitir tests unitarios en entorno nativo (PC) sin LittleFS.

bool hal_storage_begin();
bool hal_storage_exists(const char* path);
String hal_storage_read(const char* path);
bool hal_storage_write(const char* path, const String& content);
