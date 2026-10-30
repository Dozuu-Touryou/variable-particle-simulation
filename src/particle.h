#pragma once

#include <glm/glm.hpp>

struct Particle {
   glm::vec2 center{};
   glm::vec2 velocity{};
   float radius = 0.1f;
   float padding = 0.0f;
};

struct ParticleInfo {
   static constexpr uint32_t inputMemberCount = 2;
};

// make sure Particle struct matches expected data-layout of GLSL counterpart
static_assert(sizeof(Particle) == 24);
static_assert(offsetof(Particle, center) == 0);
static_assert(offsetof(Particle, velocity) == 8);
static_assert(offsetof(Particle, radius) == 16);