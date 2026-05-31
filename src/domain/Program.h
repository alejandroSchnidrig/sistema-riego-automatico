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

  bool isCyclic() const;
  void setCyclic(bool cyclic);

  uint8_t getSectorCount() const;
  void clearNodes();
  bool addNode(const ProgramNode& node);

  const ProgramNode& getNode(uint8_t index) const;
  ProgramNode& getNode(uint8_t index);

  // Helpers del modelo árbol
  // Devuelve el nodo cuyo sectorId coincide, o nullptr si no existe.
  const ProgramNode* findNodeBySectorId(uint8_t sectorId) const;
  // True si algún nodo riega ese sector.
  bool hasNode(uint8_t sectorId) const;
  // Cantidad de nodos raíz (padreSectorId == 0).
  uint8_t getRootCount() const;
  // Cantidad de hijos directos de un sector dado.
  uint8_t getChildCount(uint8_t parentSectorId) const;

  void reset();

private:
  bool        _valid;
  uint16_t    _id;
  char        _startTime[6]; // formato "HH:MM\0"
  uint8_t     _days;          // bitmask: bit0=lunes … bit6=domingo
  bool        _cyclic;
  uint8_t     _sectorCount;
  ProgramNode _nodes[Config::NUM_SECTORES];
};
