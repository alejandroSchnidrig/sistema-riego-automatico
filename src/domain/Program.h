#pragma once
#include <stdint.h>
#include <string.h>
#include "ProgramNode.h"
#include "../config/Config.h"

class Program {
public:
  Program();

  bool isValid() const;
  void setValid(bool valid);

  uint16_t getId() const;
  void setId(uint16_t id);

  const char* getStartTime() const;
  void setStartTime(const char* time);

  uint8_t getDays() const;
  void setDays(uint8_t days);

  uint16_t getSectorDelay() const;
  void setSectorDelay(uint16_t delay);

  bool isCyclic() const;
  void setCyclic(bool cyclic);

  uint8_t getSectorCount() const;
  void clearNodes();
  bool addNode(const ProgramNode& node);

  const ProgramNode& getNode(uint8_t index) const;
  ProgramNode& getNode(uint8_t index);

  void sortNodesByOrder();
  void reset();

private:
  bool        _valid;
  uint16_t    _id;
  char        _startTime[6];
  uint8_t     _days;
  uint16_t    _sectorDelay;
  bool        _cyclic;
  uint8_t     _sectorCount;
  ProgramNode _nodes[Config::NUM_SECTORES];
};
