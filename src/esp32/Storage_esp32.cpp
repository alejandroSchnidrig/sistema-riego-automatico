#include "../core/Storage.h"

#ifndef NATIVE_TEST

#include <LittleFS.h>
#include <Arduino.h>

bool hal_storage_begin() {
    return LittleFS.begin(true);
}

bool hal_storage_exists(const char* path) {
    return LittleFS.exists(path);
}

String hal_storage_read(const char* path) {
    if (!LittleFS.exists(path)) return "";
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
}

bool hal_storage_write(const char* path, const String& content) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.print(content);
    f.close();
    return true;
}

#endif // ARDUINO
