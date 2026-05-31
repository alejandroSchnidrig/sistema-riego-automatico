#include "IrrigationSystem.h"
#include "../core/HAL.h"

IrrigationSystem::IrrigationSystem(InitMode mode)
  : _sectors{
      Sector(1, Config::PINES_SECTORES[0], Config::SECTORES_ACTIVOS_BAJO[0]),
      Sector(2, Config::PINES_SECTORES[1], Config::SECTORES_ACTIVOS_BAJO[1]),
      Sector(3, Config::PINES_SECTORES[2], Config::SECTORES_ACTIVOS_BAJO[2]),
      Sector(4, Config::PINES_SECTORES[3], Config::SECTORES_ACTIVOS_BAJO[3]),
      Sector(5, Config::PINES_SECTORES[4], Config::SECTORES_ACTIVOS_BAJO[4]),
      Sector(6, Config::PINES_SECTORES[5], Config::SECTORES_ACTIVOS_BAJO[5]),
      Sector(7, Config::PINES_SECTORES[6], Config::SECTORES_ACTIVOS_BAJO[6]),
      Sector(8, Config::PINES_SECTORES[7], Config::SECTORES_ACTIVOS_BAJO[7])
    },
    _pump(Config::PIN_BOMBA, Config::BOMBA_ACTIVA_BAJO),
    _nextProgramId(1),
    _initMode(mode),
    _state(SystemState::IDLE),
    _activeProgramId(0),
    _activeSectorId(0),
    _remainingTimeSec(0),
    _manualSectorMask(0),
    _runningProgramIndex(-1),
    _runningStepIndex(-1),
    _waitingBetweenSectors(false),
    _stepStartMs(0),
    _delayStartMs(0)
{
  clearPrograms();

  if (_initMode == InitMode::WITH_SEED) {
    seedDefaultPrograms();
  }
}

// ============================================================
// Inicialización de hardware
// ============================================================

void IrrigationSystem::begin() {
  for (uint8_t i = 0; i < Config::NUM_SECTORES; i++) {
    _sectors[i].begin();
  }
  _pump.begin();
  stopRuntime(SystemState::IDLE);
}

// ============================================================
// Programas semilla (demo, se reemplaza por LittleFS en Fase F)
// ============================================================

void IrrigationSystem::seedDefaultPrograms() {
  clearPrograms();
  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    _programs[i] = Program();
  }

  // Programa 1: lunes a viernes a las 07:00, cíclico, 4 sectores
  // Nodos: {sectorId, irrigationTime, delay, parentSectorId, flow}
  _programs[0].setValid(true);
  _programs[0].setId(1);
  _programs[0].setStartTime("07:00");
  _programs[0].setDays(0b0111110);  // lun=bit0 … vie=bit4
  _programs[0].setCyclic(true);
  _programs[0].addNode({1,  60, 0, 0, 10});
  _programs[0].addNode({2,  90, 5, 0, 10});
  _programs[0].addNode({3, 120, 5, 0, 10});
  _programs[0].addNode({5,  45, 5, 0, 10});

  // Programa 2: sábado y domingo a las 19:30, no cíclico, 3 sectores
  _programs[1].setValid(true);
  _programs[1].setId(2);
  _programs[1].setStartTime("19:30");
  _programs[1].setDays(0b1100000);  // sáb=bit5, dom=bit6
  _programs[1].setCyclic(false);
  _programs[1].addNode({4, 180,  0, 0, 10});
  _programs[1].addNode({6, 180, 10, 0, 10});
  _programs[1].addNode({8,  90, 10, 0, 10});

   _nextProgramId = 3;
}

// ============================================================
// Motor de ejecución (tick = updateRuntime)
// ============================================================

void IrrigationSystem::tick() {
  if (_state != SystemState::RUNNING || _runningProgramIndex < 0) return;

  Program& p        = _programs[_runningProgramIndex];
  unsigned long now = hal_millis();

  // Fase 1: pausa inter-sector — espera el retardo del nodo siguiente antes de iniciarlo.
  if (_waitingBetweenSectors) {
    _remainingTimeSec = 0;
    int nextStep      = _runningStepIndex + 1;
    bool wraps        = (nextStep >= p.getSectorCount());
    uint8_t targetIdx = wraps ? 0 : (uint8_t)nextStep;
    uint16_t delaySec = (p.getSectorCount() > 0) ? p.getNode(targetIdx).delay : 0;
    if (now - _delayStartMs >= (unsigned long)delaySec * 1000UL) {
      if (wraps) {
        p.isCyclic() ? startStep(_runningProgramIndex, 0) : stopRuntime(SystemState::IDLE);
      } else {
        startStep(_runningProgramIndex, nextStep);
      }
    }
    return;
  }

  // Fase 2: ejecución del paso actual — riega el sector durante irrigationTime segundos.
  uint32_t      stepDurationSec = p.getNode(_runningStepIndex).irrigationTime;
  unsigned long elapsedMs       = now - _stepStartMs;
  uint32_t      elapsedSec      = (uint32_t)(elapsedMs / 1000UL);

  if (elapsedSec >= stepDurationSec) {
    _activeSectorId = 0;
    applyOutputsFromState();

    int nextStep = _runningStepIndex + 1;

    if (nextStep >= p.getSectorCount()) {
      if (p.isCyclic()) {
        if (p.getNode(0).delay > 0) {
          _waitingBetweenSectors = true;
          _delayStartMs          = now;
        } else {
          startStep(_runningProgramIndex, 0);
        }
      } else {
        stopRuntime(SystemState::IDLE);
      }
    } else {
      if (p.getNode(nextStep).delay > 0) {
        _waitingBetweenSectors = true;
        _delayStartMs          = now;
        _remainingTimeSec      = 0;
      } else {
        startStep(_runningProgramIndex, nextStep);
      }
    }
    return;
  }

  _remainingTimeSec = stepDurationSec - elapsedSec;
}

// ============================================================
// API pública de control
// ============================================================

bool IrrigationSystem::startProgramById(uint16_t id) {
  int index = findProgramIndexById(id);
  if (index < 0) return false;

  Program& p = _programs[index];
  if (p.getSectorCount() == 0) return false;

  clearManualOverrides();
  startStep(index, 0);
  return true;
}

void IrrigationSystem::stop() {
  clearManualOverrides();
  stopRuntime(SystemState::MANUAL_STOP);
}

void IrrigationSystem::clearManualOverrides() {
  _manualSectorMask = 0;
}

void IrrigationSystem::setManualSector(uint8_t sectorId, bool on) {
  if (on) {
    _manualSectorMask |= sectorIdToMask(sectorId);
    stopRuntime(SystemState::IDLE); // activar manual detiene el programa en curso
  } else {
    _manualSectorMask &= (uint16_t)~sectorIdToMask(sectorId);
    applyOutputsFromState(); // desactivar solo refresca salidas, no toca el estado del programa
  }
}

// ============================================================
// Gestión de programas
// ============================================================

uint16_t IrrigationSystem::saveProgram(Program& p) {
  int slot = (p.getId() > 0) ? findProgramIndexById(p.getId()) : -1;
  if (slot < 0) {
    slot = findFreeProgramSlot();
    if (slot < 0) return 0;
    p.setId(_nextProgramId++);
  }
  p.setValid(true);
  _programs[slot] = p;
  return p.getId();
}

bool IrrigationSystem::deleteProgram(uint16_t id) {
  int idx = findProgramIndexById(id);
  if (idx < 0) return false;
  if (_activeProgramId == id) stopRuntime(SystemState::IDLE);
  _programs[idx].setValid(false);
  return true;
}

const Program& IrrigationSystem::programAt(uint8_t index) const {
  return _programs[index];
}

// ============================================================
// Consultas de estado
// ============================================================

bool IrrigationSystem::isRunning() const {
  return _state == SystemState::RUNNING;
}

bool IrrigationSystem::isManualControlActive() const {
  return _manualSectorMask != 0;
}

bool IrrigationSystem::isSectorActive(uint8_t sectorId) const {
  return (getOutputSectorMask() & sectorIdToMask(sectorId)) != 0;
}

uint16_t IrrigationSystem::getOutputSectorMask() const {
  // OR permite que manual y programático coexistan sin pisarse
  return _manualSectorMask | sectorIdToMask(_activeSectorId);
}

uint16_t IrrigationSystem::getActiveProgramId() const {
  return _activeProgramId;
}

// ============================================================
// Consultas de hardware (serial debug y setup)
// ============================================================

uint8_t IrrigationSystem::getPumpPin() const {
  return _pump.getPin();
}

bool IrrigationSystem::isPumpOn() const {
  return _pump.isOn();
}

uint8_t IrrigationSystem::getSectorPin(uint8_t sectorId) const {
  if (sectorId < 1 || sectorId > Config::NUM_SECTORES) return 0;
  return _sectors[sectorId - 1].getPin();
}

// ============================================================
// Snapshot de estado para serialización
// ============================================================

SystemStateSnapshot IrrigationSystem::getStateSnapshot() const {
  const uint16_t mask = getOutputSectorMask();
  SystemStateSnapshot snap;
  snap.stateName           = stateToString(_state);
  snap.activeProgramId     = _activeProgramId;
  snap.activeSectorId      = firstSectorFromMask(mask);
  snap.activeSectorMask    = mask;
  snap.remainingTimeSec    = _remainingTimeSec;
  snap.pumpOn              = _pump.isOn();
  snap.manualActive        = _manualSectorMask != 0;
  snap.manualSectorMask    = _manualSectorMask;
  snap.firstManualSectorId = firstSectorFromMask(_manualSectorMask);
  return snap;
}

const char* IrrigationSystem::stateToString(SystemState state) {
  switch (state) {
    case SystemState::RUNNING:     return "RUNNING";
    case SystemState::MANUAL_STOP: return "MANUAL_STOP";
    case SystemState::IDLE:
    default:                       return "IDLE";
  }
}

// ============================================================
// Helpers privados
// ============================================================

void IrrigationSystem::stopRuntime(SystemState newState) {
  _state                = newState;
  _activeProgramId      = 0;
  _activeSectorId       = 0;
  _remainingTimeSec     = 0;
  _runningProgramIndex  = -1;
  _runningStepIndex     = -1;
  _waitingBetweenSectors = false;
  _stepStartMs          = 0;
  _delayStartMs         = 0;
  applyOutputsFromState();
}

void IrrigationSystem::startStep(int programIndex, int stepIndex) {
  if (programIndex < 0 || !_programs[programIndex].isValid()) {
    stopRuntime(SystemState::IDLE);
    return;
  }

  Program& p = _programs[programIndex];
  if (stepIndex < 0 || stepIndex >= p.getSectorCount()) {
    stopRuntime(SystemState::IDLE);
    return;
  }

  _runningProgramIndex   = programIndex;
  _runningStepIndex      = stepIndex;
  _waitingBetweenSectors = false;
  _state                 = SystemState::RUNNING;
  _activeProgramId       = p.getId();
  _activeSectorId        = p.getNode(stepIndex).sectorId;
  _remainingTimeSec      = p.getNode(stepIndex).irrigationTime;
  _stepStartMs           = hal_millis();

  applyOutputsFromState();
}

void IrrigationSystem::applyOutputsFromState() {
  const uint16_t mask = getOutputSectorMask();
  setSectorHardware(mask);
  if (mask != 0) _pump.on(); else _pump.off();
}

void IrrigationSystem::setSectorHardware(uint16_t sectorMask) {
  for (uint8_t i = 0; i < Config::NUM_SECTORES; i++) {
    if ((sectorMask & sectorIdToMask(i + 1)) != 0) {
      _sectors[i].activate();
    } else {
      _sectors[i].deactivate();
    }
  }
}

uint16_t IrrigationSystem::sectorIdToMask(uint8_t sectorId) {
  if (sectorId < 1 || sectorId > Config::NUM_SECTORES) return 0;
  return (uint16_t)1U << (sectorId - 1); // sector 1 → bit 0, sector 8 → bit 7
}

uint8_t IrrigationSystem::firstSectorFromMask(uint16_t mask) {
  for (uint8_t i = 1; i <= Config::NUM_SECTORES; i++) {
    if ((mask & sectorIdToMask(i)) != 0) return i;
  }
  return 0;
}

int IrrigationSystem::findProgramIndexById(uint16_t id) const {
  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    if (_programs[i].isValid() && _programs[i].getId() == id) return i;
  }
  return -1;
}

int IrrigationSystem::findFreeProgramSlot() const {
  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    if (!_programs[i].isValid()) return i;
  }
  return -1;
}

void IrrigationSystem::clearPrograms() {
  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    _programs[i] = Program();
  }
  _nextProgramId = 1;
}
