#include <Arduino.h>
#include "config/Config.h"
#include "scheduler/RTCManager.h"
#include "scheduler/Scheduler.h"
#include "domain/IrrigationSystem.h"
#include "web/WebServer.h"

/*
  ESP32 - Sistema de Riego Automático

  Pines (ver Config.h para valores exactos):
  - RTC DS1302 : CLK=GPIO18, DAT=GPIO19, RST=GPIO21
  - Sectores 1-8: GPIO 13, 14, 16, 17, 32, 33, 25, 26
  - Bomba      : GPIO27

  Endpoints HTTP:
      GET  /estado
      GET  /programas
      POST /configuracion
      GET  /control?type=sector&id=N&state=0|1
      POST /parada
      GET  /rtc
      POST /rtc?year=...&month=...&day=...&hour=...&minute=...&second=...

  Compilar con PlatformIO: pio run --target upload
*/

// El orden de declaración importa: irrigationSystem y rtcManager deben construirse
// antes que scheduler y httpServer, que los reciben por referencia.
RTCManager       rtcManager(Config::RTC_RST, Config::RTC_DAT, Config::RTC_CLK);
IrrigationSystem irrigationSystem;
Scheduler        scheduler(irrigationSystem, rtcManager);
HttpServer       httpServer(irrigationSystem, rtcManager);

unsigned long lastStatusPrint = 0;

// ============================================================
// Helpers de serialización / debug
// ============================================================

// Formatea la máscara de sectores como texto legible para el monitor serial
String formatSectorMaskForSerial(uint16_t sectorMask) {
  if (sectorMask == 0) return "ninguno";
  String text;
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    if ((sectorMask & ((uint16_t)1U << (i - 1))) == 0) continue;
    if (text.length() > 0) text += ", ";
    text += "S";
    text += String(i);
  }
  return text;
}


// ============================================================
// Estado periódico por serial
// ============================================================

void printPeriodicStatus() {
  unsigned long now = millis();
  if (now - lastStatusPrint < Config::INTERVALO_ESTADO_SERIAL_MS) return;
  lastStatusPrint = now;

  Serial.println();
  Serial.println("===== ESTADO DEL SISTEMA =====");

  Time rtcNow = rtcManager.now();
  Serial.print("Hora RTC       : ");
  Serial.print(RTCManager::formatDate(rtcNow));
  Serial.print(" ");
  Serial.println(RTCManager::formatTime(rtcNow));

  const SystemStateSnapshot snap = irrigationSystem.getStateSnapshot();
  Serial.print("Estado         : ");
  Serial.println(irrigationSystem.isManualControlActive() ? "MANUAL" : snap.stateName);
  Serial.print("Programa activo: ");
  Serial.println(snap.activeProgramId);
  Serial.print("Sectores activos: ");
  Serial.println(formatSectorMaskForSerial(snap.activeSectorMask));
  Serial.print("Tiempo restante: ");
  Serial.print(snap.remainingTimeSec);
  Serial.println(" s");
  Serial.print("Bomba (GPIO");
  Serial.print(irrigationSystem.getPumpPin());
  Serial.print("): ");
  Serial.println(snap.pumpOn ? "ON" : "OFF");
  Serial.print("Modo manual    : ");
  Serial.println(snap.manualActive ? "SI" : "NO");

  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    Serial.print("- Sector ");
    Serial.print(i);
    Serial.print(" (GPIO");
    Serial.print(irrigationSystem.getSectorPin(i));
    Serial.print("): ");
    Serial.println(irrigationSystem.isSectorActive(i) ? "ACTIVO" : "inactivo");
  }

  Serial.println("==============================");
}

// ============================================================
// Inicialización y loop principal
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  irrigationSystem.begin();

  // Advertencia sobre pines de solo entrada en ESP32
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    uint8_t pin = irrigationSystem.getSectorPin(i);
    if (pin >= 34 && pin <= 39) {
      Serial.print("ADVERTENCIA: GPIO");
      Serial.print(pin);
      Serial.println(" es solo entrada y no puede manejar una salida.");
    }
  }

  // Inicializar RTC DS1302
  Serial.println("\n\nInicializando RTC...");
  rtcManager.begin();

  irrigationSystem.seedDefaultPrograms();

  // Levantar el Access Point Wi-Fi y registrar rutas
  httpServer.begin();

  // Información de pines de hardware
  Serial.print("Pines RTC - CLK: GPIO");
  Serial.print(Config::RTC_CLK);
  Serial.print(", DAT: GPIO");
  Serial.print(Config::RTC_DAT);
  Serial.print(", RST: GPIO");
  Serial.println(Config::RTC_RST);
  Serial.print("Pin bomba  : GPIO");
  Serial.println(irrigationSystem.getPumpPin());
  Serial.print("Pines sectores (1-8):");
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    Serial.print(" GPIO");
    Serial.print(irrigationSystem.getSectorPin(i));
  }
  Serial.println();

  lastStatusPrint = millis();
}

void loop() {
  httpServer.handleClient();
  scheduler.tick();
  irrigationSystem.tick();
  printPeriodicStatus();
}
