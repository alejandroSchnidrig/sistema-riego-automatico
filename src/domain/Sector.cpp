#include "Sector.h"

// El id del sector coincide con el id de su válvula (1-8)
Sector::Sector(uint8_t id, uint8_t pin)
  : _id(id), _valve(pin, id) {}

void Sector::begin() {
  _valve.begin();
}

void Sector::activate() {
  _valve.open();
}

void Sector::deactivate() {
  _valve.close();
}

bool    Sector::isActive() const { return _valve.isOpen(); }
uint8_t Sector::getId()    const { return _id;             }
uint8_t Sector::getPin()   const { return _valve.getPin(); }
Valve&  Sector::getValve()       { return _valve;          }
