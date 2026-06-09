#include <unity.h>
#include "../src/domain/Valve.h"
#include "../src/domain/Pump.h"
#include "../src/domain/Program.h"
#include "../src/domain/IrrigationSystem.h"

//Variables externas de los mocks para controlar su estado desde los tests.
extern uint8_t mock_pin_modes[256];
extern uint8_t mock_pin_states[256];
extern unsigned long mock_millis_value;

void setUp(void) {
    //Reinicio mocks antes de cada test.
    for (int i = 0; i < 256; i++) {
        mock_pin_modes[i] = 0;
        mock_pin_states[i] = 0;
    }
    mock_millis_value = 0;
}

void tearDown(void) {
    //Limpio después de cada test si es necesario (no en este caso).
}

void test_valve_open_close(void) {
    Valve v(13, 1);
    v.begin();
    
    //Chequeo estado inicial: válvula cerrada, pin LOW.
    TEST_ASSERT_EQUAL(0, mock_pin_states[13]);
    TEST_ASSERT_FALSE(v.isOpen());

    //Abro valvula y chequeo que el pin se active y el estado cambie.
    v.open();
    TEST_ASSERT_EQUAL(1, mock_pin_states[13]);
    TEST_ASSERT_TRUE(v.isOpen());

    //Cierro valvula y chequeo que el pin se desactive y el estado cambie.
    v.close();
    TEST_ASSERT_EQUAL(0, mock_pin_states[13]);
    TEST_ASSERT_FALSE(v.isOpen());
}

void test_active_low_valve_open_close(void) {
    Valve v(13, 1, true);
    v.begin();

    TEST_ASSERT_EQUAL(1, mock_pin_states[13]);
    TEST_ASSERT_FALSE(v.isOpen());

    v.open();
    TEST_ASSERT_EQUAL(0, mock_pin_states[13]);
    TEST_ASSERT_TRUE(v.isOpen());

    v.close();
    TEST_ASSERT_EQUAL(1, mock_pin_states[13]);
    TEST_ASSERT_FALSE(v.isOpen());
}

void test_pump_on_off(void) {
    Pump p(27);
    p.begin();

    TEST_ASSERT_EQUAL(0, mock_pin_states[27]);
    TEST_ASSERT_FALSE(p.isOn());

    p.on();
    TEST_ASSERT_EQUAL(1, mock_pin_states[27]);
    TEST_ASSERT_TRUE(p.isOn());

    p.off();
    TEST_ASSERT_EQUAL(0, mock_pin_states[27]);
    TEST_ASSERT_FALSE(p.isOn());
}

void test_active_low_pump_on_off(void) {
    Pump p(27, true);
    p.begin();

    TEST_ASSERT_EQUAL(1, mock_pin_states[27]);
    TEST_ASSERT_FALSE(p.isOn());

    p.on();
    TEST_ASSERT_EQUAL(0, mock_pin_states[27]);
    TEST_ASSERT_TRUE(p.isOn());

    p.off();
    TEST_ASSERT_EQUAL(1, mock_pin_states[27]);
    TEST_ASSERT_FALSE(p.isOn());
}

void test_irrigation_system_manual_control(void) {
    IrrigationSystem sys;
    sys.begin();

    //La bomba deberia estar apagada inicialmente y sin sectores activos.
    TEST_ASSERT_FALSE(sys.isPumpOn());

    //Activo manualmente el sector 1.
    sys.setManualSector(1, true);
    
    //Verifico que se active el sector y la bomba.
    TEST_ASSERT_TRUE(sys.isSectorActive(1));
    TEST_ASSERT_TRUE(sys.isPumpOn());
    TEST_ASSERT_EQUAL(0, mock_pin_states[13]);
    TEST_ASSERT_EQUAL(0, mock_pin_states[27]);

    //Desactivo manualmente el sector 1.
    sys.setManualSector(1, false);
    
    //Verifico que se desactive el sector y la bomba.
    TEST_ASSERT_FALSE(sys.isSectorActive(1));
    TEST_ASSERT_FALSE(sys.isPumpOn());
    TEST_ASSERT_EQUAL(1, mock_pin_states[13]);
    TEST_ASSERT_EQUAL(1, mock_pin_states[27]);
}

void test_program_tree_helpers(void) {
    //Árbol: sector 1 raíz; sectores 2 y 3 hijos del 1; sector 4 hijo del 2.
    // {sectorId, irrigationTime, delay, parentSectorId, flow}
    Program p;
    p.addNode({1, 60,  0, 0, 12});
    p.addNode({2, 30,  5, 1, 6});
    p.addNode({3, 30,  5, 1, 6});
    p.addNode({4, 20, 10, 2, 6});

    TEST_ASSERT_EQUAL(4, p.getSectorCount());

    //hasNode / findNodeBySectorId.
    TEST_ASSERT_TRUE(p.hasNode(1));
    TEST_ASSERT_TRUE(p.hasNode(4));
    TEST_ASSERT_FALSE(p.hasNode(8));
    TEST_ASSERT_NULL(p.findNodeBySectorId(8));

    const ProgramNode *n4 = p.findNodeBySectorId(4);
    TEST_ASSERT_NOT_NULL(n4);
    TEST_ASSERT_EQUAL(2, n4->parentSectorId);
    TEST_ASSERT_EQUAL(10, n4->delay);
    TEST_ASSERT_EQUAL(6, n4->flow);

    //getRootCount: solo el sector 1 es raíz.
    TEST_ASSERT_EQUAL(1, p.getRootCount());

    //getChildCount: el sector 1 tiene 2 hijos; el 2 tiene 1; el 3 ninguno.
    TEST_ASSERT_EQUAL(2, p.getChildCount(1));
    TEST_ASSERT_EQUAL(1, p.getChildCount(2));
    TEST_ASSERT_EQUAL(0, p.getChildCount(3));
}

// ============================================================
// Motor de ejecución — modelo árbol + caudal
// ============================================================

//Avanza el reloj simulado n segundos, procesando un paso de motor por segundo.
static void advanceSeconds(IrrigationSystem &sys, int n) {
    for (int i = 0; i < n; i++) {
        mock_millis_value += 1000;
        sys.tick();
    }
}

//Pines de los sectores 2, 3 y 4 (todos activo-alto → abrir = pin HIGH).
//Sector N → índice N-1 en Config::PINES_SECTORES = {13,14,16,17,32,33,25,26}.
static const uint8_t PIN_S2 = 14;
static const uint8_t PIN_S3 = 16;

void test_engine_root_then_child_with_delay(void) {
    //Árbol: s2 raíz (2 s); s3 hijo de s2 (2 s) con retardo de 3 s.
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();

    Program p;
    p.setCyclic(false);
    p.addNode({2, 2, 0, 0, 6}); // {sectorId, irrigationTime, delay, parent, flow}
    p.addNode({3, 2, 3, 2, 6});
    uint16_t id = sys.saveProgram(p);
    TEST_ASSERT_NOT_EQUAL(0, id);

    TEST_ASSERT_TRUE(sys.startProgramById(id));

    //Al arrancar, solo la raíz s2 riega.
    SystemStateSnapshot s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(2, s.active[0].sectorId);

    //Tras 2 s la raíz termina; el hijo queda pendiente por su retardo.
    advanceSeconds(sys, 2);
    s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(0, s.activeCount);
    TEST_ASSERT_EQUAL(1, s.pendingCount);
    TEST_ASSERT_EQUAL(3, s.pending[0].sectorId);
    TEST_ASSERT_TRUE((s.completedMask & (1 << (2 - 1))) != 0); // s2 completado

    //Tras agotarse el retardo (3 s más), el hijo pasa a regar.
    advanceSeconds(sys, 3);
    s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(3, s.active[0].sectorId);
    TEST_ASSERT_EQUAL(0, s.pendingCount);

    //Tras 2 s más el hijo termina y, al no ser cíclico, el sistema para.
    advanceSeconds(sys, 2);
    TEST_ASSERT_FALSE(sys.isRunning());
}

void test_engine_parallel_children_by_flow(void) {
    //s2 raíz (3 s); s3 y s4 hijos de s2, ambos sin retardo y con caudal que entra.
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();

    Program p;
    p.addNode({2, 3, 0, 0, 6});
    p.addNode({3, 3, 0, 2, 3});
    p.addNode({4, 3, 0, 2, 3});
    uint16_t id = sys.saveProgram(p);
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_TRUE(sys.startProgramById(id));

    //Tras 3 s la raíz termina y sus dos hijos arrancan en paralelo.
    advanceSeconds(sys, 3);
    SystemStateSnapshot s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(2, s.activeCount);
    uint16_t mask = 0;
    for (uint8_t i = 0; i < s.activeCount; i++) mask |= (1 << (s.active[i].sectorId - 1));
    TEST_ASSERT_TRUE((mask & (1 << (3 - 1))) != 0);
    TEST_ASSERT_TRUE((mask & (1 << (4 - 1))) != 0);
    TEST_ASSERT_TRUE(sys.isPumpOn());
}

void test_engine_fifo_queue_and_drain(void) {
    //3 raíces de caudal 8. Validado contra una bomba de 30, luego se baja a 20:
    //entran 2 raíces (16) y la tercera (24) va a la cola FIFO.
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();
    sys.setPumpFlow(30);

    Program p;
    p.addNode({2, 3, 0, 0, 8});
    p.addNode({3, 3, 0, 0, 8});
    p.addNode({4, 3, 0, 0, 8});
    uint16_t id = sys.saveProgram(p);
    TEST_ASSERT_NOT_EQUAL(0, id);

    sys.setPumpFlow(20);
    TEST_ASSERT_TRUE(sys.startProgramById(id));

    SystemStateSnapshot s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(2, s.activeCount);
    TEST_ASSERT_EQUAL(1, s.queuedCount);
    TEST_ASSERT_EQUAL(4, s.queued[0].sectorId);

    //Al terminar las dos raíces activas (3 s) se libera caudal y la cola drena.
    advanceSeconds(sys, 3);
    s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(0, s.queuedCount);
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(4, s.active[0].sectorId);
}

void test_engine_feeding_valve_stays_open(void) {
    //s2 raíz (2 s); s3 hijo de s2 (4 s). Mientras s3 riega, s2 actúa de cañería:
    //su válvula queda FIJA abierta (la solenoide no cicla), no titila.
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();

    Program p;
    p.addNode({2, 2, 0, 0, 6});
    p.addNode({3, 4, 0, 2, 6});
    uint16_t id = sys.saveProgram(p);
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_TRUE(sys.startProgramById(id));

    //Tras 2 s: s2 terminó y alimenta a s3; s3 riega (válvula fija abierta).
    advanceSeconds(sys, 2);
    SystemStateSnapshot s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(3, s.active[0].sectorId);
    TEST_ASSERT_EQUAL(1, mock_pin_states[PIN_S3]); // s3 activo: abierto fijo
    TEST_ASSERT_EQUAL(1, mock_pin_states[PIN_S2]); // s2 cañería: abierto fijo

    //Pasos sucesivos: la cañería NO titila, se mantiene abierta mientras s3 riega.
    advanceSeconds(sys, 1);
    TEST_ASSERT_EQUAL(1, mock_pin_states[PIN_S3]);
    TEST_ASSERT_EQUAL(1, mock_pin_states[PIN_S2]); // sigue abierto fijo

    advanceSeconds(sys, 1);
    TEST_ASSERT_EQUAL(1, mock_pin_states[PIN_S3]);
    TEST_ASSERT_EQUAL(1, mock_pin_states[PIN_S2]); // sigue abierto fijo
}

void test_engine_cyclic_restarts_roots(void) {
    //s2 raíz (2 s), programa cíclico: al vaciarse todo, reinicia.
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();

    Program p;
    p.setCyclic(true);
    p.addNode({2, 2, 0, 0, 6});
    uint16_t id = sys.saveProgram(p);
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_TRUE(sys.startProgramById(id));

    //Tras 2 s la raíz termina y, por ser cíclico, vuelve a arrancar.
    advanceSeconds(sys, 2);
    TEST_ASSERT_TRUE(sys.isRunning());
    SystemStateSnapshot s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(2, s.active[0].sectorId);
    TEST_ASSERT_EQUAL(0, s.completedMask); // reinicio limpia completados
}

void test_engine_validations(void) {
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();
    sys.setPumpFlow(20);

    //Válido: raíz + hijo dentro de los límites (pathFlow hijo = 6 + 10 = 16 ≤ 20).
    Program ok;
    ok.addNode({2, 10, 0, 0, 10});
    ok.addNode({3, 10, 0, 2, 6});
    TEST_ASSERT_NOT_EQUAL(0, sys.saveProgram(ok));

    //Sin raíz (todos cuelgan de otro).
    Program noRoot;
    noRoot.addNode({2, 10, 0, 3, 6});
    noRoot.addNode({3, 10, 0, 2, 6});
    TEST_ASSERT_EQUAL(0, sys.saveProgram(noRoot));

    //Padre inexistente.
    Program badParent;
    badParent.addNode({2, 10, 0, 0, 6});
    badParent.addNode({3, 10, 0, 7, 6}); // sector 7 no está en el programa
    TEST_ASSERT_EQUAL(0, sys.saveProgram(badParent));

    //Deadlock: una raíz que pide más caudal que la bomba nunca podría regar.
    Program tooMuchFlow;
    tooMuchFlow.addNode({2, 10, 0, 0, 25});
    TEST_ASSERT_EQUAL(0, sys.saveProgram(tooMuchFlow));

    //Deadlock por cañería: caudal del nodo + el de sus ancestros supera la bomba.
    //S2(12) raíz, S3(12) hijo → pathFlow(S3) = 24 > 20: la cadena entera debe
    //abrirse para regar S3 y nunca entraría en la bomba.
    Program deadlockChain;
    deadlockChain.addNode({2, 10, 0, 0, 12});
    deadlockChain.addNode({3, 10, 0, 2, 12});
    TEST_ASSERT_EQUAL(0, sys.saveProgram(deadlockChain));

    //YA NO bloquea: Σ caudal de las raíces > bomba. El motor las encola por
    //turnos (cada pathFlow individual ≤ bomba).
    Program rootsOverflow;
    rootsOverflow.addNode({2, 10, 0, 0, 12});
    rootsOverflow.addNode({3, 10, 0, 0, 12}); // 24 > 20, pero cada raíz ≤ 20
    TEST_ASSERT_NOT_EQUAL(0, sys.saveProgram(rootsOverflow));

    //YA NO bloquea: Σ caudal de hijos > caudal del padre. No es deadlock
    //(pathFlow de cada hijo = 5 + 6 = 11 ≤ 20); de a uno entran, el resto encola.
    Program childrenOverflow;
    childrenOverflow.addNode({2, 10, 0, 0, 6});
    childrenOverflow.addNode({3, 10, 0, 2, 5});
    childrenOverflow.addNode({4, 10, 0, 2, 5});
    TEST_ASSERT_NOT_EQUAL(0, sys.saveProgram(childrenOverflow));
}

void test_seed_programs_tree_shape(void) {
    //El seed por defecto debe quedar como árboles bien formados y ejecutables.
    IrrigationSystem sys; // InitMode::WITH_SEED
    sys.begin();

    //La bomba por defecto pasó a 30 L/min (modelo cañería suma caudal).
    TEST_ASSERT_EQUAL(30, sys.getPumpFlow());

    //Programa 1: S1 raíz con dos hijos (S2, S3). Ya no incluye S4/S5.
    const Program &p1 = sys.programAt(0);
    TEST_ASSERT_TRUE(p1.isValid());
    TEST_ASSERT_EQUAL(1, p1.getId());
    TEST_ASSERT_EQUAL(3, p1.getSectorCount());
    TEST_ASSERT_EQUAL(1, p1.getRootCount());
    TEST_ASSERT_EQUAL(2, p1.getChildCount(1)); // S2 y S3 cuelgan de S1
    TEST_ASSERT_FALSE(p1.hasNode(5));
    const ProgramNode *root = p1.findNodeBySectorId(1);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(0, root->parentSectorId);
    TEST_ASSERT_EQUAL(12, root->flow);

    //Es ejecutable: al arrancar, solo la raíz S1 riega.
    TEST_ASSERT_TRUE(sys.startProgramById(1));
    SystemStateSnapshot s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(1, s.active[0].sectorId);

    //Programa 3: tres raíces en paralelo.
    const Program &p3 = sys.programAt(2);
    TEST_ASSERT_EQUAL(3, p3.getId());
    TEST_ASSERT_EQUAL(3, p3.getRootCount());
}

void test_engine_feeding_counts_toward_flow(void) {
    //Modelo nuevo: la cañería (ancestros abiertos) suma caudal contra la bomba.
    //S1 raíz (12), S2 y S3 hijos de S1 (6 c/u), sin retardo. Bomba = 20.
    //Naïve S2+S3 = 12 ≤ 20, pero con S1 de cañería: 12+6+6 = 24 > 20 → no caben
    //juntos. Uno riega y el otro espera en la cola hasta que se libere caudal.
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();
    sys.setPumpFlow(20);

    Program p;
    p.addNode({1, 3, 0, 0, 12});
    p.addNode({2, 3, 0, 1, 6});
    p.addNode({3, 3, 0, 1, 6});
    uint16_t id = sys.saveProgram(p); // pathFlow(S2)=pathFlow(S3)=18 ≤ 20 → válido
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_TRUE(sys.startProgramById(id));

    //Al terminar S1 (3 s): S2 entra (S1 cañería 12 + S2 6 = 18 ≤ 20); S3 no
    //entra (sumaría 24) y queda en la cola.
    advanceSeconds(sys, 3);
    SystemStateSnapshot s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(2, s.active[0].sectorId);
    TEST_ASSERT_EQUAL(1, s.queuedCount);
    TEST_ASSERT_EQUAL(3, s.queued[0].sectorId);

    //Al terminar S2 (3 s más) se libera caudal y S3 drena de la cola a activo.
    advanceSeconds(sys, 3);
    s = sys.getStateSnapshot();
    TEST_ASSERT_EQUAL(0, s.queuedCount);
    TEST_ASSERT_EQUAL(1, s.activeCount);
    TEST_ASSERT_EQUAL(3, s.active[0].sectorId);
}

//Igual que advanceSeconds, pero pasando la hora actual (minutos desde medianoche)
//al motor, para ejercitar el gate de horaFin de los programas cíclicos.
static void advanceSecondsAt(IrrigationSystem &sys, int n, int nowMinutes) {
    for (int i = 0; i < n; i++) {
        mock_millis_value += 1000;
        sys.tick(nowMinutes);
    }
}

void test_irrigation_system_manual_flow_limit(void) {
    //La bomba da 5 L/min; cada sector manual consume 2 L/min por defecto.
    //Entran dos sectores (4 ≤ 5); el tercero (6 > 5) se rechaza.
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();
    sys.setPumpFlow(5);

    TEST_ASSERT_TRUE(sys.setManualSector(1, true));   // 2 ≤ 5
    TEST_ASSERT_TRUE(sys.setManualSector(2, true));   // 4 ≤ 5
    TEST_ASSERT_FALSE(sys.setManualSector(3, true));  // 6 > 5 → rechazado
    TEST_ASSERT_FALSE(sys.isSectorActive(3));

    //Apagar siempre se permite y libera caudal para que el tercero entre.
    TEST_ASSERT_TRUE(sys.setManualSector(1, false));
    TEST_ASSERT_TRUE(sys.setManualSector(3, true));   // 2 + 2 = 4 ≤ 5
    TEST_ASSERT_TRUE(sys.isSectorActive(3));
}

void test_engine_cyclic_stops_after_endtime(void) {
    //Programa cíclico con horaFin 08:00 (480 min): reinicia dentro de la ventana,
    //pero pasada la horaFin no vuelve a arrancar (deja terminar el ciclo en curso).
    IrrigationSystem sys(IrrigationSystem::InitMode::EMPTY);
    sys.begin();

    Program p;
    p.setCyclic(true);
    p.setEndTime("08:00");
    p.addNode({2, 2, 0, 0, 6});
    uint16_t id = sys.saveProgram(p);
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_TRUE(sys.startProgramById(id));

    //07:00 (420 < 480): al vaciarse, reinicia el ciclo.
    advanceSecondsAt(sys, 2, 420);
    TEST_ASSERT_TRUE(sys.isRunning());

    //08:30 (510 ≥ 480): al vaciarse, no reinicia → el programa termina.
    advanceSecondsAt(sys, 2, 510);
    TEST_ASSERT_FALSE(sys.isRunning());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valve_open_close);
    RUN_TEST(test_active_low_valve_open_close);
    RUN_TEST(test_pump_on_off);
    RUN_TEST(test_active_low_pump_on_off);
    RUN_TEST(test_irrigation_system_manual_control);
    RUN_TEST(test_program_tree_helpers);
    RUN_TEST(test_engine_root_then_child_with_delay);
    RUN_TEST(test_engine_parallel_children_by_flow);
    RUN_TEST(test_engine_fifo_queue_and_drain);
    RUN_TEST(test_engine_feeding_valve_stays_open);
    RUN_TEST(test_engine_cyclic_restarts_roots);
    RUN_TEST(test_engine_cyclic_stops_after_endtime);
    RUN_TEST(test_irrigation_system_manual_flow_limit);
    RUN_TEST(test_engine_validations);
    RUN_TEST(test_engine_feeding_counts_toward_flow);
    RUN_TEST(test_seed_programs_tree_shape);
    return UNITY_END();
}
