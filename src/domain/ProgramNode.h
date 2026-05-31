#pragma once
#include <stdint.h>

// Nodo de un programa de riego (modelo árbol + caudal).
//
// El programa es un árbol: cada nodo riega un sector y puede colgar de un
// padre (otro sector del mismo programa) o ser raíz. El delay es propio del
// nodo (espera tras terminar el padre) y el flow indica cuántos L/min
// consume el sector mientras riega.
struct ProgramNode {
  uint8_t  sectorId;        // ID del sector a regar (1-8)
  uint32_t irrigationTime;  // duración del riego en segundos
  uint16_t delay;           // segundos de espera tras terminar el padre
  uint8_t  parentSectorId;  // 0 = raíz  (en JSON: "padre": null)
  uint16_t flow;            // L/min requeridos por el sector ("caudal")
};
