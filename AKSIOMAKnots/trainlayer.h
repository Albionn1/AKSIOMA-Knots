#ifndef TRAINLAYER_H
#define TRAINLAYER_H

#include "knotmath.h"
#include "traintrack.h"
#include <aksioma/layer.h>
#include <aksioma/renderer.h>

#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

#include <functional>
#include <memory>

/*
 * The Toy train scene of Photo mode (traintrack.h), as a layer of the 3D view:
 * the wooden train on the knot's track, the steam from its funnel, its lamps
 * at dusk, and Cinema, the camera that films it by itself.
 */
class TrainLayer final
    : public Aksioma::Layer
    , protected QOpenGLExtraFunctions
{
public:
    explicit TrainLayer(Aksioma::Renderer &view);

    // the scene of Photo mode has a train (Photo::SceneInfo::train)
    void setSceneHasTrain(bool has);

    // the train is on the screen
    bool showTrain() const;

    // KnotGLWidget's train (see there)
    void setTrainPath(const QVector<KnotMath::Vec3> &path, double length);
    void setTrainJourney(const QVector<Photo::TrainTrack::Pass> &journey);
    void trainSoundScene(double *speed, float *gain, float *pan) const;
    void setCinema(bool on);
    void playCinema(float seconds);
    bool cinema() const;
    void setTrainRunning(bool running);
    bool trainRunning() const;
    void setTrainAlong(double millimetres);
    void runSteam(float seconds);
    double trainAlong() const;

    // the track's length (mm) and the bridges in the order the train meets them
    double trainLength() const;
    const QVector<Photo::TrainTrack::Pass> &journey() const;

    // Cinema turned on or off
    std::function<void(bool)> cinemaChanged;

    // Aksioma::Layer
    bool shown(const Aksioma::Renderer &view) const override;
    void initializePhotoGL(Aksioma::Renderer &view) override;
    void releaseGL(Aksioma::Renderer &view) override;
    void photoSceneLoaded(Aksioma::Renderer &view) override;
    void photoSceneReleased(Aksioma::Renderer &view) override;
    void advance(Aksioma::Renderer &view) override;
    bool animating(const Aksioma::Renderer &view) const override;
    void extendBounds(const Aksioma::Renderer &view, QVector3D &minimum, QVector3D &maximum) const override;
    void drawDepth(Aksioma::Renderer &view, QOpenGLShaderProgram &program, const char *matrix,
                   const QMatrix4x4 &viewProjection) override;
    void drawOpaque(Aksioma::Renderer &view, const QMatrix4x4 &viewProjection) override;
    void drawTransparent(Aksioma::Renderer &view, const QMatrix4x4 &viewProjection, const QMatrix4x4 &camera) override;
    void photoLights(const Aksioma::Renderer &view, Aksioma::LayerLights &lights) const override;
    float exposureLift(const Aksioma::Renderer &view) const override;
    float cameraReach(const Aksioma::Renderer &view) const override;
    bool frameCamera(Aksioma::Renderer &view, bool animate) override;
    void cameraTaken(Aksioma::Renderer &view) override;
    qint64 videoMemory() const override;

private:
    // the train onto the card (with the scene), and off again
    void uploadTrain();
    void destroyTrain();

    // on along the track by the time since the last frame, and every
    // vehicle in its place
    void advanceTrain();

    // draws the vehicles: for the depth passes (matrix: the program's
    // uniform, given viewProjection * scale * place) or the colour pass
    void drawTrainVehicles(QOpenGLShaderProgram &program, const char *matrix, const QMatrix4x4 &viewProjection);

    void drawTrain(const QMatrix4x4 &viewProjection);

    void directCinema(float seconds);

    // how far back, and looking at what, the whole track is in the film's
    // picture; toEye is the way to the camera, of length 1
    float wholeKnotDistance(const QVector3D &toEye, QVector3D *target) const;

    // the next bridge the train will go over, a little way off yet
    void chooseCinemaBridge(float shotSeconds);

    double trainSpeed() const;

    // the lights of this frame, in the view's units
    void updateLamps();

    // the puffs rise and spread; while `blowing`, the funnel adds new ones
    void stepSteam(float seconds, bool blowing);

    void drawSteam(const QMatrix4x4 &viewProjection, const QMatrix4x4 &view);

    Aksioma::Renderer &view_;

    bool sceneHasTrain_ = false;
    qint64 bytes_ = 0;

    // the toy train: its model (millimetres), its track (Photo::TrainTrack),
    // how far along it the locomotive is, and where each vehicle stands
    std::unique_ptr<QOpenGLShaderProgram> trainProgram_;
    QOpenGLVertexArrayObject trainVao_;
    QOpenGLBuffer trainBuffer_;
    QOpenGLBuffer trainIndexBuffer_;
    int trainIndexCount_ = 0;

    QVector<KnotMath::Vec3> trainPath_;
    double trainLength_ = 0.0;
    double trainAlong_ = 0.0;
    bool trainRunning_ = true;
    Aksioma::Stopwatch trainClock_;
    QVector<Photo::TrainPiece> trainPieces_;

    // the steam: where each puff is (mm), how old, and which way it drifts
    struct SteamPuff
    {
        QVector3D place;
        QVector3D drift;
        float age = 0.0f;
        float seed = 0.0f;
    };

    QVector<Photo::TrainTrack::Pass> trainJourney_;

    bool cinema_ = false;
    float cinemaTime_ = 0.0f;
    int cinemaShot_ = -1;
    QVector3D cinemaSpot_;
    QVector3D cinemaAhead_;
    QVector3D cinemaSide_;

    QVector<QVector3D> lampPlaces_;
    QVector<QVector3D> lampColors_;
    float lampReach_ = 1.0f;
    QVector3D headPlace_;
    QVector3D headWay_;

    std::unique_ptr<QOpenGLShaderProgram> steamProgram_;
    QVector<SteamPuff> steam_;
    Aksioma::Stopwatch steamClock_;
    float steamWait_ = 0.0f;
    float steamSeed_ = 0.0f;
    bool steamFrozen_ = false;
};

#endif // TRAINLAYER_H
