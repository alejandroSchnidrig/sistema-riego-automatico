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
    _pumpFlow(Config::CAUDAL_BOMBA_DEFAULT),
    _initMode(mode),
    _state(SystemState::IDLE),
    _activeProgramId(0),
    _runningProgramIndex(-1),
    _activeCount(0),
    _pendingCount(0),
    _queueCount(0),
    _completedMask(0),
    _manualSectorMask(0),
    _blinkPhase(false),
    _lastStepMs(0)
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
// Programas semilla (demo, se reemplaza por seed árbol en Fase E5)
// ============================================================

void IrrigationSystem::seedDefaultPrograms() {
  clearPrograms();
  for (uint8_t i = 0; i < Config::MAX_PROGRAMAS; i++) {
    _programs[i] = Program();
  }
  _pumpFlow = Config::CAUDAL_BOMBA_DEFAULT; // los demos están diseñados para 20 L/min

  // Nodos: {sectorId, irrigationTime, delay, parentSectorId, flow} (padre 0 = raíz)

  // Programa 1: árbol S1→{S2,S3}; S2→S4; S3→S5. Cíclico, lun-vie 07:00.
  // S1 alimenta S2+S3 en paralelo (caudal S1 = 6+6 = 12); cada hoja iguala a su padre.
  _programs[0].setValid(true);
  _programs[0].setId(1);
  _programs[0].setStartTime("07:00");
  _programs[0].setDays(62);          // lun-vie
  _programs[0].setCyclic(true);
  _programs[0].addNode({1, 15, 0, 0, 12});
  _programs[0].addNode({2, 12, 3, 1, 6});
  _programs[0].addNode({3, 18, 3, 1, 6});
  _programs[0].addNode({4, 10, 2, 2, 6});
  _programs[0].addNode({5, 12, 2, 3, 6});

  // Programa 2: dos raíces (S4, S6 = 7 L/min c/u → 14 ≤ 20) con un hijo cada una.
  _programs[1].setValid(true);
  _programs[1].setId(2);
  _programs[1].setStartTime("19:30");
  _programs[1].setDays(96);          // sáb-dom
  _programs[1].setCyclic(false);
  _programs[1].addNode({4, 20, 0, 0, 7});
  _programs[1].addNode({6, 20, 0, 0, 7});
  _programs[1].addNode({7, 15, 3, 4, 6});
  _programs[1].addNode({8, 18, 3, 6, 6});

  // Programa 3: tres raíces en paralelo (3 × 6 = 18 ≤ 20), sin cola.
  _programs[2].setValid(true);
  _programs[2].setId(3);
  _programs[2].setStartTime("12:00");
  _programs[2].setDays(127);         // todos los días
  _programs[2].setCyclic(false);
  _programs[2].addNode({1, 10, 0, 0, 6});
  _programs[2].addNode({2, 10, 0, 0, 6});
  _programs[2].addNode({3, 10, 0, 0, 6});

  _nextProgramId = 4;
}

// ============================================================
// Motor de ejecución — modelo árbol + caudal
// ============================================================

void IrrigationSystem::tick() {
  if (_state != SystemState::RUNNING || _runningProgramIndex < 0) return;

  unsigned long now = hal_millis();
  // Cadencia de 1 s sin bloquear: solo se procesa un paso cuando pasó el intervalo.
  if (now - _lastStepMs < Config::INTERVALO_SCHEDULER_MS) return;
  _lastStepMs += Config::INTERVALO_SCHEDULER_MS;

  stepOneSecond();
}

void IrrigationSystem::stepOneSecond() {
  // 1) Pendientes: descontar retardo; los que llegan a 0 pasan a activos.
  uint8_t finished[Config::NUM_SECTORES];
  uint8_t finishedCount = 0;

  PendingEntry stillPending[Config::NUM_SECTORES];
  uint8_t      stillPendingCount = 0;
  for (uint8_t i = 0; i < _pendingCount; i++) {
    PendingEntry e = _pending[i];
    if (e.delaySec <= 1) {
      // El caudal ya estaba comprometido como pendiente; pasa a activo.
      if (!activeContains(e.sectorId)) {
        addActive(e.sectorId, e.irrigationTime, e.flow);
      }
    } else {
      e.delaySec = (uint16_t)(e.delaySec - 1);
      stillPending[stillPendingCount++] = e;
    }
  }
  _pendingCount = stillPendingCount;
  for (uint8_t i = 0; i < stillPendingCount; i++) _pending[i] = stillPending[i];

  // 2) Activos: descontar tiempo; los que llegan a 0 terminan.
  ActiveEntry stillActive[Config::NUM_SECTORES];
  uint8_t     stillActiveCount = 0;
  for (uint8_t i = 0; i < _activeCount; i++) {
    ActiveEntry a = _active[i];
    if (a.remainingTimeSec <= 1) {
      finished[finishedCount++] = a.sectorId;
    } else {
      a.remainingTimeSec -= 1;
      stillActive[stillActiveCount++] = a;
    }
  }
  _activeCount = stillActiveCount;
  for (uint8_t i = 0; i < stillActiveCount; i++) _active[i] = stillActive[i];

  // 3) Registrar terminados.
  for (uint8_t i = 0; i < finishedCount; i++) {
    _completedMask |= sectorIdToMask(finished[i]);
  }

  // 4) Drenar la cola con el caudal liberado (prioridad FIFO sobre hijos nuevos).
  drainQueue();

  // 5) Encolar los hijos de cada sector que terminó.
  for (uint8_t i = 0; i < finishedCount; i++) {
    enqueueChildren(finished[i]);
  }

  // 6) Fin de programa: todo vacío → cíclico reinicia raíces / si no, fin.
  if (_activeCount == 0 && _pendingCount == 0 && _queueCount == 0) {
    if (_programs[_runningProgramIndex].isCyclic()) {
      startRoots(_runningProgramIndex);
    } else {
      stopRuntime(SystemState::IDLE);
      return;
    }
  }

  // 7) Alternar fase de titileo y refrescar salidas (válvulas, cañería, bomba).
  _blinkPhase = !_blinkPhase;
  applyOutputsFromState();
}

void IrrigationSystem::startRoots(int programIndex) {
  clearRuntimeLists();
  _completedMask = 0;

  const Program& p = _programs[programIndex];
  for (uint8_t s = 0; s < p.getSectorCount(); s++) {
    const ProgramNode& n = p.getNode(s);
    if (n.parentSectorId == 0) {
      // Las raíces arrancan sin retardo.
      tryActivateSector(n.sectorId, n.irrigationTime, n.flow, 0);
    }
  }
}

uint16_t IrrigationSystem::committedFlow() const {
  uint32_t total = 0;
  for (uint8_t i = 0; i < _activeCount; i++)  total += _active[i].flow;
  for (uint8_t i = 0; i < _pendingCount; i++) total += _pending[i].flow;
  return (uint16_t)total;
}

void IrrigationSystem::tryActivateSector(uint8_t sectorId, uint32_t irrigationTime,
                                         uint16_t flow, uint16_t delaySec) {
  if ((uint32_t)committedFlow() + flow <= _pumpFlow) {
    if (delaySec > 0) {
      addPending(sectorId, delaySec, flow, irrigationTime);
    } else {
      addActive(sectorId, irrigationTime, flow);
    }
  } else {
    addQueued(sectorId, irrigationTime, delaySec, flow);
  }
}

void IrrigationSystem::drainQueue() {
  // FIFO: si el primero de la cola no entra, se detiene el drenado.
  while (_queueCount > 0 &&
         (uint32_t)committedFlow() + _queue[0].flow <= _pumpFlow) {
    QueuedEntry e = _queue[0];
    for (uint8_t i = 1; i < _queueCount; i++) _queue[i - 1] = _queue[i];
    _queueCount--;

    if (e.delaySec > 0) {
      addPending(e.sectorId, e.delaySec, e.flow, e.irrigationTime);
    } else {
      addActive(e.sectorId, e.irrigationTime, e.flow);
    }
  }
}

void IrrigationSystem::enqueueChildren(uint8_t parentSectorId) {
  const Program& p = _programs[_runningProgramIndex];
  for (uint8_t s = 0; s < p.getSectorCount(); s++) {
    const ProgramNode& n = p.getNode(s);
    if (n.parentSectorId == parentSectorId) {
      tryActivateSector(n.sectorId, n.irrigationTime, n.flow, n.delay);
    }
  }
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
  _state               = SystemState::RUNNING;
  _activeProgramId     = p.getId();
  _runningProgramIndex = index;
  _blinkPhase          = true;
  startRoots(index);
  _lastStepMs          = hal_millis();

  // Si no quedó nada por regar (p. ej. sin raíces), no hay nada que ejecutar.
  if (_activeCount == 0 && _pendingCount == 0 && _queueCount == 0) {
    stopRuntime(SystemState::IDLE);
    return false;
  }

  applyOutputsFromState();
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
    applyOutputsFromState(); // desactivar solo refresca salidas, no toca el programa
  }
}

// ============================================================
// Gestión de programas
// ============================================================

uint16_t IrrigationSystem::saveProgram(Program& p) {
  if (!validateProgram(p)) return 0;

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

uint16_t IrrigationSystem::getPumpFlow() const { return _pumpFlow; }

void IrrigationSystem::setPumpFlow(uint16_t flow) {
  if (flow > 0) _pumpFlow = flow;
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
  // Solo cuentan las válvulas abiertas en firme (manual + sectores regando).
  return _manualSectorMask | computeActiveMask();
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
  SystemStateSnapshot snap;
  snap.stateName       = stateToString(_state);
  snap.activeProgramId = _activeProgramId;

  snap.activeCount = _activeCount;
  for (uint8_t i = 0; i < _activeCount; i++)   snap.active[i]  = _active[i];
  snap.pendingCount = _pendingCount;
  for (uint8_t i = 0; i < _pendingCount; i++)  snap.pending[i] = _pending[i];
  snap.queuedCount = _queueCount;
  for (uint8_t i = 0; i < _queueCount; i++)    snap.queued[i]  = _queue[i];
  snap.completedMask = _completedMask;

  snap.pumpOn              = _pump.isOn();
  snap.manualActive        = _manualSectorMask != 0;
  snap.manualSectorMask    = _manualSectorMask;
  snap.firstManualSectorId = firstSectorFromMask(_manualSectorMask);
  snap.pumpFlow            = _pumpFlow;

  // Resumen escalar (compatibilidad /estado lineal hasta E3).
  const uint16_t mask   = _manualSectorMask | computeActiveMask();
  snap.activeSectorMask = mask;
  snap.activeSectorId   = firstSectorFromMask(mask);
  snap.remainingTimeSec = (_activeCount > 0) ? _active[0].remainingTimeSec : 0;
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
// Salidas: válvulas, cañería (titileo) y bomba
// ============================================================

uint16_t IrrigationSystem::computeActiveMask() const {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < _activeCount; i++) {
    mask |= sectorIdToMask(_active[i].sectorId);
  }
  return mask;
}

uint16_t IrrigationSystem::computeFeedingMask() const {
  if (_state != SystemState::RUNNING || _runningProgramIndex < 0) return 0;

  const Program& p = _programs[_runningProgramIndex];
  uint16_t feeding = 0;
  // Para cada sector activo, abrir la cadena de ancestros (cañería).
  for (uint8_t i = 0; i < _activeCount; i++) {
    const ProgramNode* node = p.findNodeBySectorId(_active[i].sectorId);
    while (node != nullptr && node->parentSectorId != 0) {
      feeding |= sectorIdToMask(node->parentSectorId);
      node = p.findNodeBySectorId(node->parentSectorId);
    }
  }
  return feeding;
}

void IrrigationSystem::applyOutputsFromState() {
  const uint16_t activeMask  = computeActiveMask();
  const uint16_t solidMask   = _manualSectorMask | activeMask;
  const uint16_t feedingMask = computeFeedingMask();
  const uint16_t blinkMask   = feedingMask & (uint16_t)~solidMask;

  setSectorHardware(solidMask, blinkMask);

  if ((activeMask | _manualSectorMask) != 0) _pump.on(); else _pump.off();
}

void IrrigationSystem::setSectorHardware(uint16_t solidMask, uint16_t blinkMask) {
  for (uint8_t i = 0; i < Config::NUM_SECTORES; i++) {
    const uint16_t bit = sectorIdToMask(i + 1);
    // Sólido → siempre abierto. Cañería → abierto sólo en la fase de titileo.
    const bool open = (solidMask & bit) != 0 ||
                      ((blinkMask & bit) != 0 && _blinkPhase);
    if (open) _sectors[i].activate();
    else      _sectors[i].deactivate();
  }
}

// ============================================================
// Helpers privados
// ============================================================

void IrrigationSystem::stopRuntime(SystemState newState) {
  _state               = newState;
  _activeProgramId     = 0;
  _runningProgramIndex = -1;
  clearRuntimeLists();
  _completedMask       = 0;
  _blinkPhase          = false;
  _lastStepMs          = hal_millis();
  applyOutputsFromState();
}

void IrrigationSystem::clearRuntimeLists() {
  _activeCount  = 0;
  _pendingCount = 0;
  _queueCount   = 0;
}

bool IrrigationSystem::activeContains(uint8_t sectorId) const {
  for (uint8_t i = 0; i < _activeCount; i++) {
    if (_active[i].sectorId == sectorId) return true;
  }
  return false;
}

void IrrigationSystem::addActive(uint8_t sectorId, uint32_t remaining, uint16_t flow) {
  if (_activeCount >= Config::NUM_SECTORES) return;
  _active[_activeCount].sectorId         = sectorId;
  _active[_activeCount].remainingTimeSec = remaining;
  _active[_activeCount].flow             = flow;
  _activeCount++;
}

void IrrigationSystem::addPending(uint8_t sectorId, uint16_t delaySec, uint16_t flow,
                                  uint32_t irrigationTime) {
  if (_pendingCount >= Config::NUM_SECTORES) return;
  _pending[_pendingCount].sectorId       = sectorId;
  _pending[_pendingCount].delaySec       = delaySec;
  _pending[_pendingCount].flow           = flow;
  _pending[_pendingCount].irrigationTime = irrigationTime;
  _pendingCount++;
}

void IrrigationSystem::addQueued(uint8_t sectorId, uint32_t irrigationTime,
                                 uint16_t delaySec, uint16_t flow) {
  if (_queueCount >= Config::NUM_SECTORES) return;
  _queue[_queueCount].sectorId       = sectorId;
  _queue[_queueCount].irrigationTime = irrigationTime;
  _queue[_queueCount].delaySec       = delaySec;
  _queue[_queueCount].flow           = flow;
  _queueCount++;
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

// ============================================================
// Validación de programas (modelo árbol + caudal)
// ============================================================

bool IrrigationSystem::validateProgram(const Program& p) const {
  const uint8_t count = p.getSectorCount();
  if (count == 0) return false;

  // Debe haber al menos una raíz.
  if (p.getRootCount() < 1) return false;

  for (uint8_t i = 0; i < count; i++) {
    const ProgramNode& n = p.getNode(i);

    // Ningún sector puede pedir más caudal que la bomba.
    if (n.flow > _pumpFlow) return false;

    // El padre (si lo hay) debe existir y no ser uno mismo.
    if (n.parentSectorId != 0) {
      if (n.parentSectorId == n.sectorId) return false;
      if (!p.hasNode(n.parentSectorId))   return false;
    }
  }

  // Sin ciclos: la cadena de padres de cada nodo debe llegar a una raíz.
  for (uint8_t i = 0; i < count; i++) {
    const ProgramNode* node = &p.getNode(i);
    uint8_t hops = 0;
    while (node != nullptr && node->parentSectorId != 0) {
      node = p.findNodeBySectorId(node->parentSectorId);
      if (++hops > count) return false; // ciclo detectado
    }
  }

  // Σ caudal de las raíces ≤ caudal de la bomba.
  uint32_t rootSum = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (p.getNode(i).parentSectorId == 0) rootSum += p.getNode(i).flow;
  }
  if (rootSum > _pumpFlow) return false;

  // Σ caudal de los hijos ≤ caudal del padre (la cañería no agrega caudal).
  for (uint8_t i = 0; i < count; i++) {
    const ProgramNode& parent = p.getNode(i);
    uint32_t childSum = 0;
    for (uint8_t j = 0; j < count; j++) {
      if (p.getNode(j).parentSectorId == parent.sectorId) {
        childSum += p.getNode(j).flow;
      }
    }
    if (childSum > parent.flow) return false;
  }

  return true;
}
