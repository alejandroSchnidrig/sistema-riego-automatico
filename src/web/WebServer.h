#pragma once
#include <WebServer.h>
#include <WiFi.h>
#include "ApiHandler.h"
#include "../domain/IrrigationSystem.h"
#include "../scheduler/RTCManager.h"

// HttpServer evita colisión de nombres con la clase WebServer del ESP32.
// Encapsula el servidor HTTP (puerto 80), el Access Point Wi-Fi y el registro
// de rutas. Delega cada request al ApiHandler.
class HttpServer {
public:
  HttpServer(IrrigationSystem& sys, RTCManager& rtc);
  void begin();
  void handleClient();

private:
  WebServer  _server;   // instancia del WebServer del ESP32 en puerto 80; declarado antes que _api
  ApiHandler _api;
};
