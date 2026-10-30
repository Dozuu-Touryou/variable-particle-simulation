#include <random>
#include <numbers>

#include <glm/glm.hpp>

#include "particle_spawner.h"
#include "simulation_constants.h"

using DistributionParamType = std::uniform_real_distribution<float>::param_type;

// Spawn particles of random size somewhere off-screen using a dynamic upper bound on radius size.
// Stop spawning when it becomes impossible to fill remaining area with particles with the smallest radius
bool ParticleSpawner::initializeParticles(vector<Particle>& particles)
{
   constexpr int seed = 72412;
   std::mt19937 randNumGenerator(seed);
   std::uniform_real_distribution<float> radiusDistribution;
   float totalParticleArea = 0.0f;
   for (uint32_t i = 0; i < particles.size(); ++i) {
      const float remainingArea = sim::FillableArea - totalParticleArea;
      const float remainingParticles = particles.size() - i;
      if (remainingArea < remainingParticles * calculateCircleArea(MinRadius))
         return false;
      float targetParticleArea = remainingArea / remainingParticles;
      const float dynamicMaxRadius = sqrtf(targetParticleArea / std::numbers::pi);
      const float upperBound = std::clamp(dynamicMaxRadius, MinRadius, MaxRadius);
      radiusDistribution.param(DistributionParamType(MinRadius, upperBound));
      float radius = radiusDistribution(randNumGenerator);
      totalParticleArea += calculateCircleArea(radius);
      particles[i] = Particle{
         .center = glm::vec2(5.0f),
         .velocity = glm::vec2(0.0f),
         .radius = radius
      };
   }
   if (MinRadius != MaxRadius)
      std::shuffle(particles.begin(), particles.end(), randNumGenerator);
   return true;
}

// Calculate the horizontal space occupied by next wave and the time interval before it spawns
// Note: time imterval does not currently take the influence of the velocity constant damper into account
void ParticleSpawner::updateWaveData(const vector<Particle>& particles)
{
   if (spawnedParticles >= particles.size())
      return;
   particlesPerWave = 0;
   float waveMaxDiameter = 0.0f;
   widthUsed = 0.0f;
   const uint32_t availableParticles = particles.size() - spawnedParticles;
   const bool isUniformSize = (MinRadius == MaxRadius);
   const bool isOddWave = (wavesSpawned % 2 != 0);
   for (uint32_t i = 0; i < availableParticles; ++i) {
      const float diameter = particles[spawnedParticles + i].radius * 2.0f;
      if (isUniformSize && isOddWave && particlesPerWave >= availableParticles - 1)
         break;
      if (widthUsed + diameter + MinRadius > sim::DimensionSize) {
         if (particlesPerWave == 0) {
            particlesPerWave = 1;
            widthUsed = diameter + MinRadius;
            waveMaxDiameter = diameter;
         }
         break;
      }
      widthUsed += diameter + MinRadius;
      waveMaxDiameter = std::max(waveMaxDiameter, diameter);
      particlesPerWave++;
   }
   if (particlesPerWave > 0)
      widthUsed -= MinRadius;
   waveInterval = (sim::Gravity.y > 0.0f) ? sqrtf((2.0f * waveMaxDiameter) / sim::Gravity.y) : 0.5f;
}

// Spawn a single wave of particles,
// if there are unspawned particles and the time interval since the last wave has passed
void ParticleSpawner::spawnWave(vector<Particle>& particles)
{
   waveTimer += sim::TimeStep;
   if (spawnedParticles >= particles.size() || waveTimer < waveInterval)
      return;
   waveTimer = 0.0f;
   const float extraSpace = sim::DimensionSize - widthUsed;
   float startX = -1.0f + (extraSpace * 0.5f);
   if (MinRadius == MaxRadius && (wavesSpawned % 2 != 0))
      startX += (MinRadius * 0.5f);
   float posX = startX;
   for (uint32_t i = 0; i < particlesPerWave; ++i) {
      const size_t pId = spawnedParticles + i;
      const float r = particles[pId].radius;
      particles[pId].center = glm::vec2(posX + r, -1.0f + r);
      posX += (r * 2.0f) + MinRadius;
   }
   spawnedParticles += particlesPerWave;
   wavesSpawned++;
   updateWaveData(particles);
}

float calculateCircleArea(float r)
{
   return r >= 0.0f ? std::numbers::pi * r * r : 0.0f;
}