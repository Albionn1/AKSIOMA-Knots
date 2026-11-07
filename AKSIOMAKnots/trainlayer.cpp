#include "trainlayer.h"


#include <aksioma/shaderlibrary.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

/*
 * The time since the last look, at most a tenth of a second (a frame that came
 * late does not make things jump), and 0 at the first. In nanoseconds: a frame
 * can take less than a millisecond, and whole milliseconds would add up to
 * nothing, so that what fades away would never be gone.
 */
// (the renderer's clock: a recording steps it frame by frame)
double lapSeconds(Aksioma::Stopwatch &clock)
{
    if (!clock.isValid())
    {
        clock.start();

        return 0.0;
    }

    const double seconds = static_cast<double>(clock.nsecsElapsed()) / 1.0e9;

    clock.restart();

    return std::min(seconds, 0.1);
}

float smoothstepUp(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);

    return t * t * (3.0f - 2.0f * t);
}

} // namespace

namespace
{

QByteArray shaderSource(const char *path)
{
    return Aksioma::ShaderLibrary::instance().source(QString::fromLatin1(path));
}

} // namespace

TrainLayer::TrainLayer(Aksioma::Renderer &view)
    : view_(view),
      trainBuffer_(QOpenGLBuffer::VertexBuffer),
      trainIndexBuffer_(QOpenGLBuffer::IndexBuffer),
      trainClock_(view.clock()),
      steamClock_(view.clock())
{
}

void TrainLayer::setSceneHasTrain(bool has)
{
    sceneHasTrain_ = has;
}

double TrainLayer::trainLength() const
{
    return trainLength_;
}

const QVector<Photo::TrainTrack::Pass> &TrainLayer::journey() const
{
    return trainJourney_;
}

void TrainLayer::setTrainJourney(const QVector<Photo::TrainTrack::Pass> &journey)
{
    trainJourney_ = journey;
}

/*
 * Aksioma::Layer
 */

bool TrainLayer::shown(const Aksioma::Renderer &) const
{
    return showTrain();
}

void TrainLayer::initializePhotoGL(Aksioma::Renderer &)
{
    initializeOpenGLFunctions();

/*
 * ---------------------------------------------------------
 * The toy train: painted and plain wood, brass and dark windows, lit
 * like the table (the sun with its shadows, the room less what the
 * train hides from itself, the room's reflections). Positions in
 * millimetres, each vehicle turned onto the track by uVehicle.
 * ---------------------------------------------------------
 */

const QByteArray trainVertexShader = shaderSource(":/knots/shaders/train.vert");

const QByteArray trainFragmentShader = shaderSource(":/knots/shaders/train.frag");

trainProgram_ = std::make_unique<QOpenGLShaderProgram>();

if (trainProgram_->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, trainVertexShader) &&
    trainProgram_->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, trainFragmentShader))
{
    trainProgram_->bindAttributeLocation("aPosition", 0);
    trainProgram_->bindAttributeLocation("aNormal", 1);
    trainProgram_->bindAttributeLocation("aOcclusion", 2);
    trainProgram_->bindAttributeLocation("aMaterial", 3);
    trainProgram_->link();
}

/*
 * ---------------------------------------------------------
 * The steam: soft puffs, each a card turned to the camera. A puff is
 * thicker in the middle, torn at the edge by a little noise, lit by the
 * sun that reaches it and by the room, and it thins out as it spreads.
 * ---------------------------------------------------------
 */

const QByteArray steamVertexShader = shaderSource(":/knots/shaders/steam.vert");

const QByteArray steamFragmentShader = shaderSource(":/knots/shaders/steam.frag");

steamProgram_ = std::make_unique<QOpenGLShaderProgram>();

if (steamProgram_->addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, steamVertexShader) &&
    steamProgram_->addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, steamFragmentShader))
{
    steamProgram_->link();
}
}

void TrainLayer::releaseGL(Aksioma::Renderer &)
{
    destroyTrain();

    trainProgram_.reset();
    steamProgram_.reset();
}

void TrainLayer::photoSceneLoaded(Aksioma::Renderer &)
{
    // the train (its scene only)
    if (sceneHasTrain_)
        uploadTrain();
}

void TrainLayer::photoSceneReleased(Aksioma::Renderer &)
{
    destroyTrain();

    bytes_ = 0;
}

void TrainLayer::advance(Aksioma::Renderer &)
{
    advanceTrain();
}

bool TrainLayer::animating(const Aksioma::Renderer &) const
{
    // a train that runs needs every frame
    return showTrain() && (trainRunning() || !steam_.isEmpty());
}

void TrainLayer::extendBounds(const Aksioma::Renderer &, QVector3D &, QVector3D &maximum) const
{
    // the train on the highest bridge
    if (showTrain())
        maximum.setY(maximum.y() + static_cast<float>(Photo::TrainHeightMm) * view_.worldPerMm());
}

void TrainLayer::drawDepth(Aksioma::Renderer &, QOpenGLShaderProgram &program, const char *matrix,
                           const QMatrix4x4 &viewProjection)
{
    // the train casts its shadow too (on the floor and on the track)
    if (showTrain())
        drawTrainVehicles(program, matrix, viewProjection);
}

void TrainLayer::drawOpaque(Aksioma::Renderer &, const QMatrix4x4 &viewProjection)
{
    drawTrain(viewProjection);
}

void TrainLayer::drawTransparent(Aksioma::Renderer &, const QMatrix4x4 &viewProjection, const QMatrix4x4 &camera)
{
    drawSteam(viewProjection, camera);
}

void TrainLayer::photoLights(const Aksioma::Renderer &, Aksioma::LayerLights &lights) const
{
    // (as the train last left them)
    lights.places += lampPlaces_;
    lights.colors += lampColors_;
    lights.reach = lampReach_;
    lights.beamPlace = headPlace_;
    lights.beamWay = headWay_;
    lights.beamColor = QVector3D(2.4f, 1.7f, 0.85f);
}

float TrainLayer::exposureLift(const Aksioma::Renderer &) const
{
    // Cinema at night opens the lens a little: the knot is what must be seen
    constexpr float FilmNightLift = 1.5f;

    return cinema() && view_.nightShown() ? FilmNightLift : 1.0f;
}

float TrainLayer::cameraReach(const Aksioma::Renderer &) const
{
    // the whole of a long track needs more room
    return showTrain() ? 120.0f : 0.0f;
}

bool TrainLayer::frameCamera(Aksioma::Renderer &, bool animate)
{
    /*
     * The toy train: the whole track from above one corner of it, steeper
     * than a rope on a table, so that the plan of the knot can be read.
     */
    if (!showTrain())
        return false;

    const float yaw = view_.nightShown() ? 32.0f : 25.0f;
    const float pitch = view_.nightShown() ? 36.0f : 50.0f;

    view_.frameMeshFrom(yaw, pitch, animate);

    return true;
}

void TrainLayer::cameraTaken(Aksioma::Renderer &)
{
    setCinema(false);
}

qint64 TrainLayer::videoMemory() const
{
    return bytes_;
}

/*
 * The train
 */

bool TrainLayer::showTrain() const
{
    return view_.photoMode() && sceneHasTrain_ && view_.restingMesh() && trainIndexCount_ > 0 && trainPath_.size() >= 8;
}

void TrainLayer::setTrainPath(const QVector<KnotMath::Vec3> &path, double length)
{
    trainPath_ = path;
    trainLength_ = length;
    trainAlong_ = 0.0;
    trainClock_.invalidate();
    steam_.clear();

    view_.invalidatePicture();
    view_.invalidateShadows();

    view_.update();
}

void TrainLayer::setTrainRunning(bool running)
{
    trainRunning_ = running;

    trainClock_.invalidate();

    view_.update();
}

bool TrainLayer::trainRunning() const
{
    return trainRunning_ && !Aksioma::Renderer::reduceMotion();
}

void TrainLayer::setTrainAlong(double millimetres)
{
    trainAlong_ = millimetres;

    view_.invalidatePicture();
    view_.invalidateShadows();

    view_.update();
}

double TrainLayer::trainAlong() const
{
    return trainAlong_;
}

double TrainLayer::trainSpeed() const
{
    return std::max(220.0, trainLength_ / 70.0);
}

void TrainLayer::advanceTrain()
{
    if (!showTrain())
    {
        trainClock_.invalidate();

        return;
    }

    if (trainRunning())
    {
        /*
         * At the pace of a toy pushed along by hand; on a very long track
         * a little faster, so that a lap never takes much over a minute.
         * (A frame that came late does not make it jump.)
         */
        const double seconds = lapSeconds(trainClock_);

        trainAlong_ = std::fmod(trainAlong_ + trainSpeed() * seconds, trainLength_);

        if (seconds > 0.0)
        {
            view_.invalidatePicture();
            view_.invalidateShadows();
        }
    }

    trainPieces_ = Photo::placeTrain(trainPath_, trainLength_, trainAlong_);

    updateLamps();

    /*
     * The funnel blows while the train runs. Steam already in the air goes
     * on rising after it has stopped, by a clock of its own.
     */
    const float steamSeconds = static_cast<float>(lapSeconds(steamClock_));

    if (!steamFrozen_ && (trainRunning() || !steam_.isEmpty()))
    {
        stepSteam(steamSeconds, trainRunning());

        // (no time, no change: a frame drawn again adds to the picture)
        if (steamSeconds > 0.0f)
            view_.invalidatePicture();
    }

    directCinema(steamSeconds);
}

void TrainLayer::setCinema(bool on)
{
    if (cinema_ == on)
        return;

    cinema_ = on;
    cinemaTime_ = 0.0f;
    cinemaShot_ = -1;

    if (on)
        view_.stopCameraFlight();

    if (cinemaChanged)
        cinemaChanged(on);

    view_.update();
}

void TrainLayer::trainSoundScene(double *speed, float *gain, float *pan) const
{
    const bool running = showTrain() && trainRunning();

    *speed = running ? trainSpeed() : 0.0;

    // nearer is louder; to the left of the picture, to the left of the room
    const float near = std::clamp(9.0f / std::max(view_.cameraDistance(), 1.0f), 0.22f, 1.0f);

    *gain = showTrain() ? near : 0.0f;

    *pan = 0.0f;

    if (!trainPieces_.isEmpty() && view_.width() > 0)
    {
        const QVector3D engine = trainPieces_.first().place.map(QVector3D(0.0f, 20.0f, 0.0f)) * view_.worldPerMm();

        const QVector4D clip = view_.lastViewProjection() * QVector4D(engine, 1.0f);

        if (clip.w() > 0.001f)
            *pan = std::clamp(clip.x() / clip.w(), -1.0f, 1.0f);
    }
}

void TrainLayer::playCinema(float seconds)
{
    cinemaTime_ = seconds;
    cinemaShot_ = -1;

    view_.update();
}

bool TrainLayer::cinema() const
{
    return cinema_ && showTrain();
}

float TrainLayer::wholeKnotDistance(const QVector3D &toEye, QVector3D *target) const
{
    // the picture a film shows, between its bars (paintGL)
    constexpr float FilmShape = 2.39f;

    constexpr float Margin = 1.07f;

    const float mm = view_.worldPerMm();

    const float halfHigh = std::tan(qDegreesToRadians(0.5f * Aksioma::Renderer::FieldOfView));
    const float halfWide = halfHigh * static_cast<float>(view_.width()) / static_cast<float>(std::max(view_.height(), 1));
    const float halfFilm = std::min(halfHigh, halfWide / FilmShape);

    const QVector3D forward = -toEye;
    const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 1.0f, 0.0f)).normalized();
    const QVector3D up = QVector3D::crossProduct(right, forward);

    // the track, and the feet of what carries it: the piers count at a low angle
    const int step = std::max(1, static_cast<int>(trainPath_.size()) / 240);

    QVector<QVector3D> points;
    points.reserve(2 * trainPath_.size() / step + 2);

    QVector3D middle;

    for (int k = 0; k < trainPath_.size(); k += step)
    {
        const KnotMath::Vec3 &p = trainPath_.at(k);

        const QVector3D top = QVector3D(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)) * mm;

        points.push_back(top);
        points.push_back(QVector3D(top.x(), view_.photoFloorLevel(), top.z()));

        middle += top;
    }

    middle /= static_cast<float>(std::max(static_cast<int>(points.size()) / 2, 1));

    // a point at x across and f beyond the middle shows at x / (d + f)
    auto backFrom = [&](const QVector3D &from)
    {
        float distance = 0.0f;

        for (const QVector3D &point : points)
        {
            const QVector3D p = point - from;

            const float beyond = QVector3D::dotProduct(p, forward);

            distance = std::max(distance, std::abs(QVector3D::dotProduct(p, right)) * Margin / halfWide - beyond);
            distance = std::max(distance, std::abs(QVector3D::dotProduct(p, up)) * Margin / halfFilm - beyond);
        }

        return distance;
    };

    // what is near looks larger, so the middle of the picture is found by looking
    for (int round = 0; round < 3; ++round)
    {
        const float distance = backFrom(middle);

        float left = std::numeric_limits<float>::max();
        float farRight = std::numeric_limits<float>::lowest();
        float bottom = left;
        float top = farRight;

        for (const QVector3D &point : points)
        {
            const QVector3D p = point - middle;

            const float along = std::max(distance + QVector3D::dotProduct(p, forward), 1.0e-3f);

            const float x = QVector3D::dotProduct(p, right) / along;
            const float y = QVector3D::dotProduct(p, up) / along;

            left = std::min(left, x);
            farRight = std::max(farRight, x);
            bottom = std::min(bottom, y);
            top = std::max(top, y);
        }

        middle += (right * (0.5f * (left + farRight)) + up * (0.5f * (bottom + top))) * distance;
    }

    *target = middle;

    return backFrom(middle);
}

void TrainLayer::chooseCinemaBridge(float shotSeconds)
{
    // (the train comes over it some way into the shot, not at the cut)
    const double soonest = trainAlong_ + 0.3 * trainSpeed() * shotSeconds;

    double along = soonest;
    double shortest = std::numeric_limits<double>::max();

    for (const Photo::TrainTrack::Pass &pass : trainJourney_)
    {
        if (!pass.over)
            continue;

        double wait = std::fmod(pass.along - soonest, trainLength_);

        if (wait < 0.0)
            wait += trainLength_;

        if (wait < shortest)
        {
            shortest = wait;
            along = pass.along;
        }
    }

    const QMatrix4x4 there = Photo::placeVehicle(trainPath_, trainLength_, along, 26.0);

    cinemaSpot_ = there.map(QVector3D(0.0f, 0.0f, 0.0f)) * view_.worldPerMm();
    cinemaAhead_ = there.mapVector(QVector3D(1.0f, 0.0f, 0.0f));
    cinemaAhead_.setY(0.0f);
    cinemaAhead_.normalize();
    cinemaSide_ = there.mapVector(QVector3D(0.0f, 0.0f, 1.0f)).normalized();
}

void TrainLayer::directCinema(float seconds)
{
    if (!cinema() || trainPieces_.isEmpty())
        return;

    /*
     * The film is about the knot; the train only travels on it. Four shots,
     * cut one after the other. Three hold the whole knot in the picture, so
     * that its shape is what is seen: from a slow circle above it; rising
     * until it lies open as a drawing in a book; and low, where its bridges
     * stand up against each other. The other goes close to one bridge, the
     * one thing that makes a knot a knot, as the train comes over it.
     */
    constexpr float ShotLength = 9.0f;
    constexpr int Shots = 4;

    cinemaTime_ += seconds;

    const int shot = static_cast<int>(cinemaTime_ / ShotLength) % Shots;

    const float t = std::fmod(cinemaTime_, ShotLength) / ShotLength;

    const float e = t * t * (3.0f - 2.0f * t);

    if (shot != cinemaShot_)
    {
        cinemaShot_ = shot;

        if (shot == 2)
            chooseCinemaBridge(ShotLength);
    }

    // the circle goes on through the cuts: the knot is seen from every side
    const float turn = qDegreesToRadians(40.0f + cinemaTime_ * 3.5f);

    auto wholeKnot = [&](float elevation, float back)
    {
        const float up = qDegreesToRadians(elevation);

        const QVector3D toEye(std::sin(turn) * std::cos(up), std::sin(up), std::cos(turn) * std::cos(up));

        QVector3D target;

        const float distance = wholeKnotDistance(toEye, &target);

        view_.aimCamera(target + toEye * (distance * back), target);
    };

    if (shot == 2 && !trainJourney_.isEmpty())
    {
        // slowly round the bridge, never looking along either track
        const float mm = view_.worldPerMm();

        const float round = qDegreesToRadians(28.0f + 34.0f * e);

        const QVector3D way = cinemaSide_ * std::cos(round) + cinemaAhead_ * std::sin(round);

        const QVector3D target = cinemaSpot_ - QVector3D(0.0f, 34.0f * mm, 0.0f);

        view_.aimCamera(target + way * ((560.0f - 60.0f * e) * mm) + QVector3D(0.0f, 300.0f * mm, 0.0f), target);
    }
    else if (shot == 1)
    {
        wholeKnot(36.0f + 38.0f * e, 1.0f);
    }
    else if (shot == 3)
    {
        wholeKnot(13.0f + 5.0f * e, 1.1f - 0.1f * e);
    }
    else
    {
        wholeKnot(32.0f - 5.0f * e, 1.12f - 0.12f * e);
    }

    if (seconds > 0.0f)
        view_.invalidatePicture();
}

void TrainLayer::updateLamps()
{
    lampPlaces_.clear();
    lampColors_.clear();

    if (!view_.nightShown() || trainPieces_.isEmpty())
        return;

    const float mm = view_.worldPerMm();

    lampReach_ = 900.0f * mm;

    const QMatrix4x4 &locomotive = trainPieces_.first().place;

    headPlace_ = locomotive.map(QVector3D(50.0f, 19.0f, 0.0f)) * mm;
    headWay_ = locomotive.mapVector(QVector3D(1.0f, -0.09f, 0.0f)).normalized();

    // the lit carriage carries a little light with it
    const Photo::TrainModel &train = Photo::trainModel();

    for (const Photo::TrainPiece &piece : std::as_const(trainPieces_))
    {
        if (piece.firstIndex == train.parts[2].firstIndex)
        {
            lampPlaces_.push_back(piece.place.map(QVector3D(0.0f, 42.0f, 0.0f)) * mm);
            lampColors_.push_back(QVector3D(1.0f, 0.62f, 0.28f) * 0.035f);
        }
    }
}

void TrainLayer::stepSteam(float seconds, bool blowing)
{
    constexpr float Life = 2.6f;
    constexpr float Every = 0.04f;

    // the puffs that are there rise, spread and thin out
    for (int k = steam_.size() - 1; k >= 0; --k)
    {
        SteamPuff &puff = steam_[k];

        puff.age += seconds;

        if (puff.age >= Life)
        {
            steam_.remove(k);

            continue;
        }

        const float slowing = std::exp(-1.1f * puff.age);

        puff.place += puff.drift * (seconds * slowing);
        puff.place.setY(puff.place.y() + seconds * (34.0f + 62.0f * slowing));
    }

    if (!blowing || trainPieces_.isEmpty())
        return;

    steamWait_ -= seconds;

    if (steamWait_ > 0.0f)
        return;

    steamWait_ = Every;

    const QMatrix4x4 &locomotive = trainPieces_.first().place;

    SteamPuff puff;

    puff.place = locomotive.map(QVector3D(38.5f, 41.0f, 0.0f));

    // a little of its own way, and left behind by the moving train
    const float seed = steamSeed_ += 0.618034f;

    const QVector3D ahead = locomotive.mapVector(QVector3D(1.0f, 0.0f, 0.0f));
    const QVector3D side = locomotive.mapVector(QVector3D(0.0f, 0.0f, 1.0f));

    puff.drift = ahead * 26.0f + side * (34.0f * std::sin(seed * 9.1f));
    puff.seed = std::fmod(seed * 37.0f, 40.0f);

    steam_.push_back(puff);
}

void TrainLayer::runSteam(float seconds)
{
    if (trainPieces_.isEmpty() && trainPath_.size() >= 8)
        trainPieces_ = Photo::placeTrain(trainPath_, trainLength_, trainAlong_);

    for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f)
        stepSteam(1.0f / 30.0f, true);

    steamFrozen_ = true;

    view_.invalidatePicture();

    view_.update();
}

void TrainLayer::drawSteam(const QMatrix4x4 &viewProjection, const QMatrix4x4 &view)
{
    if (!showTrain() || (steam_.isEmpty() && !view_.nightShown()) || !steamProgram_ || !steamProgram_->isLinked())
        return;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);

    // (light already times its share; what is behind shows through the rest)
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

    steamProgram_->bind();

    view_.setShadowUniforms(*steamProgram_, view_.lighting().meshShadows);
    view_.setPhotoUniforms(*steamProgram_);

    const float mm = view_.worldPerMm();

    // the camera's own directions, in the world
    const QVector3D right(view(0, 0), view(0, 1), view(0, 2));
    const QVector3D up(view(1, 0), view(1, 1), view(1, 2));

    steamProgram_->setUniformValue("uViewProjection", viewProjection);
    steamProgram_->setUniformValue("uRight", right);
    steamProgram_->setUniformValue("uUp", up);

    view_.emptyVao().bind();

    // the oldest first: they lie furthest behind the funnel
    for (const SteamPuff &puff : std::as_const(steam_))
    {
        const float age = puff.age / 2.6f;

        const float size = (9.0f + 36.0f * std::sqrt(age)) * mm;

        const float thickness = 0.2f * smoothstepUp(age / 0.05f) * std::pow(1.0f - age, 1.8f);

        steamProgram_->setUniformValue("uCentre", puff.place * mm);
        steamProgram_->setUniformValue("uSize", size);
        steamProgram_->setUniformValue("uThickness", thickness);
        steamProgram_->setUniformValue("uSeed", puff.seed);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // the glow around every light
    if (view_.nightShown())
    {
        steamProgram_->setUniformValue("uCentre", headPlace_);
        steamProgram_->setUniformValue("uSize", 42.0f * mm);
        steamProgram_->setUniformValue("uHalo", QVector3D(1.3f, 0.85f, 0.4f));

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        steamProgram_->setUniformValue("uHalo", QVector3D(0.0f, 0.0f, 0.0f));
    }

    view_.emptyVao().release();

    steamProgram_->release();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void TrainLayer::uploadTrain()
{
    destroyTrain();

    const Photo::TrainModel &train = Photo::trainModel();

    if (!trainProgram_ || !trainProgram_->isLinked())
        return;

    trainVao_.create();
    trainVao_.bind();

    trainBuffer_.create();
    trainBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    trainBuffer_.bind();
    trainBuffer_.allocate(train.vertices.constData(), static_cast<int>(train.vertices.size() * sizeof(float)));

    const int stride = static_cast<int>(Photo::TrainModel::VertexFloats * sizeof(float));

    trainProgram_->bind();

    trainProgram_->enableAttributeArray(0);
    trainProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 3, stride);
    trainProgram_->enableAttributeArray(1);
    trainProgram_->setAttributeBuffer(1, GL_FLOAT, static_cast<int>(3 * sizeof(float)), 3, stride);
    trainProgram_->enableAttributeArray(2);
    trainProgram_->setAttributeBuffer(2, GL_FLOAT, static_cast<int>(6 * sizeof(float)), 1, stride);
    trainProgram_->enableAttributeArray(3);
    trainProgram_->setAttributeBuffer(3, GL_FLOAT, static_cast<int>(7 * sizeof(float)), 1, stride);

    trainProgram_->release();

    trainIndexBuffer_.create();
    trainIndexBuffer_.setUsagePattern(QOpenGLBuffer::StaticDraw);
    trainIndexBuffer_.bind();
    trainIndexBuffer_.allocate(train.indices.constData(), static_cast<int>(train.indices.size() * sizeof(quint32)));

    trainVao_.release();

    trainBuffer_.release();
    trainIndexBuffer_.release();

    trainIndexCount_ = train.indices.size();

    bytes_ += static_cast<qint64>(train.vertices.size()) * 4 +
                   static_cast<qint64>(train.indices.size()) * 4;
}

void TrainLayer::destroyTrain()
{
    if (trainVao_.isCreated())
        trainVao_.destroy();

    for (QOpenGLBuffer *buffer : {&trainBuffer_, &trainIndexBuffer_})
    {
        if (buffer->isCreated())
            buffer->destroy();
    }

    trainIndexCount_ = 0;
}

void TrainLayer::drawTrainVehicles(QOpenGLShaderProgram &program, const char *matrix, const QMatrix4x4 &viewProjection)
{
    trainVao_.bind();

    for (const Photo::TrainPiece &piece : std::as_const(trainPieces_))
    {
        if (matrix != nullptr)
        {
            QMatrix4x4 scale;

            scale.scale(view_.worldPerMm());

            program.setUniformValue(matrix, viewProjection * scale * piece.place);
        }
        else
        {
            program.setUniformValue("uVehicle", piece.place);
        }

        glDrawElements(
            GL_TRIANGLES,
            piece.indexCount,
            GL_UNSIGNED_INT,
            reinterpret_cast<const void *>(static_cast<quintptr>(piece.firstIndex) * sizeof(quint32)));
    }

    trainVao_.release();
}

void TrainLayer::drawTrain(const QMatrix4x4 &viewProjection)
{
    if (!showTrain())
        return;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    trainProgram_->bind();

    view_.setShadowUniforms(*trainProgram_, view_.lighting().meshShadows);
    view_.setPhotoUniforms(*trainProgram_);

    trainProgram_->setUniformValue("uViewProjection", viewProjection);
    trainProgram_->setUniformValue("uScale", view_.worldPerMm());
    trainProgram_->setUniformValue("uCameraPosition", view_.photoEye());
    trainProgram_->setUniformValue("uMirrorLevel", view_.photoFloorLevel());
    trainProgram_->setUniformValue("uReflections", view_.lighting().reflections ? 1.0f : 0.0f);

    drawTrainVehicles(*trainProgram_, nullptr, QMatrix4x4());

    trainProgram_->release();
}
