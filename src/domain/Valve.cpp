#include "Valve.h"

Valve::Valve(uint8_t pin, uint8_t id, bool activeLow)
  : _pin(pin), _id(id), _activeLow(activeLow), _isOpen(false) {}

void Valve::begin() {
  hal_pinMode(_pin, HAL_OUTPUT);
  close();  // estado seguro al arrancar: válvula cerrada
}

void Valve::open() {
  _isOpen = true;
  hal_digitalWrite(_pin, _activeLow ? HAL_LOW : HAL_HIGH);
}

void Valve::close() {
  _isOpen = false;
  hal_digitalWrite(_pin, _activeLow ? HAL_HIGH : HAL_LOW);
}

bool    Valve::isOpen()  const { return _isOpen; }
uint8_t Valve::getId()   const { return _id;     }
uint8_t Valve::getPin()  const { return _pin;    }
