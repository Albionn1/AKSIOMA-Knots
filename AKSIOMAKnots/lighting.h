#ifndef LIGHTING_H
#define LIGHTING_H

/*
 * How the 3D view is lit: the lights and effects that can be turned on and
 * off (Settings, Lighting), and the presets that set them all at once.
 */
namespace Lighting
{

struct Options
{
    // the studio lights: key high on the left, fill low on the right, rim
    // behind the knot
    bool keyLight = true;
    bool fillLight = true;
    bool rimLight = true;

    // the strip lights and soft boxes seen in shiny surfaces
    bool reflections = true;

    // the lights turn with the camera, so the knot is always lit from the
    // side; off: they stay in place while the camera moves
    bool followCamera = true;

    // shadows of the key light on the knot itself and on the floor, and
    // the soft contact shadow right under the knot
    bool knotShadows = true;
    bool floorShadow = true;
    bool contactShadow = true;

    // darker where strands come close
    bool occlusion = true;

    bool floorGrid = true;

    // percent of the normal
    int brightness = 100;
    int shadowSoftness = 100;

    // 0 low, 1 medium, 2 high: the size of the shadow map (memory)
    int shadowQuality = 1;

    bool operator==(const Options &other) const;
    bool operator!=(const Options &other) const;
};

enum class Preset
{
    Studio = 0,
    Soft,
    Dramatic,
    Flat,

    // none of the above
    Custom
};

// Options of a preset (Custom: the defaults). The shadow quality is not
// part of the look: a preset keeps the quality it is given.
Options preset(
    Preset which,
    int shadowQuality = 1);

// The preset the options are (Custom when none), the quality aside.
Preset presetOf(
    const Options &options);

// Side of the shadow map in texels for a quality.
int shadowMapSize(
    int quality);

} // namespace Lighting

#endif // LIGHTING_H
