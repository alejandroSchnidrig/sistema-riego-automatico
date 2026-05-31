#include "../src/core/Storage.h"
#include <map>

//Mock de funciones de almacenamiento para tests unitarios en entorno nativo (PC) sin LittleFS. Permite simular la existencia de archivos y su contenido usando un mapa en memoria.

std::map<String, String> mock_fs;
bool mock_fs_initialized = false;

bool hal_storage_begin() {
    mock_fs_initialized = true;
    return true;
}

bool hal_storage_exists(const char* path) {
    return mock_fs.count(String(path)) > 0;
}

String hal_storage_read(const char* path) {
    String p(path);
    if (mock_fs.count(p)) {
        return mock_fs[p];
    }
    return "";
}

bool hal_storage_write(const char* path, const String& content) {
    mock_fs[String(path)] = content;
    return true;
}
