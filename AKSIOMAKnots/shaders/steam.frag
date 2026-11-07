#version 330 core
#include "common/photo.glsl"

in vec2 vCorner;
in vec3 vWorld;

uniform vec3 uCentre;
uniform float uThickness;
uniform float uSeed;
uniform vec3 uHalo;

layout(location = 0) out vec4 fragColor;

void main()
{
    float r = length(vCorner);

    if (r >= 1.0)
        discard;

    // the glow around a light, in the eye and the lens: it only adds
    if (uHalo.r + uHalo.g + uHalo.b > 0.0)
    {
        float g = (1.0 - r) * (1.0 - r);

        fragColor = vec4(uHalo * g * (0.1 * exp(-3.0 * r) + 0.9 * exp(-22.0 * r * r)), 0.0);

        return;
    }

    vec2 p = vCorner * 2.1 + uSeed;

    float billow = 0.55 * noise2(p) + 0.3 * noise2(p * 2.3 + 4.1) + 0.15 * noise2(p * 5.1 + 9.7);

    float edge = 1.0 - smoothstep(0.2 + 0.5 * billow, 1.0, r);

    float a = clamp(edge * uThickness * (0.45 + 1.1 * billow), 0.0, 1.0);

    if (a <= 0.002)
        discard;

    // white vapour: it scatters what falls on it
    float sunLit = keyShadow(uCentre, uKeyDir);

    vec3 light = (uKeyIrradiance * sunLit * (0.55 + 0.45 * vCorner.y) * 0.6 + roomIrradiance(vec3(0.0, 1.0, 0.0))) * 0.9 / PhotoPi;

    light += lampAir(uCentre) * 1.1 / PhotoPi;

    light *= 0.78 + 0.34 * billow;

    fragColor = vec4(cleanLight(light) * a, a);
}
