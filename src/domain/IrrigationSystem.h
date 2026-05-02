#pragma once
#include <stdint.h>
#include "Sector.h"
#include "Pump.h"
#include "Program.h"
#include "../config/Config.h"

enum class SystemState { IDLE, RUNNING, MANUAL_STOP };

// DTO inmutable que ApiHandler serializa a JSON; no contiene referencias al estado interno.
struct SystemStateSnapshot {
  const char* stateName;
  uint16_t    activeProgramId;
  uint8_t     activeSectorId;
  uint16_t    activeSectorMask;    // máscara combinada: manual | programático
  uint32_t    remainingTimeSec;
  bool        pumpOn;
  bool        manualActive;
  uint16_t    manualSectorMask;
  uint8_t     firstManualSectorId; // primer bit encendido de manualSectorMask (para UI)
};

class IrrigationSystem {
public:
  IrrigationSystem();

  void begin();
  void seedDefaultPrograms();
  void tick();

  bool startProgramById(uint16_t id);
  void stop();
  void clearManualOverrides();
  void setManualSector(uint8_t sectorId, bool on);

  // Program management
  uint16_t saveProgram(Program& p);
  bool deleteProgram(uint16_t id);
  const Program& programAt(uint8_t index) const;

  // State queries
  bool isRunning() const;
  bool isManualControlActive() const;
  bool isSectorActive(uint8_t sectorId) const;
  uint16_t getOutputSectorMask() const;
  uint16_t getActiveProgramId() const;

  // Hardware queries (for serial debug and setup)
  uint8_t getPumpPin() const;
  bool isPumpOn() const;
  uint8_t getSectorPin(uint8_t sectorId) const;

  SystemStateSnapshot getStateSnapshot() const;
  static const char* stateToString(SystemState state);

private:
  Sector   _sectors[Config::NUM_SECTORES];
  Pump     _pump;
  Program  _programs[Config::MAX_PROGRAMAS];
  uint16_t _nextProgramId;

  SystemState   _state;
  uint16_t      _activeProgramId;
  uint8_t       _activeSectorId;
  uint32_t      _remainingTimeSec;
  uint16_t      _manualSectorMask;

  int           _runningProgramIndex;
  int           _runningStepIndex;
  bool          _waitingBetweenSectors;
  unsigned long _stepStartMs;
  unsigned long _delayStartMs;

  void stopRuntime(SystemState newState);
  void startStep(int programIndex, int stepIndex);
  void applyOutputsFromState();
  void setSectorHardware(uint16_t sectorMask);

  static uint16_t sectorIdToMask(uint8_t sectorId);
  static uint8_t firstSectorFromMask(uint16_t mask);

  int findProgramIndexById(uint16_t id) const;
  int findFreeProgramSlot() const;
};
