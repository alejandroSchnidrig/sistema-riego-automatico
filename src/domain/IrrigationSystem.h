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

  // Listas del modelo árbol + caudal (se serializan en /estado)
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

  // Inicializa el hardware (válvulas y bomba) y deja el sistema en IDLE. Llamar desde setup().
  void begin();
  // Vacía todos los slots de programas y resetea el contador de IDs.
  void clearPrograms();
  // Carga los programas de demostración (se usan si no hay config guardada).
  void seedDefaultPrograms();
  // Avanza el motor un paso si pasó el intervalo de 1 s (no bloquea).
  // nowMinutes: minutos desde medianoche (hora actual del RTC). -1 = sin hora
  // (en tests sin RTC); en ese caso un programa cíclico reinicia siempre.
  void tick(int nowMinutes = -1);

  // Inicia el programa con ese id. Devuelve false si no existe o no tiene nada que regar.
  bool startProgramById(uint16_t id);
  // Parada manual inmediata: corta todo y pasa a MANUAL_STOP.
  void stop();
  // Apaga todos los sectores encendidos manualmente.
  void clearManualOverrides();
  // Enciende/apaga un sector en modo manual. Al encender devuelve false si no
  // hay caudal libre en la bomba (Σ caudal manual + el nuevo > caudalBomba).
  // Apagar siempre tiene éxito.
  bool setManualSector(uint8_t sectorId, bool on);

  // Gestión de programas
  // Valida y guarda (o actualiza) un programa. Devuelve el id asignado, o 0 si es inválido o no hay espacio.
  uint16_t saveProgram(Program& p);
  // Borra (marca como inválido) el programa con ese id. Devuelve false si no existe.
  bool deleteProgram(uint16_t id);
  // Devuelve el programa en el slot 'index' (esté ocupado o no).
  const Program& programAt(uint8_t index) const;

  // Caudal de la bomba (límite global de concurrencia)
  uint16_t getPumpFlow() const;        // caudal máximo de la bomba (L/min)
  void setPumpFlow(uint16_t flow);     // fija el caudal de la bomba (ignora 0)

  // Caudal asumido por sector (1..8) cuando se enciende en modo manual (L/min)
  uint16_t getManualSectorFlow(uint8_t sectorId) const;       // caudal manual del sector
  void setManualSectorFlow(uint8_t sectorId, uint16_t flow);  // fija el caudal manual del sector

  // Consultas de estado
  bool isRunning() const;                          // ¿hay un programa en ejecución?
  bool isManualControlActive() const;              // ¿hay algún sector en manual?
  bool isSectorActive(uint8_t sectorId) const;     // ¿la válvula del sector está abierta en firme?
  uint16_t getOutputSectorMask() const;            // máscara de válvulas abiertas en firme (manual | regando)
  uint16_t getActiveProgramId() const;             // id del programa en curso (0 si ninguno)

  // Consultas de hardware (para debug serial y setup)
  uint8_t getPumpPin() const;                      // pin GPIO de la bomba
  bool isPumpOn() const;                            // ¿la bomba está encendida?
  uint8_t getSectorPin(uint8_t sectorId) const;    // pin GPIO de la válvula del sector (0 si id inválido)

  // Arma el DTO inmutable con todo el estado actual (lo serializa ApiHandler).
  SystemStateSnapshot getStateSnapshot() const;
  // Nombre textual del estado ("IDLE" / "RUNNING" / "MANUAL_STOP").
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
  uint16_t      _manualSectorFlow[Config::NUM_SECTORES]; // caudal manual por sector
  unsigned long _lastStepMs;          // marca del último paso de 1 s procesado

  // Motor de ejecución
  // Procesa un paso de 1 s: descuenta retardos y tiempos, drena la cola,
  // encola hijos de los que terminaron y refresca las salidas.
  void stepOneSecond(int nowMinutes);
  // Activa los nodos raíz del programa (arranque o reinicio de ciclo).
  void startRoots(int programIndex);
  // ¿Un programa cíclico puede reiniciar su ciclo a la hora actual?
  // false cuando ya se pasó la horaFin del programa (nowMinutes >= finMin).
  bool canRestartCycle(const Program& p, int nowMinutes) const;
  // Caudal total que circularía si estuvieran abiertas las válvulas de
  // 'irrigatingMask': suma el caudal de cada sector y el de TODOS sus ancestros
  // (cañería), contando cada sector una sola vez. Usa el programa en ejecución.
  uint16_t flowForSectorSet(uint16_t irrigatingMask) const;
  // Caudal ya reservado: activos + pendientes + toda su cañería.
  uint16_t committedFlow() const;
  // Máscara de sectores con caudal ya comprometido (pendientes de retardo).
  uint16_t computePendingMask() const;
  // ¿Activar 'sectorId' (abriendo su cañería) cabe en el caudal de la bomba,
  // dado lo que ya está activo/pendiente? Equivale al flowIfActivated del proto.
  bool fitsToActivate(uint8_t sectorId) const;
  // Activa el sector (o lo deja pendiente si tiene retardo) si entra en la
  // bomba; si no entra, lo manda a la cola FIFO.
  void tryActivateSector(uint8_t sectorId, uint32_t irrigationTime,
                         uint16_t flow, uint16_t delaySec);
  // Saca de la cola FIFO los sectores que ya entran en el caudal liberado.
  void drainQueue();
  // Encola (o activa) los hijos directos de un sector que acaba de terminar.
  void enqueueChildren(uint8_t parentSectorId);

  bool activeContains(uint8_t sectorId) const;   // ¿el sector ya está regando?
  // Agrega un sector a la lista de activos (regando).
  void addActive(uint8_t sectorId, uint32_t remaining, uint16_t flow);
  // Agrega un sector a la lista de pendientes (esperando su retardo).
  void addPending(uint8_t sectorId, uint16_t delaySec, uint16_t flow,
                  uint32_t irrigationTime);
  // Agrega un sector al final de la cola FIFO.
  void addQueued(uint8_t sectorId, uint32_t irrigationTime,
                 uint16_t delaySec, uint16_t flow);
  void clearRuntimeLists();                       // vacía activos, pendientes y cola

  // Salidas (válvulas, bomba). La cañería abre la válvula fija (no titila).
  uint16_t computeActiveMask() const;     // máscara de los sectores regando
  uint16_t computeFeedingMask() const;    // máscara de la cañería (ancestros de los activos)
  void applyOutputsFromState();           // recalcula y aplica válvulas + bomba
  void setSectorHardware(uint16_t openMask); // abre/cierra cada válvula física según la máscara

  // Detiene la ejecución, limpia el runtime y aplica salidas con el estado dado.
  void stopRuntime(SystemState newState);

  static uint16_t sectorIdToMask(uint8_t sectorId);  // id de sector (1..8) → bit de máscara
  static uint8_t firstSectorFromMask(uint16_t mask); // primer sector encendido (0 si ninguno)

  int findProgramIndexById(uint16_t id) const;  // índice del slot con ese id, o -1
  int findFreeProgramSlot() const;               // índice del primer slot libre, o -1
  // Valida el árbol del programa: raíz, padres existentes, sin ciclos y sin
  // deadlock de caudal (ningún sector + su cañería supera la bomba).
  bool validateProgram(const Program& p) const;
};
