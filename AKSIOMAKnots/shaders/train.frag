#version 330 core
#include "common/photo.glsl"

in vec3 vWorld;
in vec3 vMm;
in vec3 vNormal;
in float vOpen;
flat in int vMaterial;

uniform vec3 uCameraPosition;
uniform float uReflections;
uniform float uMirrorLevel;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 mirrorData;

float ggx(float nh, float a)
{
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;

    return a2 / (PhotoPi * d * d);
}

float smith(float nv, float nl, float a)
{
    float k = a * 0.5;

    return 0.25 / ((nv * (1.0 - k) + k) * (nl * (1.0 - k) + k));
}

void main()
{
    mirrorData = vec2(1.0, vWorld.y - uMirrorLevel);

    vec3 geometric = normalize(vNormal);
    vec3 n = geometric;

    vec3 v = normalize(uCameraPosition - vWorld);

    if (dot(n, v) < 0.0)
        n = -n;

    // the grain of the wood runs along the toy; under the paint it is only
    // just there, a faint ripple in the reflections
    float grainAt = vMm.x * 0.05;

    float grain = 0.55 * noise2(vec2(grainAt, (vMm.y + vMm.z) * 0.9)) +
                  0.3 * noise2(vec2(vMm.x * 0.3, (vMm.y + vMm.z) * 2.3)) +
                  0.15 * noise2(vec2(vMm.x * 0.9, (vMm.y + vMm.z) * 5.3));

    vec3 albedo;

    // the rough surface under the lacquer, how much lacquer lies on it
    float rough = 0.5;
    float coat = 0.45;
    float metal = 0.0;
    vec3 glow = vec3(0.0);

    if (vMaterial == 0)
        albedo = vec3(0.21, 0.012, 0.016);
    else if (vMaterial == 1)
        albedo = vec3(0.012, 0.032, 0.085);
    else if (vMaterial == 2)
        albedo = vec3(0.34, 0.255, 0.16);
    else if (vMaterial == 6)
        albedo = vec3(0.012, 0.075, 0.042);
    else if (vMaterial == 3)
    {
        // plain beech, oiled: darker bands, and a little lacquer
        float bands = 0.5 + 0.5 * sin((vMm.y + vMm.z) * 2.1 + 5.0 * noise2(vec2(vMm.x * 0.03, vMm.z * 0.2)));

        albedo = mix(vec3(0.72, 0.47, 0.25), vec3(0.47, 0.27, 0.13), 0.55 * bands + 0.45 * (1.0 - grain));
        rough = 0.5;
        coat = 0.35;
    }
    else if (vMaterial == 4)
    {
        albedo = vec3(0.008, 0.008, 0.010);
        rough = 0.35;
    }
    else if (vMaterial == 5)
    {
        // polished brass, handled a little
        albedo = vec3(0.95, 0.70, 0.30);
        rough = 0.2 + 0.14 * grain;
        coat = 0.0;
        metal = 1.0;
    }
    else if (vMaterial == 8)
    {
        // burnished steel
        albedo = vec3(0.55, 0.56, 0.58);
        rough = 0.3;
        coat = 0.0;
        metal = 1.0;
    }
    else if (vMaterial == 9)
    {
        // the lamp: lit
        albedo = vec3(0.6, 0.55, 0.45);
        glow = uLampCount > 0 ? vec3(8.0, 5.4, 2.4) : vec3(0.8, 0.6, 0.3);
        rough = 0.1;
    }
    else
    {
        // a window: dark glass by day, a warm room inside it at night
        albedo = vec3(0.004, 0.007, 0.012);
        rough = 0.05;

        if (uLampCount > 0)
            glow = vec3(1.7, 0.95, 0.34) * (0.7 + 0.5 * noise2(vMm.xy * 0.2));
    }

    // road dirt, thickest low down, and soot from the funnel along the top
    if (vMaterial != 5 && vMaterial != 7 && vMaterial != 9)
    {
        float grime = noise2(vec2(vMm.x * 0.12, vMm.y * 0.5 + vMm.z * 0.2)) * 0.6 + grain * 0.4;

        float low = 1.0 - smoothstep(6.0, 24.0, vMm.y + 12.0 * (grime - 0.5));

        albedo = mix(albedo, vec3(0.05, 0.042, 0.034), 0.65 * low);
        albedo *= 0.8 + 0.3 * grime;

        rough = mix(rough, 0.82, 0.8 * low);
        coat *= 1.0 - 0.8 * low;
    }

    float nv = max(dot(n, v), 1.0e-3);

    vec3 l = uKeyDir;
    float nl = max(dot(n, l), 0.0);

    float sunLit = keyShadow(vWorld, geometric);

    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diffuse = albedo * (1.0 - metal);

    // the sun, on the surface
    vec3 h = normalize(l + v);
    float nh = max(dot(n, h), 0.0);
    float vh = max(dot(h, v), 0.0);
    float a = max(rough * rough, 0.02);

    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    vec3 specular = fresnel * ggx(nh, a) * smith(nv, nl, a);

    vec3 color = (diffuse / PhotoPi + specular) * uKeyIrradiance * nl * sunLit;

    /*
     * The lacquer over it: a thin clear layer, smooth as glass. It takes its
     * share of the light off the top, and gives the sharp bright highlight
     * and the mirror image of the garden that make paint look deep.
     */
    float coatF = coat * (0.04 + 0.96 * pow(1.0 - nv, 5.0));

    // a ripple of the grain under the lacquer bends its reflection a little
    vec3 coatN = normalize(n + (vec3(0.0, 1.0, 0.0) - n * n.y) * (grain - 0.5) * 0.02);

    vec3 ch = normalize(l + v);
    float coatA = 0.012;

    color += coat * (0.04 + 0.96 * pow(1.0 - vh, 5.0)) * ggx(max(dot(coatN, ch), 0.0), coatA) *
             smith(nv, nl, coatA) * uKeyIrradiance * nl * sunLit;

    float open = vOpen * shadeHere();

    // the room
    color += diffuse * roomIrradiance(n) * open / PhotoPi;

    // what lies under it is lit too: the sunny floor sends light back up
    color += diffuse * uFloorBounce * (0.5 - 0.5 * n.y) * shadeHere() * 0.5 / PhotoPi;

    vec3 r = reflect(-v, n);

    color += roomSpecular(r, rough) * roomReflectance(f0, rough, nv) * open * uReflections * (1.0 - coatF);

    color += roomSpecular(reflect(-v, coatN), 0.06) * coatF * open * uReflections;

    color += lampLight(vWorld, n, v, diffuse, f0, rough) + lampLight(vWorld, coatN, v, vec3(0.0), vec3(0.04 * coat), 0.08);

    color = color * (1.0 - coatF * 0.5) + glow * (uInMirror != 0 ? 0.15 : 1.0);

    fragColor = vec4(cleanLight(color), 1.0);
}
