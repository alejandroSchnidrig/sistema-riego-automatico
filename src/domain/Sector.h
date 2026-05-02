#pragma once

#include "Valve.h"

// ============================================================
// Sector — representa una zona de riego independiente.
//
// Responsabilidades:
//   - Poseer la Valve asociada a su zona
//   - Activar y desactivar su válvula
//   - Reportar si está activo
//
// Nota de diseño: Sector posee su Valve por valor (composición)
// en lugar de por referencia, para permitir arrays estáticos
// de tamaño fijo sin problemas de lifetime ni arrays paralelos.
//
// Lo que NO hace: no conoce tiempos, secuencias ni programas.
// Eso es responsabilidad de Program e IrrigationSystem.
// ============================================================

class Sector {
public:
  Sector(uint8_t id, uint8_t pin);

  // Inicializa el pin de la válvula asociada.
  // Debe llamarse desde setup().
  void begin();

  void activate();          // abre la válvula del sector
  void deactivate();        // cierra la válvula del sector

  bool    isActive() const;
  uint8_t getId()    const;
  uint8_t getPin()   const; // pin GPIO de la válvula
  Valve&  getValve();       // acceso a la válvula subyacente

private:
  uint8_t _id;
  Valve   _valve;
};
