#ifndef TRAINSYNTH_H
#define TRAINSYNTH_H

#include <cstdint>

/*
 * The sound of the model train, made from nothing: no recordings. Everything
 * is noise and sine waves shaped as the real thing is:
 *
 *   the steam: bursts of filtered noise, two for every turn of the wheels, so
 *   that they are in time with what is seen;
 *
 *   the track: a short clack of steel where each axle meets a rail joint;
 *
 *   the wheels: a low rumble on the rails that grows with the speed;
 *
 *   the whistle: two pipes a fifth apart, with breath in them, now and then;
 *
 *   the night: crickets on either side, and a little wind.
 */
class TrainSynth
{
public:
    static constexpr int SampleRate = 44100;

    explicit TrainSynth(std::uint32_t seed = 2463534242u);

    // What is happening now. speed in mm/s (0: standing), gain 0..1, pan
    // -1 (left) .. 1 (right).
    void setScene(double speed, bool night, float gain, float pan);

    void blowWhistle();

    // the next `frames` stereo frames, left and right in turn, within -1..1
    void render(float *out, int frames);

    // how many steam bursts and joints so far (for the tests)
    int chuffs() const { return chuffCount_; }
    int knocks() const { return knockCount_; }
    int whistles() const { return whistleCount_; }

private:
    float noise();

    std::uint32_t state_;

    // what it is told, and where it has got to on the way there
    double targetSpeed_ = 0.0;
    double speed_ = 0.0;
    bool night_ = false;
    float nightLevel_ = 0.0f;
    float targetGain_ = 0.0f;
    float gain_ = 0.0f;
    float targetPan_ = 0.0f;
    float pan_ = 0.0f;

    // the steam
    double chuffPhase_ = 0.0;
    int chuffBeat_ = 0;
    float chuffLevel_ = 0.0f;
    float chuffLow_ = 0.0f;
    float chuffBand_ = 0.0f;
    float thumpLevel_ = 0.0f;
    float thumpPhase_ = 0.0f;

    // the track
    double travelled_ = 0.0;
    double nextKnock_ = 0.0;
    int knockAxle_ = 0;
    float knockLevel_ = 0.0f;
    float knockPhaseA_ = 0.0f;
    float knockPhaseB_ = 0.0f;

    // the wheels, the wind
    float rumble_ = 0.0f;
    float rumble2_ = 0.0f;
    float wind_ = 0.0f;
    float windSlow_ = 0.0f;

    // the whistle: where it is in its call, and its pipes
    double whistleClock_ = -1.0;
    double whistleWait_ = 6.0;
    float pipeA_ = 0.0f;
    float pipeB_ = 0.0f;
    float pipeWobble_ = 0.0f;
    float breath_ = 0.0f;

    // three crickets
    struct Cricket
    {
        float pitch = 4300.0f;
        float pan = 0.0f;
        float phase = 0.0f;
        double clock = 0.0;
        double every = 1.0;
    };

    Cricket crickets_[3];

    int chuffCount_ = 0;
    int knockCount_ = 0;
    int whistleCount_ = 0;
};

#endif // TRAINSYNTH_H
