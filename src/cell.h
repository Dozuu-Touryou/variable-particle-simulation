#pragma once

#include "simulation_constants.h"

struct Cell {
   uint32_t particleIds[sim::MaxParticlesPerCell]{};
   uint32_t idCount = 0;
   uint32_t timestamp = 0;

   void add(uint32_t, uint32_t);
};

inline void Cell::add(uint32_t id, uint32_t currentTimestamp)
{
   if (timestamp != currentTimestamp) {
      timestamp = currentTimestamp;
      idCount = 0;
   }
   if (idCount < sim::MaxParticlesPerCell)
      particleIds[idCount++] = id;
}