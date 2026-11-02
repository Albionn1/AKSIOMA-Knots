#include "trainsynth.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr float TwoPi = 6.2831853f;

// the model's own measures (traintrack.cpp): the wheels' radius, where the
// pieces of track meet, how far apart the axles are
constexpr double WheelRadiusMm = 7.0;
constexpr double PieceMm = 144.0;
constexpr double AxleSpacingMm = 52.0;

constexpr float Rate = static_cast<float>(TrainSynth::SampleRate);

} // namespace

TrainSynth::TrainSynth(std::uint32_t seed)
    : state_(seed == 0 ? 1u : seed)
{
    crickets_[0].pitch = 4250.0f;
    crickets_[0].pan = -0.7f;
    crickets_[0].every = 0.93;

    crickets_[1].pitch = 4720.0f;
    crickets_[1].pan = 0.65f;
    crickets_[1].every = 1.17;
    crickets_[1].clock = 0.4;

    crickets_[2].pitch = 3880.0f;
    crickets_[2].pan = 0.1f;
    crickets_[2].every = 1.61;
    crickets_[2].clock = 0.75;
}

float TrainSynth::noise()
{
    // xorshift: quick, and the same every time for the tests
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;

    return static_cast<float>(state_) / 2147483648.0f - 1.0f;
}

void TrainSynth::setScene(double speed, bool night, float gain, float pan)
{
    targetSpeed_ = speed;
    night_ = night;
    targetGain_ = gain;
    targetPan_ = pan;
}

void TrainSynth::blowWhistle()
{
    if (whistleClock_ < 0.0)
    {
        whistleClock_ = 0.0;

        ++whistleCount_;
    }
}

void TrainSynth::render(float *out, int frames)
{
    const double step = 1.0 / SampleRate;

    // (slow enough that nothing clicks when it changes)
    const float ease = 1.0f - std::exp(-1.0f / (0.12f * Rate));

    const float chuffFall = std::exp(-1.0f / (0.034f * Rate));
    const float thumpFall = std::exp(-1.0f / (0.016f * Rate));
    const float knockFall = std::exp(-1.0f / (0.011f * Rate));

    for (int f = 0; f < frames; ++f)
    {
        speed_ += (targetSpeed_ - speed_) * ease;
        gain_ += (targetGain_ - gain_) * ease;
        pan_ += (targetPan_ - pan_) * ease;
        nightLevel_ += ((night_ ? 1.0f : 0.0f) - nightLevel_) * ease * 0.2f;

        const double moved = speed_ * step;

        const float pace = static_cast<float>(std::clamp(speed_ / 220.0, 0.0, 1.5));

        // the steam: two beats a turn of the wheels, one in four the strongest
        chuffPhase_ += moved / (2.0 * 3.14159265 * WheelRadiusMm) * 2.0;

        if (chuffPhase_ >= 1.0 && speed_ > 5.0)
        {
            chuffPhase_ -= 1.0;

            static constexpr float Accent[4] = {1.0f, 0.55f, 0.8f, 0.5f};

            chuffLevel_ = std::max(chuffLevel_, Accent[chuffBeat_]);
            thumpLevel_ = std::max(thumpLevel_, Accent[chuffBeat_]);

            chuffBeat_ = (chuffBeat_ + 1) % 4;

            ++chuffCount_;
        }

        chuffLevel_ *= chuffFall;
        thumpLevel_ *= thumpFall;

        // a band of noise around 1.6 kHz: the sound of steam from a small funnel
        const float raw = noise();

        constexpr float BandF = 0.205f;
        constexpr float BandQ = 0.62f;

        const float high = raw - chuffLow_ - BandQ * chuffBand_;

        chuffBand_ += BandF * high;
        chuffLow_ += BandF * chuffBand_;

        thumpPhase_ += TwoPi * 170.0f / Rate;

        if (thumpPhase_ > TwoPi)
            thumpPhase_ -= TwoPi;

        const float steam = chuffBand_ * chuffLevel_ * 0.42f + std::sin(thumpPhase_) * thumpLevel_ * 0.1f;

        // the track: each joint is met by the first axle, then the second
        travelled_ += moved;

        if (travelled_ >= nextKnock_ && speed_ > 5.0)
        {
            nextKnock_ += knockAxle_ == 0 ? AxleSpacingMm : PieceMm - AxleSpacingMm;

            knockLevel_ = knockAxle_ == 0 ? 1.0f : 0.7f;
            knockAxle_ = 1 - knockAxle_;

            knockPhaseA_ = 0.0f;
            knockPhaseB_ = 0.0f;

            ++knockCount_;
        }

        knockLevel_ *= knockFall;

        // (steel on steel: higher and more ringing than wood)
        knockPhaseA_ += TwoPi * 1420.0f / Rate;
        knockPhaseB_ += TwoPi * 3170.0f / Rate;

        const float knock = knockLevel_ * (0.2f * std::sin(knockPhaseA_) + 0.1f * std::sin(knockPhaseB_) + 0.05f * raw);

        // the wheels on the wood
        rumble_ += (raw - rumble_) * 0.012f;
        rumble2_ += (rumble_ - rumble2_) * 0.03f;

        const float wheels = rumble2_ * 4.2f * pace;

        // the whistle, by itself now and then while the train runs
        if (speed_ > 40.0 && whistleClock_ < 0.0)
        {
            whistleWait_ -= step;

            if (whistleWait_ <= 0.0)
            {
                whistleWait_ = 19.0 + 16.0 * (0.5 + 0.5 * noise());

                blowWhistle();
            }
        }

        float whistle = 0.0f;

        if (whistleClock_ >= 0.0)
        {
            const double t = whistleClock_;

            whistleClock_ += step;

            // long, a breath, then short
            auto blast = [](double time, double from, double to)
            {
                const double rise = std::clamp((time - from) / 0.05, 0.0, 1.0);
                const double fall = std::clamp((to - time) / 0.12, 0.0, 1.0);

                return static_cast<float>(rise * fall);
            };

            const float envelope = std::max(blast(t, 0.0, 0.62), blast(t, 0.76, 1.08));

            pipeWobble_ += TwoPi * 5.2f / Rate;

            const float bend = 1.0f + 0.004f * std::sin(pipeWobble_);

            pipeA_ += TwoPi * 1174.0f * bend / Rate;
            pipeB_ += TwoPi * 1760.0f * bend / Rate;

            if (pipeA_ > TwoPi)
                pipeA_ -= TwoPi;

            if (pipeB_ > TwoPi)
                pipeB_ -= TwoPi;

            breath_ += (raw - breath_) * 0.4f;

            whistle = envelope * (0.13f * std::sin(pipeA_) + 0.09f * std::sin(pipeB_) +
                                  0.03f * std::sin(2.0f * pipeA_) + 0.025f * breath_);

            if (t > 1.3)
                whistleClock_ = -1.0;
        }

        // crickets: a chirp is a quick run of pulses
        float cricketLeft = 0.0f;
        float cricketRight = 0.0f;

        if (nightLevel_ > 0.001f)
        {
            for (Cricket &cricket : crickets_)
            {
                cricket.clock += step;

                if (cricket.clock >= cricket.every)
                    cricket.clock -= cricket.every;

                cricket.phase += TwoPi * cricket.pitch / Rate;

                if (cricket.phase > TwoPi)
                    cricket.phase -= TwoPi;

                constexpr double Chirp = 0.21;

                if (cricket.clock < Chirp)
                {
                    const float pulses = std::sin(TwoPi * 29.0f * static_cast<float>(cricket.clock));
                    const float shape = std::sin(3.14159f * static_cast<float>(cricket.clock / Chirp));

                    const float sound = std::sin(cricket.phase) * pulses * pulses * shape * 0.016f * nightLevel_;

                    cricketLeft += sound * (0.5f - 0.5f * cricket.pan);
                    cricketRight += sound * (0.5f + 0.5f * cricket.pan);
                }
            }
        }

        // the night air
        windSlow_ += (raw - windSlow_) * 0.0009f;
        wind_ += (windSlow_ - wind_) * 0.004f;

        const float air = wind_ * 0.9f * nightLevel_;

        const float train = steam + knock + wheels + whistle;

        const float left = train * (0.5f - 0.32f * pan_) * gain_ * 1.5f + (cricketLeft + air) * std::min(gain_ * 2.0f, 1.0f);
        const float right = train * (0.5f + 0.32f * pan_) * gain_ * 1.5f + (cricketRight + air) * std::min(gain_ * 2.0f, 1.0f);

        // a soft limit: never a hard clip, whatever adds up
        out[2 * f] = std::tanh(left);
        out[2 * f + 1] = std::tanh(right);
    }
}
