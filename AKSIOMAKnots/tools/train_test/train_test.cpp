// Headless check of the Toy train scene (traintrack.cpp): every knot of the
// table up to a number of crossings is built as a track. The finished centre
// line must be the same knot (Alexander and Jones polynomials), the train
// must fit (no piece of track in another's way, every bend and slope within
// limits), and each must have a bridge per crossing of its drawing.
//
//   train_test [max crossings] [stride]   the table, every stride-th knot
//   train_test name 3_1 [plan.png]        one knot; a drawing of the plan

#include "knotdiagram.h"
#include "knotmath.h"
#include "knottable.h"
#include "trainsynth.h"
#include "traintrack.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSet>

#include <cmath>
#include <cstdio>

using namespace KnotMath;

namespace
{

int failures = 0;

// how many crossings the Jones polynomial is computed for: more than the
// usual 18 takes longer, and tells a knot from its mirror image on the
// drawings that have more
int jonesLimit = 18;

// The curve the window builds the track from, at its usual density: the
// table's own points are resampled first, and a track depends on the curve.
QVector<Vec3> appCurve(const QString &name)
{
    return buildPreset(Preset::KnotTable, 160, 0, 0, name).points;
}

void savePlan(const Photo::TrainTrack &track, const QString &file)
{
    double minX = 1e30, maxX = -1e30, minZ = 1e30, maxZ = -1e30;

    for (const Vec3 &p : track.path)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
    }

    constexpr int Size = 1000;

    const double scale = (Size - 80) / std::max(maxX - minX, maxZ - minZ);

    QImage image(Size, Size, QImage::Format_RGB32);

    image.fill(Qt::white);

    QPainter painter(&image);

    painter.setRenderHint(QPainter::Antialiasing);

    const int n = track.path.size();

    auto at = [&](const Vec3 &p) { return QPointF(40.0 + (p.x - minX) * scale, 40.0 + (p.z - minZ) * scale); };

    // low parts first, each with the white edge that shows over and under
    QVector<int> order(n);

    for (int i = 0; i < n; ++i)
        order[i] = i;

    std::stable_sort(
        order.begin(),
        order.end(),
        [&](int a, int b) { return track.path.at(a).y < track.path.at(b).y; });

    const double width = 40.0 * scale;

    for (int pass = 0; pass < 1; ++pass)
    {
        for (const int i : order)
        {
            const Vec3 &a = track.path.at(i);
            const Vec3 &b = track.path.at((i + 1) % n);

            painter.setPen(QPen(Qt::white, width + 4.0, Qt::SolidLine, Qt::FlatCap));
            painter.drawLine(at(a), at(b));

            const int level = static_cast<int>(180.0 * std::clamp((a.y - 10.0) / 54.0, 0.0, 1.0));

            painter.setPen(QPen(QColor(200 - level, 150, 60 + level), width, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(at(a), at(b));
        }
    }

    painter.end();

    image.save(file);
}

void build(const QString &name, const QVector<Vec3> &curve, int crossings, const QString &file)
{
    const AlexanderPolynomial alexander = computeAlexanderPolynomial(curve);
    const JonesPolynomial jones = computeJonesPolynomial(curve, jonesLimit);

    QElapsedTimer clock;
    clock.start();

    const Photo::TrainTrack track = Photo::buildTrainTrack(curve);

    const qint64 ms = clock.elapsed();

    if (!track.valid)
    {
        std::printf("%-8s FAILED: %s\n", qPrintable(name), qPrintable(track.error));
        ++failures;

        return;
    }

    const AlexanderPolynomial a = computeAlexanderPolynomial(track.rope.points);
    const JonesPolynomial j = computeJonesPolynomial(track.rope.points, jonesLimit);

    const bool same =
        a.valid && alexander.valid && a.text == alexander.text &&
        (!jones.valid || !j.valid || j.text == jones.text);

    double minX = 1e30, maxX = -1e30, minZ = 1e30, maxZ = -1e30;

    for (const Vec3 &p : track.path)
    {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minZ = std::min(minZ, p.z);
        maxZ = std::max(maxZ, p.z);
    }

    const bool fits =
        track.steepest < 0.36 && track.tightestBend >= 92.0 && track.closestStrands >= 50.0;

    const bool bridges = track.bridges.size() >= crossings;

    // every bridge on the journey exactly twice, once over and once under
    QVector<int> overs(track.bridges.size() + 1, 0);
    QVector<int> unders(track.bridges.size() + 1, 0);

    bool journey = track.journey.size() == 2 * track.bridges.size();

    for (const Photo::TrainTrack::Pass &pass : track.journey)
    {
        if (pass.bridge < 1 || pass.bridge > track.bridges.size())
        {
            journey = false;

            continue;
        }

        ++(pass.over ? overs : unders)[pass.bridge];
    }

    for (int k = 1; k <= track.bridges.size(); ++k)
        journey = journey && overs.at(k) == 1 && unders.at(k) == 1;

    if (!journey)
        std::printf("%-8s the journey does not pass every bridge once each way\n", qPrintable(name));

    const bool ok = same && fits && bridges && journey;

    if (!ok)
        ++failures;

    std::printf(
        "%-8s %5.2f m round, %4.2f x %4.2f m, %2d bridges, %3d stacks  slope %.2f  bend %4.0f mm  side by side %4.0f mm  "
        "%5d vertices  %6lld ms  %s%s%s\n",
        qPrintable(name),
        track.length / 1000.0,
        (maxX - minX) / 1000.0,
        (maxZ - minZ) / 1000.0,
        static_cast<int>(track.bridges.size()),
        track.piers,
        track.steepest,
        track.tightestBend,
        track.closestStrands,
        static_cast<int>(track.mesh.positions.size() / 3),
        ms,
        same ? "same knot" : "CHANGED",
        jones.valid && j.valid ? " J" : " (Alexander only)",
        ok ? "" : "  <-- FAIL");

    std::fflush(stdout);

    if (!file.isEmpty())
        savePlan(track, file);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const KnotTable &table = KnotTable::instance();

    if (!table.isLoaded())
    {
        std::printf("no knot table: %s\n", qPrintable(table.error()));
        return 1;
    }

    // train_test drawing 4_1 before.png after.png: the drawing, before and
    // after it is opened up
    if (argc >= 5 && QString(argv[1]) == QStringLiteral("drawing"))
    {
        QString error;

        KnotDiagram diagram = diagramFromCurve(table.curve(table.indexOf(QString(argv[2]))), 2.0, error);

        auto draw = [](const KnotDiagram &d, const QString &file)
        {
            double low = 1e30;
            double high = -1e30;

            for (const Vec3 &p : d.points)
            {
                low = std::min({low, p.x, p.y});
                high = std::max({high, p.x, p.y});
            }

            const double scale = 900.0 / (high - low);

            QImage image(1000, 1000, QImage::Format_RGB32);

            image.fill(Qt::white);

            QPainter painter(&image);

            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(Qt::black, 3.0));

            auto at = [&](const Vec3 &p) { return QPointF(50.0 + (p.x - low) * scale, 950.0 - (p.y - low) * scale); };

            for (int i = 0; i < d.points.size(); ++i)
                painter.drawLine(at(d.points.at(i)), at(d.points.at((i + 1) % d.points.size())));

            painter.setPen(QPen(Qt::red, 2.0));
            painter.setBrush(Qt::NoBrush);

            for (const DiagramCrossing &c : d.crossings)
                painter.drawEllipse(at(c.position), 9.0, 9.0);

            painter.end();
            image.save(file);
        };

        draw(diagram, QString(argv[3]));

        const bool opened = openDiagram(diagram);

        std::printf("opened: %d, %d crossings, %d points\n", opened, int(diagram.crossings.size()), int(diagram.points.size()));

        draw(diagram, QString(argv[4]));

        return 0;
    }

    if (argc >= 3 && QString(argv[1]) == QStringLiteral("name"))
    {
        const int index = table.indexOf(QString(argv[2]));

        if (index < 0)
        {
            std::printf("unknown knot\n");
            return 1;
        }

        build(table.at(index).name, appCurve(table.at(index).name), table.at(index).crossings,
              argc >= 4 ? QString(argv[3]) : QString());

        return failures == 0 ? 0 : 1;
    }

    /*
     * train_test sound [file.wav]: nobody can listen in a test, so the sound
     * is measured. By day and by night, running and standing: no sample out
     * of range or not a number, the steam in time with the wheels, a knock at
     * every joint, the whistle, crickets only at night.
     */
    if (argc >= 2 && QString(argv[1]) == QStringLiteral("sound"))
    {
        auto energy = [](TrainSynth &synth, double seconds, float *peak, QVector<float> *keep)
        {
            const int frames = static_cast<int>(seconds * TrainSynth::SampleRate);

            QVector<float> samples(2 * frames);

            synth.render(samples.data(), frames);

            double sum = 0.0;

            for (const float x : samples)
            {
                if (std::isnan(x) || std::abs(x) > 1.0f)
                    *peak = 99.0f;

                *peak = std::max(*peak, std::abs(x));

                sum += double(x) * x;
            }

            if (keep != nullptr)
                *keep += samples;

            return std::sqrt(sum / samples.size());
        };

        constexpr double Speed = 220.0;
        constexpr double Seconds = 30.0;

        float peak = 0.0f;
        QVector<float> kept;

        TrainSynth night;

        night.setScene(Speed, true, 1.0f, 0.3f);

        const double loud = energy(night, Seconds, &peak, &kept);

        const double chuffsWanted = Speed * Seconds / (2.0 * 3.14159265 * 8.0) * 2.0;
        const double knocksWanted = Speed * Seconds / 144.0 * 2.0;

        std::printf("running at night: rms %.3f, peak %.3f, %d steam bursts (about %.0f), %d knocks (about %.0f), %d whistles\n",
                    loud, peak, night.chuffs(), chuffsWanted, night.knocks(), knocksWanted, night.whistles());

        night.setScene(0.0, true, 1.0f, 0.0f);

        float ignored = 0.0f;

        energy(night, 3.0, &ignored, nullptr);

        const int chuffsBefore = night.chuffs();

        const double standing = energy(night, 5.0, &peak, nullptr);

        std::printf("standing at night: rms %.4f, %d steam bursts (none)\n", standing, night.chuffs() - chuffsBefore);

        TrainSynth day;

        day.setScene(0.0, false, 1.0f, 0.0f);

        energy(day, 3.0, &ignored, nullptr);

        const double quiet = energy(day, 5.0, &peak, nullptr);

        std::printf("standing by day: rms %.5f (silence)\n", quiet);

        TrainSynth off;

        off.setScene(Speed, true, 0.0f, 0.0f);

        const double muted = energy(off, 5.0, &peak, nullptr);

        std::printf("no gain: rms %.6f (silence)\n", muted);

        const bool ok =
            peak <= 1.0f && loud > 0.02 && std::abs(night.chuffs() - chuffsBefore) == 0 &&
            std::abs(chuffsBefore - chuffsWanted) < 4.0 && std::abs(night.knocks() - knocksWanted) < 4.0 &&
            night.whistles() >= 1 && standing > 0.0005 && quiet < 0.0005 && muted < 0.00001;

        if (argc >= 3)
        {
            QFile wav(QString::fromLocal8Bit(argv[2]));

            if (wav.open(QIODevice::WriteOnly))
            {
                const quint32 bytes = static_cast<quint32>(kept.size() * 2);

                QByteArray head;

                auto put32 = [&head](quint32 v) { head.append(reinterpret_cast<const char *>(&v), 4); };
                auto put16 = [&head](quint16 v) { head.append(reinterpret_cast<const char *>(&v), 2); };

                head += "RIFF";
                put32(36 + bytes);
                head += "WAVEfmt ";
                put32(16);
                put16(1);
                put16(2);
                put32(TrainSynth::SampleRate);
                put32(TrainSynth::SampleRate * 4);
                put16(4);
                put16(16);
                head += "data";
                put32(bytes);

                wav.write(head);

                for (const float x : kept)
                {
                    const qint16 v = static_cast<qint16>(x * 32767.0f);

                    wav.write(reinterpret_cast<const char *>(&v), 2);
                }
            }
        }

        std::printf("%s\n", ok ? "sound ok" : "SOUND FAILED");

        return ok ? 0 : 1;
    }

    /*
     * train_test list <names> <shares> <share>: the knots in a file, one name
     * a line, with the Jones polynomial up to 24 crossings.
     */
    if (argc >= 5 && QString(argv[1]) == QStringLiteral("list"))
    {
        jonesLimit = 24;

        QFile names(QString::fromLocal8Bit(argv[2]));

        if (!names.open(QIODevice::ReadOnly | QIODevice::Text))
            return 1;

        const int shares = std::max(1, QString(argv[3]).toInt());
        const int share = QString(argv[4]).toInt();

        int line = 0;

        while (!names.atEnd())
        {
            const QString name = QString::fromLatin1(names.readLine()).trimmed();

            if (name.isEmpty() || line++ % shares != share)
                continue;

            const int index = table.indexOf(name);

            if (index >= 0)
                build(table.at(index).name, appCurve(table.at(index).name), table.at(index).crossings, QString());
        }

        std::printf("\n%d failures\n", failures);

        return failures == 0 ? 0 : 1;
    }

    /*
     * train_test hard <done.log> <drawn> <shares> <share>: the knots not in
     * the log (an earlier run's output) that are most likely to give trouble:
     * those whose cleanest drawing has at least <drawn> crossings, far more
     * than the knot needs, and every 25th of the others to show what the
     * rest are like.
     */
    if (argc >= 6 && QString(argv[1]) == QStringLiteral("hard"))
    {
        QSet<QString> done;

        QFile log(QString::fromLocal8Bit(argv[2]));

        if (log.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            while (!log.atEnd())
                done.insert(QString::fromLatin1(log.readLine()).section(QLatin1Char(' '), 0, 0));
        }

        const int drawn = QString(argv[3]).toInt();
        const int shares = std::max(1, QString(argv[4]).toInt());
        const int share = QString(argv[5]).toInt();

        int count = 0;
        int hard = 0;
        int seen = 0;

        for (int index = 0; index < table.size(); ++index)
        {
            const QString name = table.at(index).name;

            if (done.contains(name) || seen++ % shares != share)
                continue;

            const QVector<Vec3> curve = appCurve(name);

            QString error;

            const KnotDiagram diagram = diagramFromCurve(curve, 2.0, error);

            const bool likely = diagram.crossings.size() >= drawn;

            if (likely)
                ++hard;

            // (the others share one count, whichever core they land on)
            if (!likely && seen % (25 * shares) != share + 1)
                continue;

            build(name, curve, table.at(index).crossings, QString());

            ++count;
        }

        std::printf("\n%d knots, %d hard ones, %d failures\n", count, hard, failures);

        return failures == 0 ? 0 : 1;
    }

    const int most = argc >= 2 ? QString(argv[1]).toInt() : 8;
    const int stride = argc >= 3 ? std::max(1, QString(argv[2]).toInt()) : 1;

    // train_test 13 12 5: the sixth of twelve equal shares of the table, for
    // running the whole of it on several cores
    const int share = argc >= 4 ? QString(argv[3]).toInt() : 0;

    int count = 0;
    int seen = 0;

    for (int index = 0; index < table.size(); ++index)
    {
        if (table.at(index).crossings > most)
            continue;

        if (seen++ % stride != share)
            continue;

        build(table.at(index).name, appCurve(table.at(index).name), table.at(index).crossings, QString());

        ++count;
    }

    std::printf("\n%d knots, %d failures\n", count, failures);

    return failures == 0 ? 0 : 1;
}
