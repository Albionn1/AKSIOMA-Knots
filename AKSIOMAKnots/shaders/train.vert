
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aOcclusion;
layout(location = 3) in float aMaterial;

uniform mat4 uViewProjection;
uniform mat4 uVehicle;
uniform float uScale;          // view units per millimetre

out vec3 vWorld;
out vec3 vMm;
out vec3 vNormal;
out float vOpen;
flat out int vMaterial;

void main()
{
    vMm = aPosition;
    vWorld = (uVehicle * vec4(aPosition, 1.0)).xyz * uScale;
    vNormal = mat3(uVehicle) * aNormal;
    vOpen = aOcclusion;
    vMaterial = int(aMaterial + 0.5);

    gl_Position = uViewProjection * vec4(vWorld, 1.0);
}
