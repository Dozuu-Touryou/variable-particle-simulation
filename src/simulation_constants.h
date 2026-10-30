#pragma once

#include<glm/glm.hpp>

namespace sim {
   inline constexpr uint32_t ParticleCount{ 8000 };
   inline constexpr glm::vec2 Gravity{ 0.0f, 0.981f };
   inline constexpr float TimeStep = 1.0f / 120.0f;
   inline constexpr float MaxFillFraction = 0.77f;
   inline constexpr float DimensionSize = 2.0f;
   inline constexpr float SimulationArea = DimensionSize * DimensionSize;
   inline constexpr float FillableArea = MaxFillFraction * SimulationArea;
   inline constexpr uint32_t MaxParticlesPerCell = 32;
   inline constexpr uint32_t GridDimension = 128;
   inline constexpr uint32_t CellCount = GridDimension * GridDimension;
   inline constexpr float CellDimension = DimensionSize / static_cast<float>(GridDimension);
   inline constexpr float Elasticity = 1.0f;
   inline constexpr float VelocityDamper = 0.99f;
   inline constexpr uint32_t WorkGroupSize = 256;
};