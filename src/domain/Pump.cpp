#include "Pump.h"

Pump::Pump(uint8_t pin, bool activeLow)
  : _pin(pin), _activeLow(activeLow), _isOn(false) {}

void Pump::begin() {
  hal_pinMode(_pin, HAL_OUTPUT);
  off();  // estado seguro al arrancar: bomba apagada
}

void Pump::on() {
  _isOn = true;
  hal_digitalWrite(_pin, _activeLow ? HAL_LOW : HAL_HIGH);
}

void Pump::off() {
  _isOn = false;
  hal_digitalWrite(_pin, _activeLow ? HAL_HIGH : HAL_LOW);
}

bool    Pump::isOn()   const { return _isOn; }
uint8_t Pump::getPin() const { return _pin;  }
