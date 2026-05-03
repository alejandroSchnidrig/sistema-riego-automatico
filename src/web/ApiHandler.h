#pragma once
#include <WebServer.h>
#include "../domain/IrrigationSystem.h"
#include "../scheduler/RTCManager.h"
#include "../storage/StorageManager.h"
#include "../domain/Program.h"

class ApiHandler {
public:
  ApiHandler(IrrigationSystem& sys, RTCManager& rtc,
             StorageManager& storage, WebServer& server);

  void handleRoot();
  void handleStatus();
  void handlePrograms();
  void handleControl();
  void handleStop();
  void handleConfig();
  void handleFavicon();
  void handleNotFound();
  void handleRTC();
  void handleDebugConfig();

private:
  IrrigationSystem& _sys;
  RTCManager&       _rtc;
  StorageManager&   _storage;
  WebServer&        _server;

  String buildStatusJson() const;
  String buildProgramsJson() const;
  String buildRTCJson();
  String buildOkJson(bool ok, const String& extra = "") const;
  String getRequestBody();
  bool   parseProgramFromJson(const String& programJson, Program& outProgram);
};
