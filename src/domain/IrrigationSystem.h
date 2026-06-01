#pragma once
#include <stdint.h>
#include "Sector.h"
#include "Pump.h"
#include "Program.h"
#include "../config/Config.h"

enum class SystemState { IDLE, RUNNING, MANUAL_STOP };

// ------------------------------------------------------------
// Entradas de las listas de runtime del motor árbol + caudal.
// ------------------------------------------------------------

// Sector regando: válvula abierta, descontando tiempo.
struct ActiveEntry {
  uint8_t  sectorId;
  uint32_t remainingTimeSec;
  uint16_t flow;
};

// Sector cuyo padre terminó: espera su retardo antes de regar.
// El caudal ya está comprometido, pero la válvula sigue cerrada.
struct PendingEntry {
  uint8_t  sectorId;
  uint16_t delaySec;        // segundos restantes de espera
  uint16_t flow;
  uint32_t irrigationTime;  // duración a aplicar al activarse
};

// Sector sin caudal libre: espera en cola FIFO.
struct QueuedEntry {
  uint8_t  sectorId;
  uint32_t irrigationTime;
  uint16_t delaySec;
  uint16_t flow;
};

// DTO inmutable que ApiHandler serializa a JSON; no contiene referencias al estado interno.
struct SystemStateSnapshot {
  const char* stateName;
  uint16_t    activeProgramId;

  // Listas del modelo árbol + caudal (se serializan en /estado, Fase E3)
  uint8_t      activeCount;
  ActiveEntry  active[Config::NUM_SECTORES];
  uint8_t      pendingCount;
  PendingEntry pending[Config::NUM_SECTORES];
  uint8_t      queuedCount;
  QueuedEntry  queued[Config::NUM_SECTORES];
  uint16_t     completedMask;   // bit N-1 encendido = sector N completado

  bool        pumpOn;
  bool        manualActive;
  uint16_t    manualSectorMask;
  uint8_t     firstManualSectorId; // primer bit encendido de manualSectorMask (para UI)
  uint16_t    pumpFlow;            // caudal máximo de la bomba (L/min)

  // Resumen escalar (usado por el debug serial de main.cpp; /estado usa las listas)
  uint8_t     activeSectorId;
  uint16_t    activeSectorMask;    // máscara combinada: manual | programático
  uint32_t    remainingTimeSec;
};

class IrrigationSystem {
public:
  enum class InitMode {
    EMPTY,
    WITH_SEED
  };

  explicit IrrigationSystem(InitMode mode = InitMode::WITH_SEED);

  void begin();
  void clearPrograms();
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

  // Caudal de la bomba (límite global de concurrencia)
  uint16_t getPumpFlow() const;
  void setPumpFlow(uint16_t flow);

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
  uint16_t _pumpFlow;

  InitMode _initMode;

  SystemState   _state;
  uint16_t      _activeProgramId;
  int           _runningProgramIndex;

  // Listas de runtime del motor árbol + caudal
  ActiveEntry  _active[Config::NUM_SECTORES];
  uint8_t      _activeCount;
  PendingEntry _pending[Config::NUM_SECTORES];
  uint8_t      _pendingCount;
  QueuedEntry  _queue[Config::NUM_SECTORES];   // FIFO
  uint8_t      _queueCount;
  uint16_t     _completedMask;

  uint16_t      _manualSectorMask;
  unsigned long _lastStepMs;          // marca del último paso de 1 s procesado

  // Motor de ejecución
  void stepOneSecond();
  void startRoots(int programIndex);
  uint16_t committedFlow() const;
  void tryActivateSector(uint8_t sectorId, uint32_t irrigationTime,
                         uint16_t flow, uint16_t delaySec);
  void drainQueue();
  void enqueueChildren(uint8_t parentSectorId);

  bool activeContains(uint8_t sectorId) const;
  void addActive(uint8_t sectorId, uint32_t remaining, uint16_t flow);
  void addPending(uint8_t sectorId, uint16_t delaySec, uint16_t flow,
                  uint32_t irrigationTime);
  void addQueued(uint8_t sectorId, uint32_t irrigationTime,
                 uint16_t delaySec, uint16_t flow);
  void clearRuntimeLists();

  // Salidas (válvulas, bomba). La cañería abre la válvula fija (no titila).
  uint16_t computeActiveMask() const;
  uint16_t computeFeedingMask() const;
  void applyOutputsFromState();
  void setSectorHardware(uint16_t openMask);

  void stopRuntime(SystemState newState);

  static uint16_t sectorIdToMask(uint8_t sectorId);
  static uint8_t firstSectorFromMask(uint16_t mask);

  int findProgramIndexById(uint16_t id) const;
  int findFreeProgramSlot() const;
  bool validateProgram(const Program& p) const;
};
