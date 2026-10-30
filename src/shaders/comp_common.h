#define PI 3.141592653589793

layout (constant_id = 0) const uint MaxParticlesPerCell = 32;
layout (constant_id = 1) const uint GridDimension = 128;
layout (constant_id = 4) const float CellDimension = 0.015625;

struct Particle {
   vec2 center;
   vec2 velocity;
   float radius;
};

struct Cell {
   uint particleIds[MaxParticlesPerCell];
   uint idCount;
   uint timestamp;
};

layout (std430, binding = 0) buffer ParticleBuffer {
   Particle particles[];
};

layout (std430, binding = 1) buffer GridBuffer {
   Cell grid[];
};

layout (push_constant) uniform SimulationInfo {
   uint activeParticleCount;
};