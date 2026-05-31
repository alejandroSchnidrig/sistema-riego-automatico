#pragma once
#include "Arduino.h"

// Abstracción de funciones de hardware para permitir tests unitarios en entorno nativo (PC) sin Arduino.

#define HAL_OUTPUT 1
#define HAL_INPUT  0
#define HAL_HIGH   1
#define HAL_LOW    0

void hal_pinMode(uint8_t pin, uint8_t mode);
void hal_digitalWrite(uint8_t pin, uint8_t val);
unsigned long hal_millis();