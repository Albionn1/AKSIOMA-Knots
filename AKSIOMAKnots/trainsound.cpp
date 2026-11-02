#include "trainsound.h"

#include "trainsynth.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QThread>

#include <algorithm>
#include <memory>
#include <vector>

namespace
{

// what the sound card reads from: as many samples as it asks for
class SynthStream : public QIODevice
{
public:
    SynthStream(
        const std::atomic<double> &speed,
        const std::atomic<bool> &night,
        const std::atomic<float> &gain,
        const std::atomic<float> &pan)
        : speed_(speed), night_(night), gain_(gain), pan_(pan)
    {
        open(QIODevice::ReadOnly);
    }

    bool isSequential() const override { return true; }

    qint64 bytesAvailable() const override { return 1 << 16; }

protected:
    qint64 readData(char *data, qint64 size) override
    {
        const int frames = static_cast<int>(size / (2 * sizeof(float)));

        if (frames <= 0)
            return 0;

        synth_.setScene(speed_.load(), night_.load(), gain_.load(), pan_.load());
        synth_.render(reinterpret_cast<float *>(data), frames);

        return static_cast<qint64>(frames) * 2 * sizeof(float);
    }

    qint64 writeData(const char *, qint64) override { return 0; }

private:
    const std::atomic<double> &speed_;
    const std::atomic<bool> &night_;
    const std::atomic<float> &gain_;
    const std::atomic<float> &pan_;

    TrainSynth synth_;
};

// the sound card's own thread: it runs until it is told to quit
class SoundThread : public QThread
{
public:
    SoundThread(
        const std::atomic<double> &speed,
        const std::atomic<bool> &night,
        const std::atomic<float> &gain,
        const std::atomic<float> &pan)
        : speed_(speed), night_(night), gain_(gain), pan_(pan)
    {
    }

protected:
    void run() override
    {
        QAudioFormat format;

        format.setSampleRate(TrainSynth::SampleRate);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Float);

        const QAudioDevice device = QMediaDevices::defaultAudioOutput();

        if (device.isNull() || !device.isFormatSupported(format))
            return;

        SynthStream stream(speed_, night_, gain_, pan_);

        QAudioSink sink(device, format);

        sink.setBufferSize(TrainSynth::SampleRate / 8 * 2 * static_cast<int>(sizeof(float)));
        sink.start(&stream);

        exec();

        sink.stop();
    }

private:
    const std::atomic<double> &speed_;
    const std::atomic<bool> &night_;
    const std::atomic<float> &gain_;
    const std::atomic<float> &pan_;
};

} // namespace

TrainSound::TrainSound(QObject *parent)
    : QObject(parent)
{
    thread_ = new SoundThread(speed_, night_, gain_, pan_);

    thread_->start();
}

TrainSound::~TrainSound()
{
    thread_->quit();
    thread_->wait(2000);

    delete thread_;
}

void TrainSound::setScene(double speed, bool night, float gain, float pan)
{
    speed_ = speed;
    night_ = night;
    gain_ = gain;
    pan_ = pan;
}
