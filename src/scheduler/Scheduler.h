#pragma once
#include <stdint.h>
#include "RTCManager.h"
#include "../domain/IrrigationSystem.h"
#include "../domain/Program.h"

class Scheduler {
public:
  Scheduler(IrrigationSystem& sys, RTCManager& rtc);
  // Se llama en el loop: una vez por minuto revisa si algún programa debe
  // arrancar a esta hora/día y, si corresponde, se lo pide a IrrigationSystem.
  void tick();

private:
  IrrigationSystem& _sys;
  RTCManager&       _rtc;

  uint16_t _lastYear;
  uint8_t  _lastMonth;
  uint8_t  _lastDay;
  uint8_t  _lastHour;    // 255 = centinela; fuerza evaluación en el primer tick
  uint8_t  _lastMinute;  // 255 = centinela; uint8_t no puede valer 255 en tiempo real

  // ¿Este programa debe arrancar ahora? (válido, día habilitado y hora == now).
  bool shouldStartProgramNow(const Program& program, const RTC_Time& now) const;
  // Guarda fecha/hora del último tick evaluado (para no re-evaluar el mismo minuto).
  void rememberMinute(const RTC_Time& now);
  // ¿'now' cae en el mismo minuto ya evaluado? (evita disparos repetidos).
  bool isSameMinute(const RTC_Time& now) const;
};
