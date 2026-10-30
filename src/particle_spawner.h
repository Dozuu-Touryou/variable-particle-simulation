#pragma once

#include<vector>

#include "particle.h"

using std::vector;

class ParticleSpawner {
public:
   bool initializeParticles(vector<Particle>&);
   void initializeWaveTimer() { waveInterval = waveTimer; }
   void spawnWave(vector<Particle>&);
   void updateWaveData(const vector<Particle>&);
   uint32_t getSpawnedParticles() const { return spawnedParticles; }
private:
   static constexpr float MinRadius{ 0.0075f };
   static constexpr float MaxRadius{ 0.15f };

   uint32_t particlesPerWave = 0;
   float waveInterval = 0.0f;
   float waveTimer = 0.0f;
   uint32_t wavesSpawned = 0;
   uint32_t spawnedParticles = 0;
   float widthUsed = 0.0f;
};

float calculateCircleArea(float);