#pragma once

#include <Arduino.h>

// ============================================================
// Pump — representa la bomba de agua central del sistema.
//
// Responsabilidades:
//   - Conocer su pin GPIO
//   - Encenderse y apagarse
//   - Reportar su estado actual
//
// Lo que NO hace: la bomba no se controla en forma independiente.
// Siempre sigue automáticamente al estado de los sectores activos.
// Esa decisión la toma IrrigationSystem, no esta clase.
// ============================================================

class Pump {
public:
  explicit Pump(uint8_t pin);

  // Configura el pin como salida y apaga la bomba.
  // Debe llamarse desde setup(), no desde el constructor.
  void begin();

  void on();            // enciende la bomba (pin → HIGH)
  void off();           // apaga la bomba   (pin → LOW)

  bool    isOn()    const;
  uint8_t getPin()  const;

private:
  uint8_t _pin;
  bool    _isOn;
};
