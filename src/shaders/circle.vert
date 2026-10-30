#version 460

const vec3 positions[4] = vec3[4](
   vec3(1.0, -1.0, 0.0),	// Top right
   vec3(-1.0, -1.0, 0.0),	// Top left
   vec3(1.0, 1.0, 0.0),		// Bottom right
   vec3(-1.0, 1.0, 0.0)		// Bottom left
);

layout (location = 0) in vec2 inCenter;
layout (location = 1) in float inRadius;

layout (location = 0) out ParticleData {
   vec2 center;
   float radius;
   vec2 fragPosition;
} outData;

void main()
{
   outData.center = inCenter;
   outData.radius = inRadius;
   vec4 scaledPosition = vec4((positions[gl_VertexIndex].xy * inRadius) + inCenter, 0.0, 1.0);
   gl_Position = scaledPosition;
   outData.fragPosition = scaledPosition.xy;
}