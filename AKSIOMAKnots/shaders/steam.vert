
#version 330 core

uniform mat4 uViewProjection;
uniform vec3 uCentre;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uSize;

out vec2 vCorner;
out vec3 vWorld;

void main()
{
    vCorner = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1) * 2.0 - 1.0;

    vWorld = uCentre + (uRight * vCorner.x + uUp * vCorner.y) * uSize;

    gl_Position = uViewProjection * vec4(vWorld, 1.0);
}
