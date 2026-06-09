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

  // Nombre legible del programa (puede estar vacío → la UI usa "Programa #id").
  const char* getName() const;
  void setName(const char* name);

  const char* getStartTime() const;
  void setStartTime(const char* time);

  // Hora de finalización "HH:MM" del rango de repetición de un programa cíclico.
  // Vacío ("") = sin fin (repetición indefinida, comportamiento legacy).
  const char* getEndTime() const;
  void setEndTime(const char* time);

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
  // Caudal de la "cañería" de un sector: su propio caudal + el de TODOS sus
  // ancestros (la cadena de padres que debe abrirse para que el agua le llegue).
  // 0 si el sector no existe. Guarda anti-ciclo para árboles mal formados.
  uint16_t getPathFlow(uint8_t sectorId) const;

  void reset();

private:
  bool        _valid;
  uint16_t    _id;
  char        _name[Config::NOMBRE_PROGRAMA_MAX_LEN]; // nombre legible ("" = sin nombre)
  char        _startTime[6]; // formato "HH:MM\0"
  char        _endTime[6];   // formato "HH:MM\0" ("" = sin fin), solo cíclicos
  uint8_t     _days;          // bitmask: bit0=lunes … bit6=domingo
  bool        _cyclic;
  uint8_t     _sectorCount;
  ProgramNode _nodes[Config::NUM_SECTORES];
};
