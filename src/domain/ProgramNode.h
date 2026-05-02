#pragma once
#include <stdint.h>

// A single execution step within a program (linear model — replaced in Phase D)
struct ProgramNode {
  uint8_t  id;
  uint8_t  order;
  uint32_t irrigationTime;
};
