#include "WebServer.h"
#include "../config/Config.h"

namespace {
  const byte DNS_PORT = 53;
}

HttpServer::HttpServer(IrrigationSystem& sys, RTCManager& rtc, StorageManager& storage)
  : _server(80),
    _api(sys, rtc, storage, _server)
{}

void HttpServer::begin() {
  IPAddress apIp;
  apIp.fromString(Config::AP_IP);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIp, apIp, subnet);
  WiFi.softAP(Config::AP_SSID, Config::AP_PASSWORD);
  _dnsServer.start(DNS_PORT, "*", apIp);

  Serial.println();
  Serial.println("==================================");
  Serial.println("ESP32 SISTEMA DE RIEGO INICIADO");
  Serial.print("SSID       : ");
  Serial.println(Config::AP_SSID);
  Serial.print("Contrasena : ");
  Serial.println(Config::AP_PASSWORD);
  Serial.print("IP         : ");
  Serial.println(WiFi.softAPIP());
  Serial.print("URL        : http://");
  Serial.println(Config::AP_DNS_NAME);
  Serial.print("URL backup : http://");
  Serial.println(WiFi.softAPIP());
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
  _server.on("/debug/config",  HTTP_GET,  [this]() { _api.handleDebugConfig(); });
  _server.on("/favicon.ico",   HTTP_GET,  [this]() { _api.handleFavicon(); });
  _server.onNotFound([this]() { _api.handleNotFound(); });
  _server.begin();
}

void HttpServer::handleClient() {
  _dnsServer.processNextRequest();
  _server.handleClient();
}
