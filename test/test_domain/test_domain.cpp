#include <unity.h>
#include "../src/domain/Valve.h"
#include "../src/domain/Pump.h"
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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_valve_open_close);
    RUN_TEST(test_active_low_valve_open_close);
    RUN_TEST(test_pump_on_off);
    RUN_TEST(test_active_low_pump_on_off);
    RUN_TEST(test_irrigation_system_manual_control);
    return UNITY_END();
}
