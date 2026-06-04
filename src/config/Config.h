#pragma once

#include <stdint.h>

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
  constexpr bool    SECTORES_ACTIVOS_BAJO[NUM_SECTORES] = {true, false, false, false, false, false, false, false};

  // ----------------------------------------------------------
  // Pin de la bomba de agua central
  // ----------------------------------------------------------
  constexpr uint8_t PIN_BOMBA = 27;
  constexpr bool    BOMBA_ACTIVA_BAJO = true;

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
  constexpr char AP_DNS_NAME[] = "riego.local";

  // ----------------------------------------------------------
  // Límites de almacenamiento
  // ----------------------------------------------------------
  constexpr uint8_t MAX_PROGRAMAS = 10;  // cantidad máxima de programas guardados

  // Longitud máxima del nombre de un programa (incluye el '\0' final).
  constexpr uint8_t NOMBRE_PROGRAMA_MAX_LEN = 32;

  // ----------------------------------------------------------
  // Caudal de la bomba (modelo árbol + caudal)
  // Límite global de caudal simultáneo en L/min; gobierna la
  // concurrencia de sectores y la cola FIFO.
  // ----------------------------------------------------------
  constexpr uint16_t CAUDAL_BOMBA_DEFAULT = 20;

  // Caudal asumido (L/min) al encender un sector en modo manual. Un sector
  // manual no tiene caudal propio (ese vive en el ProgramNode), así que se
  // asume este consumo por sector para respetar el límite de la bomba.
  constexpr uint16_t CAUDAL_MANUAL_DEFAULT = 2;

  // ----------------------------------------------------------
  // Intervalos de temporización (milisegundos)
  // ----------------------------------------------------------

  // Frecuencia con que el Scheduler consulta el RTC (Fase C)
  constexpr uint16_t INTERVALO_SCHEDULER_MS = 1000;

  // Frecuencia con que se imprime el estado por el monitor serial
  constexpr uint32_t INTERVALO_ESTADO_SERIAL_MS = 20000;

}  // namespace Config
