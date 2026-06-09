#include "Program.h"

Program::Program() {
  reset();
}

void Program::reset() {
  _valid       = false;
  _id          = 0;
  memset(_name, 0, sizeof(_name));
  memset(_startTime, 0, sizeof(_startTime));
  memset(_endTime, 0, sizeof(_endTime));
  _days        = 0;
  _cyclic      = false;
  _sectorCount = 0;
  memset(_nodes, 0, sizeof(_nodes));
}

bool Program::isValid() const { return _valid; }
void Program::setValid(bool valid) { _valid = valid; }

uint16_t Program::getId() const { return _id; }
void Program::setId(uint16_t id) { _id = id; }

const char* Program::getName() const { return _name; }
void Program::setName(const char* name) {
  if (name == nullptr) { _name[0] = '\0'; return; }
  strncpy(_name, name, sizeof(_name) - 1);
  _name[sizeof(_name) - 1] = '\0';
}

const char* Program::getStartTime() const { return _startTime; }
void Program::setStartTime(const char* time) {
  strncpy(_startTime, time, sizeof(_startTime) - 1);
  _startTime[sizeof(_startTime) - 1] = '\0';
}

const char* Program::getEndTime() const { return _endTime; }
void Program::setEndTime(const char* time) {
  if (time == nullptr) { _endTime[0] = '\0'; return; }
  strncpy(_endTime, time, sizeof(_endTime) - 1);
  _endTime[sizeof(_endTime) - 1] = '\0';
}

uint8_t Program::getDays() const { return _days; }
void Program::setDays(uint8_t days) { _days = days; }

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

const ProgramNode* Program::findNodeBySectorId(uint8_t sectorId) const {
  for (uint8_t i = 0; i < _sectorCount; i++) {
    if (_nodes[i].sectorId == sectorId) return &_nodes[i];
  }
  return nullptr;
}

bool Program::hasNode(uint8_t sectorId) const {
  return findNodeBySectorId(sectorId) != nullptr;
}

uint8_t Program::getRootCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < _sectorCount; i++) {
    if (_nodes[i].parentSectorId == 0) count++;
  }
  return count;
}

uint8_t Program::getChildCount(uint8_t parentSectorId) const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < _sectorCount; i++) {
    if (_nodes[i].parentSectorId == parentSectorId) count++;
  }
  return count;
}

uint16_t Program::getPathFlow(uint8_t sectorId) const {
  const ProgramNode* node = findNodeBySectorId(sectorId);
  if (node == nullptr) return 0;
  uint32_t sum  = 0;
  uint8_t  hops = 0;
  while (node != nullptr) {
    sum += node->flow;
    if (node->parentSectorId == 0) break;
    if (++hops > _sectorCount) break;  // guarda anti-ciclo
    node = findNodeBySectorId(node->parentSectorId);
  }
  return (uint16_t)sum;
}
