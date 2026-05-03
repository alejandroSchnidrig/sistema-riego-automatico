#include "WebServer.h"
#include "../config/Config.h"

HttpServer::HttpServer(IrrigationSystem& sys, RTCManager& rtc, StorageManager& storage)
  : _server(80),
    _api(sys, rtc, storage, _server)
{}

void HttpServer::begin() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::AP_SSID, Config::AP_PASSWORD);

  Serial.println();
  Serial.println("==================================");
  Serial.println("ESP32 SISTEMA DE RIEGO INICIADO");
  Serial.print("SSID       : ");
  Serial.println(Config::AP_SSID);
  Serial.print("Contrasena : ");
  Serial.println(Config::AP_PASSWORD);
  Serial.print("IP         : ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Abrir en navegador: http://192.168.4.1");
  Serial.println("==================================");

  _server.on("/",              HTTP_GET,  [this]() { _api.handleRoot(); });
  _server.on("/index.html",    HTTP_GET,  [this]() { _api.handleRoot(); });
  _server.on("/estado",        HTTP_GET,  [this]() { _api.handleStatus(); });
  _server.on("/programas",     HTTP_GET,  [this]() { _api.handlePrograms(); });
  _server.on("/configuracion", HTTP_POST, [this]() { _api.handleConfig(); });
  _server.on("/control",       HTTP_GET,  [this]() { _api.handleControl(); });
  _server.on("/rtc",           HTTP_GET,  [this]() { _api.handleRTC(); });
  _server.on("/rtc",           HTTP_POST, [this]() { _api.handleRTC(); });
  _server.on("/parada",        HTTP_POST, [this]() { _api.handleStop(); });
  _server.on("/favicon.ico",   HTTP_GET,  [this]() { _api.handleFavicon(); });
  _server.onNotFound([this]() { _api.handleNotFound(); });
  _server.begin();
}

void HttpServer::handleClient() {
  _server.handleClient();
}
