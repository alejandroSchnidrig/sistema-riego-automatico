#include "Valve.h"

Valve::Valve(uint8_t pin, uint8_t id)
  : _pin(pin), _id(id), _isOpen(false) {}

void Valve::begin() {
  pinMode(_pin, OUTPUT);
  close();  // estado seguro al arrancar: válvula cerrada
}

void Valve::open() {
  _isOpen = true;
  digitalWrite(_pin, HIGH);
}

void Valve::close() {
  _isOpen = false;
  digitalWrite(_pin, LOW);
}

bool    Valve::isOpen()  const { return _isOpen; }
uint8_t Valve::getId()   const { return _id;     }
uint8_t Valve::getPin()  const { return _pin;    }
