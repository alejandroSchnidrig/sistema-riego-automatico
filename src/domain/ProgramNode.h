#pragma once
#include <stdint.h>

// Paso de ejecución dentro de un programa (modelo lineal — se reemplaza en Fase D)
struct ProgramNode {
  uint8_t  id;             // ID del sector a regar (1-8)
  uint8_t  order;          // posición en la secuencia (1 = primero)
  uint32_t irrigationTime; // duración del riego en segundos
};
