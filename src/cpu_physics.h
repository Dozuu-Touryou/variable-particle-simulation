#pragma once

#include<vector>

#include "particle.h"
#include "cell.h"

using std::vector;

namespace cpu::physics {
   // Apply semi-implicit Euler integration to update the position of a particle
   void applyGravity(Particle&);
   
   // Attempt to register a particle's presence in each cell its area overlaps
   void mapParticle(vector<Cell>&, const Particle&, uint32_t, uint32_t);

   // Detect collision between a particle and surrounding particles using helpers.
   // Based on circumference-sampling variant of the neighbor search algorithm
   void collisionDetection(vector<Cell>&, vector<Particle>&, uint32_t, uint32_t);

   // Check if 2 particles are colliding
   bool checkCollision(const Particle&, const Particle&);

   // Separate 2 overlapping particles by moving each in the opposite direction of their collision normal
   void adjustPositions(Particle&, Particle&);

   // Apply an impulse-based collision response to update 2 colliding particles' velocities
   void resolveCollision(Particle&, Particle&);

   // Push particle back inside simulation area confines should it happen to reach boundaries
   void resolveBoundaryCollisions(Particle&);
};