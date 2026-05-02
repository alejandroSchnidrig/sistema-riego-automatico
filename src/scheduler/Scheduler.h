#pragma once
#include <stdint.h>
#include "RTCManager.h"
#include "../domain/IrrigationSystem.h"
#include "../domain/Program.h"

class Scheduler {
public:
  Scheduler(IrrigationSystem& sys, RTCManager& rtc);
  void tick();

private:
  IrrigationSystem& _sys;
  RTCManager&       _rtc;

  uint16_t _lastYear;
  uint8_t  _lastMonth;
  uint8_t  _lastDay;
  uint8_t  _lastHour;    // 255 = centinela; fuerza evaluación en el primer tick
  uint8_t  _lastMinute;  // 255 = centinela; uint8_t no puede valer 255 en tiempo real

  bool shouldStartProgramNow(const Program& program, const Time& now) const;
  void rememberMinute(const Time& now);
  bool isSameMinute(const Time& now) const;
};
