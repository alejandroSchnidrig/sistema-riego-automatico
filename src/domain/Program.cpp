#include "Program.h"

Program::Program() {
  reset();
}

void Program::reset() {
  _valid       = false;
  _id          = 0;
  memset(_startTime, 0, sizeof(_startTime));
  _days        = 0;
  _sectorDelay = 0;
  _cyclic      = false;
  _sectorCount = 0;
  memset(_nodes, 0, sizeof(_nodes));
}

bool Program::isValid() const { return _valid; }
void Program::setValid(bool valid) { _valid = valid; }

uint16_t Program::getId() const { return _id; }
void Program::setId(uint16_t id) { _id = id; }

const char* Program::getStartTime() const { return _startTime; }
void Program::setStartTime(const char* time) {
  strncpy(_startTime, time, sizeof(_startTime) - 1);
  _startTime[sizeof(_startTime) - 1] = '\0';
}

uint8_t Program::getDays() const { return _days; }
void Program::setDays(uint8_t days) { _days = days; }

uint16_t Program::getSectorDelay() const { return _sectorDelay; }
void Program::setSectorDelay(uint16_t delay) { _sectorDelay = delay; }

bool Program::isCyclic() const { return _cyclic; }
void Program::setCyclic(bool cyclic) { _cyclic = cyclic; }

uint8_t Program::getSectorCount() const { return _sectorCount; }

void Program::clearNodes() { _sectorCount = 0; }

bool Program::addNode(const ProgramNode& node) {
  if (_sectorCount >= Config::NUM_SECTORES) return false;
  _nodes[_sectorCount++] = node;
  return true;
}

const ProgramNode& Program::getNode(uint8_t index) const {
  return _nodes[index];
}

ProgramNode& Program::getNode(uint8_t index) {
  return _nodes[index];
}

void Program::sortNodesByOrder() {
  for (uint8_t i = 0; i < _sectorCount; i++) {
    for (uint8_t j = i + 1; j < _sectorCount; j++) {
      if (_nodes[j].order < _nodes[i].order) {
        ProgramNode tmp = _nodes[i];
        _nodes[i]       = _nodes[j];
        _nodes[j]       = tmp;
      }
    }
  }
}
