#include "Pump.h"

Pump::Pump(uint8_t pin)
  : _pin(pin), _isOn(false) {}

void Pump::begin() {
  pinMode(_pin, OUTPUT);
  off();  // estado seguro al arrancar: bomba apagada
}

void Pump::on() {
  _isOn = true;
  digitalWrite(_pin, HIGH);
}

void Pump::off() {
  _isOn = false;
  digitalWrite(_pin, LOW);
}

bool    Pump::isOn()   const { return _isOn; }
uint8_t Pump::getPin() const { return _pin;  }
