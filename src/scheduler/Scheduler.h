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
  uint8_t  _lastHour;
  uint8_t  _lastMinute;

  bool shouldStartProgramNow(const Program& program, const Time& now) const;
  void rememberMinute(const Time& now);
  bool isSameMinute(const Time& now) const;
};
