#version 460

layout (location = 0) in ParticleData {
   vec2 center;
   float radius;
   vec2 fragPosition;
};

layout (location = 0) out vec4 fragColor;

void main()
{
    const float outlineThickness = 0.0025;
    const vec2 offset = fragPosition - center;
    const float fragDistanceSq = dot(offset, offset);
    const float radiusSq = radius * radius;
    const float innerRadiusSq = (radius - outlineThickness) * (radius - outlineThickness);
   if (fragDistanceSq < radiusSq && fragDistanceSq > innerRadiusSq)
      fragColor = vec4(1.0);
   else 
      discard;
}