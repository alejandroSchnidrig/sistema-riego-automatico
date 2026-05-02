#pragma once

#include <Arduino.h>

// ============================================================
// Config.h — Centraliza todos los pines GPIO, credenciales
//             Wi-Fi e intervalos de temporización del sistema.
//
// Ninguna clase del dominio ni ningún handler HTTP debe
// definir constantes de hardware: todo pasa por aquí.
// ============================================================

namespace Config {

  // ----------------------------------------------------------
  // Pines de sectores (válvulas solenoides / LEDs en maqueta)
  // Sector N → índice N-1 en este arreglo
  // ----------------------------------------------------------
  constexpr uint8_t NUM_SECTORES = 8;
  constexpr uint8_t PINES_SECTORES[NUM_SECTORES] = {13, 14, 16, 17, 32, 33, 25, 26};

  // ----------------------------------------------------------
  // Pin de la bomba de agua central
  // ----------------------------------------------------------
  constexpr uint8_t PIN_BOMBA = 27;

  // ----------------------------------------------------------
  // Pines del módulo RTC DS1302 (bit-banging, no I2C)
  // ----------------------------------------------------------
  constexpr uint8_t RTC_CLK = 18;  // línea de reloj
  constexpr uint8_t RTC_DAT = 19;  // línea de datos
  constexpr uint8_t RTC_RST = 21;  // reset / chip select

  // ----------------------------------------------------------
  // Configuración del Access Point Wi-Fi
  // ----------------------------------------------------------
  constexpr char AP_SSID[]     = "Riego-ESP32";
  constexpr char AP_PASSWORD[] = "riego12345";
  constexpr char AP_IP[]       = "192.168.4.1";

  // ----------------------------------------------------------
  // Límites de almacenamiento
  // ----------------------------------------------------------
  constexpr uint8_t MAX_PROGRAMAS = 10;  // cantidad máxima de programas guardados

  // ----------------------------------------------------------
  // Intervalos de temporización (milisegundos)
  // ----------------------------------------------------------

  // Frecuencia con que el Scheduler consulta el RTC (Fase C)
  constexpr uint16_t INTERVALO_SCHEDULER_MS = 1000;

  // Frecuencia con que se imprime el estado por el monitor serial
  constexpr uint32_t INTERVALO_ESTADO_SERIAL_MS = 20000;

}  // namespace Config
