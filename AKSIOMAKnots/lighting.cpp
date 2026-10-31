#include "lighting.h"

#include <algorithm>

namespace Lighting
{

bool Options::operator==(const Options &other) const
{
    return keyLight == other.keyLight &&
           fillLight == other.fillLight &&
           rimLight == other.rimLight &&
           reflections == other.reflections &&
           followCamera == other.followCamera &&
           knotShadows == other.knotShadows &&
           floorShadow == other.floorShadow &&
           contactShadow == other.contactShadow &&
           occlusion == other.occlusion &&
           floorGrid == other.floorGrid &&
           brightness == other.brightness &&
           shadowSoftness == other.shadowSoftness &&
           shadowQuality == other.shadowQuality;
}

bool Options::operator!=(const Options &other) const
{
    return !(*this == other);
}

Options preset(
    Preset which,
    int shadowQuality)
{
    Options options;

    switch (which)
    {
    case Preset::Studio:
    case Preset::Custom:
        break;

    case Preset::Soft:
        // gentle light, wide shadow edges
        options.rimLight = false;
        options.brightness = 110;
        options.shadowSoftness = 300;
        break;

    case Preset::Dramatic:
        // no fill: deep, crisp shadows
        options.fillLight = false;
        options.brightness = 90;
        options.shadowSoftness = 50;
        break;

    case Preset::Flat:
        // even light: clear on paper and on projectors
        options.rimLight = false;
        options.reflections = false;
        options.knotShadows = false;
        options.floorShadow = false;
        options.occlusion = false;
        options.brightness = 110;
        break;
    }

    options.shadowQuality = shadowQuality;

    return options;
}

Preset presetOf(
    const Options &options)
{
    for (int i = 0; i < static_cast<int>(Preset::Custom); ++i)
    {
        const Preset candidate = static_cast<Preset>(i);

        if (preset(candidate, options.shadowQuality) == options)
            return candidate;
    }

    return Preset::Custom;
}

PhotoLevel photoLevel(
    int level)
{
    PhotoLevel values;

    if (level == 2)
    {
        values.scale = 0.85f;
        values.samples = 2;
        values.shadowMap = 2048;
        values.shadowTaps = 8;
        values.shadeTaps = 8;
        values.lensTaps = 24;
    }
    else if (level >= 3)
    {
        values.scale = 0.7f;
        values.samples = 0;
        values.shadowMap = 2048;
        values.shadowTaps = 8;
        values.shadeTaps = 0;
        values.lensTaps = 12;
        values.mirror = false;
    }

    return values;
}

int shadowMapSize(
    int quality)
{
    // 4, 16 or 64 MB
    return 1024 << std::clamp(quality, 0, 2);
}

} // namespace Lighting
