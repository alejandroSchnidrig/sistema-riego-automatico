#pragma once

#include <Arduino.h>

// ============================================================
// Valve — representa una válvula solenoide (o LED en maqueta).
//
// Responsabilidades:
//   - Conocer su pin GPIO y su ID de sector (1-8)
//   - Abrir y cerrar la válvula (HIGH / LOW)
//   - Reportar su estado actual
//
// Lo que NO hace: no conoce tiempos ni secuencias.
// Eso es responsabilidad de Sector y Program.
// ============================================================

class Valve {
public:
  Valve(uint8_t pin, uint8_t id);

  // Configura el pin como salida y cierra la válvula.
  // Debe llamarse desde setup(), no desde el constructor,
  // porque pinMode() no está disponible antes de que Arduino
  // inicialice el hardware.
  void begin();

  void open();           // activa la válvula (pin → HIGH)
  void close();          // desactiva la válvula (pin → LOW)

  bool    isOpen()  const;
  uint8_t getId()   const;
  uint8_t getPin()  const;

private:
  uint8_t _pin;
  uint8_t _id;
  bool    _isOpen;
};
