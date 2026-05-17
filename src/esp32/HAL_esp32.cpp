#include "../core/HAL.h"

#ifndef NATIVE_TEST

#include <Arduino.h>

void hal_pinMode(uint8_t pin, uint8_t mode) {
    pinMode(pin, mode == HAL_OUTPUT ? OUTPUT : INPUT);
}

void hal_digitalWrite(uint8_t pin, uint8_t val) {
    digitalWrite(pin, val == HAL_HIGH ? HIGH : LOW);
}

unsigned long hal_millis() {
    return millis();
}

#endif // ARDUINO