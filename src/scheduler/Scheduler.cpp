#include "Scheduler.h"
#include "../config/Config.h"

Scheduler::Scheduler(IrrigationSystem& sys, RTCManager& rtc)
  : _sys(sys),
    _rtc(rtc),
    _lastYear(0),
    _lastMonth(0),
    _lastDay(0),
    _lastHour(255),
    _lastMinute(255)
{}

void Scheduler::tick() {
  const Time now = _rtc.now();
  if (!_rtc.isValid(now)) return;
  if (isSameMinute(now)) return;

  rememberMinute(now);

  if (_sys.isRunning() || _sys.isManualControlActive()) return;

  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    const Program& p = _sys.programAt(i);
    if (shouldStartProgramNow(p, now)) {
      _sys.startProgramById(p.getId());
      return;
    }
  }
}

bool Scheduler::shouldStartProgramNow(const Program& program, const Time& now) const {
  if (!program.isValid() || program.getSectorCount() == 0) return false;
  if (!_rtc.isValid(now)) return false;

  const uint8_t dayBit = RTCManager::dayMaskBitFromDate(now.yr, now.mon, now.date);
  if (dayBit > 6 || (program.getDays() & (1U << dayBit)) == 0) return false;

  uint8_t programHour = 0, programMinute = 0;
  if (!RTCManager::parseHourMinute(program.getStartTime(), programHour, programMinute)) return false;

  return programHour == now.hr && programMinute == now.min;
}

void Scheduler::rememberMinute(const Time& now) {
  _lastYear   = now.yr;
  _lastMonth  = now.mon;
  _lastDay    = now.date;
  _lastHour   = now.hr;
  _lastMinute = now.min;
}

bool Scheduler::isSameMinute(const Time& now) const {
  return now.yr   == _lastYear   &&
         now.mon  == _lastMonth  &&
         now.date == _lastDay    &&
         now.hr   == _lastHour   &&
         now.min  == _lastMinute;
}
