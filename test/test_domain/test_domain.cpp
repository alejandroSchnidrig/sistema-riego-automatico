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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valve_open_close);
    RUN_TEST(test_active_low_valve_open_close);
    RUN_TEST(test_pump_on_off);
    RUN_TEST(test_active_low_pump_on_off);
    RUN_TEST(test_irrigation_system_manual_control);
    RUN_TEST(test_program_tree_helpers);
    return UNITY_END();
}
