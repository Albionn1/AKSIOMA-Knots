#ifndef TRAINSOUND_H
#define TRAINSOUND_H

#include <QObject>

#include <atomic>

class QThread;

/*
 * Plays TrainSynth on a thread of its own, so that a slow frame in the window
 * never breaks up the sound. A computer with no sound device plays nothing and
 * minds nothing.
 */
class TrainSound : public QObject
{
    Q_OBJECT

public:
    explicit TrainSound(QObject *parent = nullptr);
    ~TrainSound() override;

    // what is happening (see TrainSynth::setScene); from any thread
    void setScene(double speed, bool night, float gain, float pan);

private:
    QThread *thread_ = nullptr;

    std::atomic<double> speed_{0.0};
    std::atomic<bool> night_{false};
    std::atomic<float> gain_{0.0f};
    std::atomic<float> pan_{0.0f};
};

#endif // TRAINSOUND_H
