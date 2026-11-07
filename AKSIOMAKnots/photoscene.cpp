#include "photoscene.h"

#include <QStringList>

namespace Photo
{

void preloadScene(const SceneInfo &info)
{
    Aksioma::preloadEnvironment(info.environment, {info.surfaceColor, info.surfaceNormal, info.surfaceRoughness});
}

Aksioma::PhotoStage stageOf(const SceneInfo &info)
{
    Aksioma::PhotoStage stage;

    stage.environment = info.environment;
    stage.cameraHeightM = info.cameraHeightM;
    stage.roomRadiusM = info.roomRadiusM;
    stage.surfaceColor = info.surfaceColor;
    stage.surfaceNormal = info.surfaceNormal;
    stage.surfaceRoughness = info.surfaceRoughness;
    stage.surfaceTileMm = info.surfaceTileMm;
    stage.tableWidthMm = info.tableWidthMm;
    stage.tableDepthMm = info.tableDepthMm;
    stage.tableThicknessMm = info.tableThicknessMm;
    stage.tableHeightM = info.tableHeightM;
    stage.environmentYaw = info.environmentYaw;
    stage.exposure = info.exposure;
    stage.calmStage = info.calmStage;
    stage.stoneFloor = info.stoneFloor;
    stage.stoneLight = info.stoneLight;
    stage.evening = info.evening;
    stage.sideLightDirection = info.sideLightDirection;
    stage.sideLight = info.sideLight;
    stage.sideLightWidth = info.sideLightWidth;

    // laid rope, a shoelace, or the toy train's wooden track (the app's own
    // material: shaders/track_material.glsl)
    stage.tube.material = info.rope.construction == 2   ? Aksioma::PhotoStage::Tube::BraidedLace
                          : info.rope.construction == 3 ? Aksioma::PhotoStage::Tube::FirstAppMaterial
                                                        : Aksioma::PhotoStage::Tube::LaidRope;
    stage.tube.diameterMm = info.rope.diameterMm;
    stage.tube.color = info.rope.color;
    stage.tube.roughness = info.rope.roughness;

    return stage;
}

SceneInfo sceneInfo(Scene scene)
{
    SceneInfo info;

    switch (scene)
    {
    case Scene::TrainTrack:
    case Scene::TrainDay:
    {
        const bool evening = scene == Scene::TrainTrack;

        // the wood of the track, whichever the place
        info.surfaceColor = QStringLiteral(":/data/photo/wood_table_large_diff.jpg");
        info.surfaceNormal = QStringLiteral(":/data/photo/wood_table_large_nor_gl.jpg");
        info.surfaceRoughness = QStringLiteral(":/data/photo/wood_table_large_rough.jpg");
        info.surfaceTileMm = 1550.0;

        info.stoneFloor = true;

        // the scale of the scene: the track is 40 mm wide, as toy track is
        info.rope.construction = 3;
        info.rope.diameterMm = 40.0;
        info.rope.color = QColor(214, 170, 120);
        info.rope.roughness = 0.5f;

        info.train = true;
        info.calmStage = 0.1f;
        info.cameraHeightM = 1.6;

        if (evening)
        {
            // a museum's courtyard after dark: a deep blue sky, wide paving,
            // and the lit windows along one side, low and warm
            info.environment = QStringLiteral(":/data/photo/evening_museum_courtyard_4k.hdr");
            info.roomRadiusM = 22.0;
            info.exposure = 0.8f;

            info.evening = true;
            info.sideLightDirection = QVector3D(-0.87f, 0.27f, -0.48f).normalized();
            info.sideLight = QVector3D(30.0f, 19.0f, 10.5f);
            info.stoneLight = 3.2f;
            info.sideLightWidth = 0.045f;
        }
        else
        {
            // the garden's stone paving, wide enough for a long track
            info.environment = QStringLiteral(":/data/photo/chinese_garden_4k.hdr");
            info.roomRadiusM = 16.0;
            info.exposure = 3.0f;
        }

        break;
    }

    case Scene::BallroomFloor:
        // a Victorian ballroom in the morning sun: tall windows, a worn
        // board floor
        info.environment = QStringLiteral(":/data/photo/meadow_2_4k.hdr");
        info.cameraHeightM = 1.6;
        info.roomRadiusM = 12.0;
        info.exposure = 2.6f;
        info.calmStage = 0.1f;

        info.surfaceColor = QStringLiteral(":/data/photo/wood_floor_worn_diff.jpg");
        info.surfaceNormal = QStringLiteral(":/data/photo/wood_floor_worn_nor_gl.jpg");
        info.surfaceRoughness = QStringLiteral(":/data/photo/wood_floor_worn_rough.jpg");
        info.surfaceTileMm = 2000.0;

        // a round braided cotton lace, 5 mm: it bends under its own weight
        // within a couple of centimetres
        info.rope.construction = 2;
        info.rope.diameterMm = 5.0;
        info.rope.bendingLengthMm = 20.0;
        info.rope.tiedSet = 0.3;
        info.rope.twistStiffness = 0.5;
        info.rope.setRadiusMm = 14.0;
        info.rope.setRate = 0.3;
        info.rope.curlRadiusMm = 70.0;
        info.rope.frictionGround = 0.4;
        info.rope.frictionSelf = 0.5;
        info.rope.color = QColor(236, 232, 224);
        info.rope.roughness = 0.8f;
        break;

    case Scene::LoungeTable:
    case Scene::Count:
    default:
        // a ship's lounge at sunrise: the low sun through the windows
        info.environment = QStringLiteral(":/data/photo/chinese_garden_4k.hdr");
        info.cameraHeightM = 1.6;
        info.roomRadiusM = 16.0;
        info.exposure = 3.0f;
        info.calmStage = 0.1f;

        // an old plank table, 1.8 m x 0.9 m
        info.surfaceColor = QStringLiteral(":/data/photo/wood_table_large_diff.jpg");
        info.surfaceNormal = QStringLiteral(":/data/photo/wood_table_large_nor_gl.jpg");
        info.surfaceRoughness = QStringLiteral(":/data/photo/wood_table_large_rough.jpg");
        info.surfaceTileMm = 1550.0;
        info.tableWidthMm = 1800.0;
        info.tableDepthMm = 900.0;
        info.tableThicknessMm = 45.0;
        info.tableHeightM = 0.75;

        // three-strand hemp rope, 12 mm: stiffer, it bends under its own
        // weight over some 5 cm
        info.rope.construction = 1;
        info.rope.diameterMm = 12.0;
        info.rope.bendingLengthMm = 50.0;
        info.rope.frictionGround = 0.5;
        info.rope.frictionSelf = 0.7;
        info.rope.color = QColor(184, 152, 104);
        info.rope.roughness = 0.75f;
        break;
    }

    return info;
}

} // namespace Photo
