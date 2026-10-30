#include <numbers>
#include <algorithm>

#include <glm/glm.hpp>

#include "cpu_physics.h"
#include "simulation_constants.h"

void cpu::physics::applyGravity(Particle& particle)
{
   particle.velocity += sim::Gravity * sim::TimeStep;
   particle.center += particle.velocity * sim::TimeStep;
   particle.velocity *= sim::VelocityDamper;
}

void cpu::physics::mapParticle(vector<Cell>& grid, const Particle& p, uint32_t pId, uint32_t tstamp)
{
   const float angle = sim::CellDimension / p.radius;
   for (float ai = angle * 0.5f; ai < 2.0f * std::numbers::pi; ai += angle) {
      const float circlePointX = p.center.x + p.radius * cosf(ai);
      const float circlePointY = p.center.y + p.radius * sinf(ai);
      int32_t gridX = (circlePointX + 1.0f) / sim::CellDimension;
      gridX = std::clamp(gridX, 0, static_cast<int32_t>(sim::GridDimension - 1));
      int32_t gridY = (circlePointY + 1.0f) / sim::CellDimension;
      gridY = std::clamp(gridY, 0, static_cast<int32_t>(sim::GridDimension - 1));
      grid[gridX + gridY * sim::GridDimension].add(pId, tstamp);
   }
}

void cpu::physics::collisionDetection(
   vector<Cell>& grid, vector<Particle>& particles, uint32_t activeParticleCount, uint32_t tstamp)
{
   for (uint32_t i = 0; i < activeParticleCount; ++i) {
      const float r = particles[i].radius + sim::CellDimension / 2.0f;
      const float angle = sim::CellDimension / r;
      for (float ai = angle * 0.5f; ai < 2.0f * std::numbers::pi; ai += angle) {
         const float outerPointX = particles[i].center.x + r * cosf(ai);
         const float outerPointY = particles[i].center.y + r * sinf(ai);
         int32_t gridX = (outerPointX + 1.0f) / sim::CellDimension;
         gridX = std::clamp(gridX, 0, static_cast<int32_t>(sim::GridDimension - 1));
         int32_t gridY = (outerPointY + 1.0f) / sim::CellDimension;
         gridY = std::clamp(gridY, 0, static_cast<int32_t>(sim::GridDimension - 1));
         Cell& cell = grid[gridX + gridY * sim::GridDimension];
         if (cell.timestamp >= tstamp) {
            for (uint32_t idI = 0; idI < cell.idCount; ++idI) {
               const uint32_t j = cell.particleIds[idI];
               if (i >= j)
                  continue;
               if (checkCollision(particles[i], particles[j])) {
                  adjustPositions(particles[i], particles[j]);
                  resolveCollision(particles[i], particles[j]);
               }
            }
         }
      }
   }
}

bool cpu::physics::checkCollision(const Particle& particleA, const Particle& particleB)
{
   const glm::vec2 delta = particleB.center - particleA.center;
   const float radiiSum = particleA.radius + particleB.radius; 
   return glm::dot(delta, delta) < radiiSum * radiiSum;
}

void cpu::physics::adjustPositions(Particle& particleA, Particle& particleB)
{
   const glm::vec2 delta = particleB.center - particleA.center;
   const float distance = glm::length(delta);
   const float penetrationDepth = particleA.radius + particleB.radius - distance;
   if (penetrationDepth <= 0.0f)
      return;
   const glm::vec2 impactNormalA = glm::normalize(delta);
   particleA.center += -impactNormalA * penetrationDepth / 2.0f;
   particleB.center += impactNormalA * penetrationDepth / 2.0f;
}

void cpu::physics::resolveCollision(Particle& particleA, Particle& particleB)
{
   const glm::vec2 delta = particleB.center - particleA.center;
   const glm::vec2 impactNormalA = glm::normalize(delta);
   const glm::vec2 relativeVelocity = particleB.velocity - particleA.velocity;
   const float normalSpeed = glm::dot(relativeVelocity, impactNormalA);
   if (normalSpeed > 0.0f)
      return;
   const float impulse = (-(1.0f + sim::Elasticity) * normalSpeed) / 2.0f;
   particleA.velocity -= impulse * impactNormalA;
   particleB.velocity += impulse * impactNormalA;
}

void cpu::physics::resolveBoundaryCollisions(Particle& particle)
{
   // bottom
   if (particle.center.y + particle.radius > 1.0f) {
      particle.velocity.y *= -sim::Elasticity;
      particle.center.y = 1.0f - particle.radius;
   }
   // left side
   if (particle.center.x - particle.radius < -1.0f) {
      particle.velocity.x *= -sim::Elasticity;
      particle.center.x = -1.0f + particle.radius;
   }
   // right side
   if (particle.center.x + particle.radius > 1.0f) {
      particle.velocity.x *= -sim::Elasticity;
      particle.center.x = 1.0f - particle.radius;
   }
}