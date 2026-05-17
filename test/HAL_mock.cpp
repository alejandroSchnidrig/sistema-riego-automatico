#include "../src/core/HAL.h"

//Mock de funciones de ESP32. Permite simular el comportamiento de los pines y el tiempo para tests unitarios en entorno nativo (PC) sin Arduino.

uint8_t mock_pin_modes[256] = {0};
uint8_t mock_pin_states[256] = {0};
unsigned long mock_millis_value = 0;

void hal_pinMode(uint8_t pin, uint8_t mode) {
    mock_pin_modes[pin] = mode;
}

void hal_digitalWrite(uint8_t pin, uint8_t val) {
    mock_pin_states[pin] = val;
}

unsigned long hal_millis() {
    return mock_millis_value;
}