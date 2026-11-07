#include "ropephysics.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Photo
{

namespace
{

using KnotMath::Vec3;

constexpr double Pi = 3.14159265358979323846;

// points per radius of rope (the rope is simulated in units of its radius:
// aksioma/rod.h)
constexpr double Spacing = 0.5;

// distance along the rope between points i and j (in points)
int arcGap(int i, int j, int n, bool closed)
{
    const int gap = std::abs(i - j);

    return closed ? std::min(gap, n - gap) : gap;
}

} // namespace

RestingRope layOnTable(
    const QVector<Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material,
    RopeProgress *progress)
{
    RestingRope result;

    if (curve.size() < 4 || radius <= 0.0)
    {
        result.error = QStringLiteral("no curve");
        return result;
    }

    /*
     * Evenly spaced points along a smooth curve through the given ones,
     * half a radius apart.
     */
    const QVector<Vec3> smooth = KnotMath::refineCurve(curve, 4, closed);

    double length = 0.0;

    for (int i = 1; i < smooth.size(); ++i)
        length += (smooth.at(i) - smooth.at(i - 1)).length();

    if (closed)
        length += (smooth.first() - smooth.last()).length();

    const int n = std::clamp(
        static_cast<int>(std::lround(length / (Spacing * radius))),
        16,
        6000);

    const QVector<Vec3> start = KnotMath::resampleClosedCurve(smooth, n, closed);

    if (start.size() != n)
    {
        result.error = QStringLiteral("resampling failed");
        return result;
    }

    // laid down flat, the lowest point touching the table
    QVector<QVector3D> flat;

    flat.reserve(n);

    for (const Vec3 &p : start)
        flat.push_back(QVector3D(float(p.x), float(p.y), float(p.z)));

    QMatrix4x4 pose = flatPose(flat);

    double lowest = 1.0e30;

    for (const QVector3D &p : flat)
        lowest = std::min(lowest, double(pose.map(p).y()));

    QMatrix4x4 lift;

    lift.translate(0.0f, float(radius - lowest), 0.0f);

    pose = lift * pose;

    result.pose = pose;

    QVector<Vec3> x(n);

    for (int i = 0; i < n; ++i)
    {
        const QVector3D p = pose.map(flat.at(i));

        x[i] = Vec3(p.x(), p.y(), p.z()) / radius;
    }

    QVector<Vec3> frames;

    const Settling settling = settleRope(x, QVector<bool>(n, false), closed, material, Ground(), &frames, progress);

    result.cancelled = settling.cancelled;

    if (settling.cancelled)
        return result;

    result.steps = settling.steps;
    result.seconds = settling.seconds;
    result.finalSpeed = settling.finalSpeed;
    result.settled = settling.settled;

    // back to the knot's coordinates
    const QMatrix4x4 back = pose.inverted();

    result.points.reserve(n);

    for (const Vec3 &q : x)
    {
        const QVector3D k = back.map(QVector3D(
            float(q.x * radius),
            float(q.y * radius),
            float(q.z * radius)));

        result.points.push_back(Vec3(k.x(), k.y(), k.z()));
    }

    for (const Vec3 &f : frames)
    {
        const QVector3D k = back.mapVector(QVector3D(float(f.x), float(f.y), float(f.z)));

        result.frames.push_back(Vec3(k.x(), k.y(), k.z()));
    }

    result.valid = true;

    return result;
}

Settling settleRope(
    QVector<Vec3> &x,
    const QVector<bool> &pinned,
    bool closed,
    const RopeMaterial &material,
    const Ground &ground,
    QVector<Vec3> *frames,
    RopeProgress *progress)
{
    RopeSimulation simulation(x, pinned, closed, material, ground);

    if (!simulation.isValid())
        return Settling();

    // it has just been tied
    simulation.takeSet(material.tiedSet);

    const Settling result = Aksioma::Physics::settle(simulation, progress);

    if (result.cancelled)
        return result;

    x = simulation.points();

    if (frames != nullptr)
        *frames = simulation.frames();

    return result;
}

RopeInHand::RopeInHand(const RopeSimulation &rope)
    : live_(rope)
{
}

QVector<Vec3> RopeInHand::points(quint64 *version, QVector<Vec3> *frames) const
{
    std::uint64_t now = 0;

    const QVector<Vec3> result = live_.read(
        [frames](const RopeSimulation &rope)
        {
            if (frames != nullptr)
                *frames = rope.frames();

            return rope.points();
        },
        &now);

    if (version != nullptr)
        *version = now;

    return result;
}

bool RopeInHand::closed() const
{
    return live_.read([](const RopeSimulation &rope) { return rope.closed(); });
}

bool RopeInHand::resting() const
{
    return live_.resting();
}

void RopeInHand::cut(int first, int count)
{
    live_.change([first, count](RopeSimulation &rope) { rope.cut(first, count); });
}

void RopeInHand::hold(int index, const Vec3 &target, bool under)
{
    live_.change([index, target, under](RopeSimulation &rope) { rope.grab(index, target, under); });
}

void RopeInHand::moveHand(const Vec3 &target)
{
    live_.adjust([target](RopeSimulation &rope) { rope.moveGrab(target); });
}

void RopeInHand::letGo()
{
    live_.change([](RopeSimulation &rope) { rope.release(); });
}

int RopeInHand::heldPoint() const
{
    return live_.read([](const RopeSimulation &rope) { return rope.grabbedPoint(); });
}

bool RopeInHand::holdsUnder() const
{
    return live_.read([](const RopeSimulation &rope) { return rope.grabsBeneath(); });
}

void RopeInHand::rest()
{
    live_.rest();
}

/*
 * =========================================================
 * THE ROPE'S SURFACE
 * =========================================================
 */

namespace
{

// three-strand rope: a strand is a circle of radius StrandA (in rope
// radii) whose middle is StrandC from the rope's axis; neighbours press
// into each other, so the grooves are narrow and about a third deep
constexpr double StrandA = 0.6;
constexpr double StrandC = 0.4;

// one turn of a strand around the rope every 3.2 diameters (lay length)
constexpr double LayLength = 6.4;

// braided lace: 8 yarns each way, at 50 degrees to the lace; the yarns
// stand out this much (of the radius)
constexpr int BraidYarns = 8;
constexpr double BraidAngle = 50.0 * Pi / 180.0;
constexpr double YarnHeight = 0.18;

// a cut end: nothing holds the strands together over the last of the rope
// (radii), so they open this much there
constexpr double EndFlareArc = 1.5;
constexpr double EndFlare = 0.14;

// the face of a cut end, in the texture coordinate w: the shader draws its
// fibres seen end on
constexpr float EndGrain = -0.25f;

double wrapAngle(double a)
{
    a = std::fmod(a + Pi, 2.0 * Pi);

    if (a < 0.0)
        a += 2.0 * Pi;

    return a - Pi;
}

// distance from the axis of the three-strand cross section at the angle
// delta from a strand's middle
double strandProfile(double delta)
{
    const double sine = StrandC * std::sin(delta);

    return StrandC * std::cos(delta) + std::sqrt(std::max(StrandA * StrandA - sine * sine, 0.0));
}

/*
 * How much of the room a point in a groove sees (cosine weighted): in the
 * cross section the neighbouring strand rises above the point's tangent;
 * with the angles hL, hR of the highest edge on both sides the seen part
 * is (cos hL + cos hR) / 2. Tabulated over the angle across a strand.
 */
double grooveVisibility(double delta)
{
    constexpr int Samples = 720;

    auto point = [](double angle)
    {
        double best = 0.0;

        for (int k = 0; k < 3; ++k)
            best = std::max(best, strandProfile(wrapAngle(angle - 2.0 * Pi * k / 3.0)));

        return std::pair<double, double>(best * std::cos(angle), best * std::sin(angle));
    };

    const auto [px, py] = point(delta);

    const auto [ax, ay] = point(delta - 1.0e-3);
    const auto [bx, by] = point(delta + 1.0e-3);

    // outward normal of the profile at the point
    double tx = bx - ax;
    double ty = by - ay;
    const double tl = std::hypot(tx, ty);

    tx /= tl;
    ty /= tl;

    const double nx = ty;
    const double ny = -tx;

    double highestLeft = 0.0;
    double highestRight = 0.0;

    for (int k = 1; k < Samples; ++k)
    {
        const double angle = delta + 2.0 * Pi * k / Samples;
        const auto [qx, qy] = point(angle);

        const double dx = qx - px;
        const double dy = qy - py;
        const double dl = std::hypot(dx, dy);

        if (dl < 1.0e-6)
            continue;

        // elevation above the tangent
        const double up = (dx * nx + dy * ny) / dl;

        if (up <= 0.0)
            continue;

        const double elevation = std::asin(std::min(up, 1.0));

        // which side of the point it lies on
        if (dx * tx + dy * ty >= 0.0)
            highestRight = std::max(highestRight, elevation);
        else
            highestLeft = std::max(highestLeft, elevation);
    }

    return 0.5 * (std::cos(highestLeft) + std::cos(highestRight));
}

/*
 * Helpers on every core of the processor, started once and kept waiting:
 * the rope's surface is built for every frame while it is in the hand,
 * and starting threads anew each time would cost more than the work.
 */
class Workers
{
public:
    static Workers &instance()
    {
        static Workers workers;
        return workers;
    }

    // work(first, last) over 0 .. count - 1, in one piece per thread; the
    // calling thread does the first piece
    void run(int count, const std::function<void(int, int)> &work)
    {
        const std::lock_guard<std::mutex> one(oneJob_);

        const int threads = static_cast<int>(threads_.size()) + 1;

        if (threads == 1 || count < 64)
        {
            work(0, count);
            return;
        }

        {
            const std::lock_guard<std::mutex> lock(mutex_);

            job_ = &work;
            count_ = count;
            piece_ = (count + threads - 1) / threads;
            pending_ = static_cast<int>(threads_.size());
            ++generation_;
        }

        start_.notify_all();

        work(0, std::min(count, piece_));

        std::unique_lock<std::mutex> lock(mutex_);

        done_.wait(lock, [this]() { return pending_ == 0; });

        job_ = nullptr;
    }

private:
    Workers()
    {
        const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);

        for (int t = 1; t < threads; ++t)
            threads_.emplace_back([this, t]() { loop(t); });
    }

    ~Workers()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }

        start_.notify_all();

        for (std::thread &thread : threads_)
            thread.join();
    }

    void loop(int index)
    {
        int seen = 0;

        for (;;)
        {
            const std::function<void(int, int)> *job = nullptr;
            int first = 0;
            int last = 0;

            {
                std::unique_lock<std::mutex> lock(mutex_);

                start_.wait(lock, [this, seen]() { return quit_ || generation_ != seen; });

                if (quit_)
                    return;

                seen = generation_;
                job = job_;
                first = index * piece_;
                last = std::min(count_, first + piece_);
            }

            if (first < last)
                (*job)(first, last);

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                --pending_;
            }

            done_.notify_one();
        }
    }

    std::vector<std::thread> threads_;

    std::mutex oneJob_;
    std::mutex mutex_;
    std::condition_variable start_;
    std::condition_variable done_;

    const std::function<void(int, int)> *job_ = nullptr;
    int count_ = 0;
    int piece_ = 0;
    int pending_ = 0;
    int generation_ = 0;
    bool quit_ = false;
};

template <typename Work>
void inParallel(int count, Work &&work)
{
    const std::function<void(int, int)> job = work;

    Workers::instance().run(count, job);
}

/*
 * The cross section of a three-strand rope at every angle around it,
 * measured from the middle of strand 0 (it turns along the rope with the
 * lay): the distance from the axis, the place across the strand on top,
 * which strand, how deep in a groove, and how much of the room the point
 * sees past the neighbouring strand. Worked out once.
 */
struct StrandSample
{
    float rho;
    float across;
    float which;
    float groove;
    float open;
};

constexpr int StrandSamples = 4096;

const std::vector<StrandSample> &strandTable()
{
    static const std::vector<StrandSample> table = []()
    {
        constexpr int GrooveTable = 64;

        double grooveTable[GrooveTable + 1];

        for (int k = 0; k <= GrooveTable; ++k)
            grooveTable[k] = grooveVisibility((k / double(GrooveTable) - 0.5) * 2.0 * Pi / 3.0);

        const double deepest = strandProfile(Pi / 3.0);

        std::vector<StrandSample> samples(StrandSamples);

        for (int k = 0; k < StrandSamples; ++k)
        {
            const double x = 2.0 * Pi * k / StrandSamples;

            // the strand standing out most at this angle
            int strand = 0;
            double best = -1.0;
            double delta = 0.0;

            for (int t = 0; t < 3; ++t)
            {
                const double d = wrapAngle(x - 2.0 * Pi * t / 3.0);
                const double r = strandProfile(d);

                if (r > best)
                {
                    best = r;
                    strand = t;
                    delta = d;
                }
            }

            const double u = std::clamp(0.5 + delta / (2.0 * Pi / 3.0), 0.0, 1.0) * GrooveTable;
            const int g = std::min(static_cast<int>(u), GrooveTable - 1);

            samples[k].rho = float(best);
            samples[k].across = float(0.5 + delta / (2.0 * Pi / 3.0));
            samples[k].which = float(strand);
            samples[k].groove = float((1.0 - best) / (1.0 - deepest));
            samples[k].open = float(grooveTable[g] + (grooveTable[g + 1] - grooveTable[g]) * (u - g));
        }

        return samples;
    }();

    return table;
}

/*
 * Loose fibres. A natural rope is hairy: the ends of short fibres stick
 * out of the yarns, most of them lying close along the strand and lifting
 * off a little, some curling away. They are thin tubes (three sides,
 * tapering) rooted on the tops of the strands; texture coordinates mark
 * them (w = -1, x = from root to tip). A braided cotton lace has far fewer
 * and shorter ones. The same rope always gets the same fibres.
 */
void addLooseFibres(KnotMath::Mesh &mesh, bool closed, double radius, bool braided, bool quick)
{
    const int n = mesh.ringCount;
    const int segments = mesh.ringSegments;

    if (n < 4 || segments < 3)
        return;

    const int vertices = mesh.positions.size() / 3;

    const bool colours = mesh.colors.size() == 3 * vertices;
    const bool samples = mesh.vertexSample.size() == vertices;

    const double length = mesh.centerlineArc.isEmpty()
        ? 0.0
        : mesh.strandLength.value(0, mesh.centerlineArc.last());

    // fibres per radius of rope, their length and thickness (in radii)
    // (a moving rope: half of them, the same ones)
    const double density = (braided ? 9.0 : 16.0) * (quick ? 0.4 : 1.0);

    // a cut end has a tuft of fibre ends standing out of it, shared by the two
    const int tufts = closed ? 0 : (quick ? 300 : 900);
    const double shortest = braided ? 0.1 : 0.16;
    const double longest = braided ? 0.5 : 1.3;
    const double thickness = braided ? 0.009 : 0.012;

    const int along = std::clamp(static_cast<int>(density * length / radius), 0, 40000);
    const int count = along + tufts;

    constexpr int Pieces = 5;
    constexpr int Sides = 3;

    constexpr int FibreVertices = (Pieces + 1) * Sides;
    constexpr int FibreIndices = Pieces * Sides * 6;

    // room for all of them at once; each fibre fills its own part
    const int firstIndex = mesh.indices.size();

    mesh.positions.resize(3 * (vertices + count * FibreVertices));
    mesh.normals.resize(3 * (vertices + count * FibreVertices));
    mesh.texcoords.resize(4 * (vertices + count * FibreVertices));
    mesh.occlusion.resize(vertices + count * FibreVertices);
    mesh.indices.resize(firstIndex + count * FibreIndices);

    if (colours)
        mesh.colors.resize(3 * (vertices + count * FibreVertices), 1.0f);

    if (samples)
        mesh.vertexSample.resize(vertices + count * FibreVertices);

    float *positions = mesh.positions.data();
    float *normals = mesh.normals.data();
    float *texcoords = mesh.texcoords.data();
    float *occlusionData = mesh.occlusion.data();
    unsigned int *indices = mesh.indices.data();
    int *sampleData = samples ? mesh.vertexSample.data() : nullptr;

    const float *centerline = mesh.centerline.constData();

    auto centreAt = [centerline](int ring)
    {
        return Vec3(centerline[4 * ring], centerline[4 * ring + 1], centerline[4 * ring + 2]);
    };

    inParallel(count, [&](int firstFibre, int lastFibre)
    {
        for (int f = firstFibre; f < lastFibre; ++f)
        {
            // random numbers of this fibre's own (xorshift from its number):
            // the same rope always gets the same fibres
            quint32 state = 0x9e3779b9u ^ (static_cast<quint32>(f) * 0x85ebca6bu);

            auto random = [&state]()
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;

                return (state & 0xffffff) / double(0x1000000);
            };

            random();
            random();

            const bool tuft = f >= along;

            const int ring = tuft ? ((f - along) % 2 == 0 ? 0 : n - 1) : std::min(static_cast<int>(random() * n), n - 1);

            // on top of a strand or yarn, never down in a groove
            int segment = std::min(static_cast<int>(random() * segments), segments - 1);

            for (int attempt = 0; attempt < 6; ++attempt)
            {
                if (texcoords[4 * (ring * segments + segment) + 3] < 0.35f)
                    break;

                segment = std::min(static_cast<int>(random() * segments), segments - 1);
            }

            const int root = ring * segments + segment;

            Vec3 base(positions[3 * root], positions[3 * root + 1], positions[3 * root + 2]);
            Vec3 normal = Vec3(normals[3 * root], normals[3 * root + 1], normals[3 * root + 2]).normalized();

            const int before = closed ? (ring - 1 + n) % n : std::max(ring - 1, 0);
            const int after = closed ? (ring + 1) % n : std::min(ring + 1, n - 1);

            Vec3 onward = centreAt(after) - centreAt(before);

            onward = (onward - normal * Vec3::dot(onward, normal)).normalized();

            Vec3 side = Vec3::cross(normal, onward);

            // either way along the rope, turned a little off it, lifting off
            const double turn = (random() - 0.5) * (braided ? 1.6 : 1.0);
            const double lift = 0.12 + 0.55 * random() * random();
            const double way = random() < 0.5 ? -1.0 : 1.0;

            Vec3 direction =
                (onward * (way * std::cos(turn)) + side * std::sin(turn)) * std::cos(lift) +
                normal * std::sin(lift);

            // most fibres short, a few long (the longest fibre ends of hemp)
            const double u = random();

            double fibre = (shortest + (longest - shortest) * u * u * u) * radius;

            // out of the face of a cut end, spreading from its middle: the
            // nearer the rim, the more
            if (tuft)
            {
                const Vec3 middle = centreAt(ring);
                const Vec3 out = (ring == 0 ? centreAt(0) - centreAt(1) : centreAt(n - 1) - centreAt(n - 2)).normalized();

                const Vec3 rim = base - middle;
                const double place = std::sqrt(random());

                base = middle + rim * place;
                normal = out;
                side = Vec3::cross(out, rim.normalized());

                direction = (out + rim.normalized() * (0.1 + 0.8 * place * random()) + side * (random() - 0.5)).normalized();

                fibre = (0.35 + 0.65 * random()) * fibre;
            }

            // it curls: turns a little at every piece, about the normal and away
            const double curl = (random() - 0.5) * 0.9;
            const double droop = (random() - 0.3) * 0.5;

            const int first = vertices + f * FibreVertices;
            const float occlusion = occlusionData[root];
            const float id = float(random());

            Vec3 point = base - normal * (0.05 * radius);

            for (int p = 0; p <= Pieces; ++p)
            {
                const double t = double(p) / Pieces;

                // a frame around the fibre
                const Vec3 helper = std::abs(Vec3::dot(direction, normal)) < 0.9 ? normal : side;
                const Vec3 a = Vec3::cross(direction, helper).normalized();
                const Vec3 b = Vec3::cross(direction, a);

                const double width = thickness * radius * (1.0 - 0.75 * t);

                for (int s = 0; s < Sides; ++s)
                {
                    const double angle = 2.0 * Pi * s / Sides;
                    const Vec3 out = a * std::cos(angle) + b * std::sin(angle);
                    const Vec3 position = point + out * width;

                    const int v = first + p * Sides + s;

                    positions[3 * v] = float(position.x);
                    positions[3 * v + 1] = float(position.y);
                    positions[3 * v + 2] = float(position.z);

                    normals[3 * v] = float(out.x);
                    normals[3 * v + 1] = float(out.y);
                    normals[3 * v + 2] = float(out.z);

                    texcoords[4 * v] = float(t);
                    texcoords[4 * v + 1] = 0.0f;
                    texcoords[4 * v + 2] = id;
                    texcoords[4 * v + 3] = -1.0f;

                    // the root sits among the yarns, the tip out in the open
                    occlusionData[v] = float(occlusion + (1.0 - occlusion) * t);

                    if (sampleData != nullptr)
                        sampleData[v] = ring;
                }

                point += direction * (fibre / Pieces);

                direction = (direction + side * (curl / Pieces) + normal * (droop / Pieces)).normalized();
            }

            unsigned int *out = indices + firstIndex + f * FibreIndices;

            for (int p = 0; p < Pieces; ++p)
            {
                for (int s = 0; s < Sides; ++s)
                {
                    const unsigned a0 = unsigned(first + p * Sides + s);
                    const unsigned a1 = unsigned(first + p * Sides + (s + 1) % Sides);
                    const unsigned b0 = a0 + Sides;
                    const unsigned b1 = a1 + Sides;

                    *out++ = a0;
                    *out++ = a1;
                    *out++ = b0;
                    *out++ = a1;
                    *out++ = b1;
                    *out++ = b0;
                }
            }
        }
    });
}

} // namespace

KnotMath::Mesh buildRopeMesh(
    const QVector<Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material,
    bool quick,
    const QVector<Vec3> &frames)
{
    const bool braided = material.construction == 2;

    /*
     * Rings close enough to follow the strands or yarns, and around. A
     * rope that moves (quick, built for every frame) is drawn lighter: in
     * motion the grooves need fewer points, and a long rope (a knot of a
     * dozen crossings) keeps to a budget of vertices.
     */
    double ringSpacing = (braided ? 0.12 : 0.2) * radius * (quick ? 1.4 : 1.0);
    const int segments = braided ? (quick ? 64 : 96) : (quick ? 48 : 72);

    constexpr double QuickVertices = 60000.0;

    const QVector<Vec3> smooth = KnotMath::refineCurve(curve, 4, closed);

    double length = 0.0;

    for (int i = 1; i < smooth.size(); ++i)
        length += (smooth.at(i) - smooth.at(i - 1)).length();

    if (closed)
        length += (smooth.first() - smooth.last()).length();

    if (quick)
        ringSpacing = std::max(ringSpacing, length * segments / QuickVertices);

    const int rings = std::clamp(
        static_cast<int>(std::lround(length / ringSpacing)),
        16,
        60000);

    const QVector<Vec3> line = KnotMath::resampleClosedCurve(smooth, rings, closed);

    KnotMath::Mesh mesh = KnotMath::buildTubeMeshWithCrossings(
        line,
        radius,
        segments,
        closed,
        KnotMath::ColorMode::SingleColor,
        {});

    if (!mesh.isValid() || mesh.ringSegments != segments)
        return mesh;

    const int n = mesh.ringCount;
    const int ringVertices = n * segments;

    const double total = mesh.strandLength.isEmpty() ? length : mesh.strandLength.first();

    /*
     * The pattern makes a whole number of turns on a closed rope, so it
     * closes up: strands once round per lay length, yarns once round per
     * 2 pi r / tan(braid angle).
     */
    const double period =
        braided ? 2.0 * Pi * radius / std::tan(BraidAngle) : LayLength * radius;

    const double turns =
        closed ? std::max(1.0, std::round(total / period)) : total / period;

    const double phaseScale = 2.0 * Pi * turns / std::max(total, 1.0e-9);

    /*
     * Where the material has turned to, along the rope: the given directions
     * by the fraction of the length, for every ring.
     */
    QVector<Vec3> ringFrames;

    if (frames.size() == curve.size() && curve.size() >= 4)
    {
        QVector<double> arc(curve.size(), 0.0);

        for (int i = 1; i < curve.size(); ++i)
            arc[i] = arc.at(i - 1) + (curve.at(i) - curve.at(i - 1)).length();

        const double whole =
            closed ? arc.last() + (curve.first() - curve.last()).length() : arc.last();

        ringFrames.resize(n);

        int at = 0;

        for (int i = 0; i < n; ++i)
        {
            const double s = whole * mesh.centerlineArc.at(i) / std::max(double(total), 1.0e-9);

            while (at + 1 < curve.size() && arc.at(at + 1) <= s)
                ++at;

            const int next = closed ? (at + 1) % curve.size() : std::min(at + 1, int(curve.size()) - 1);
            const double end = at + 1 < curve.size() ? arc.at(at + 1) : whole;
            const double f = std::clamp((s - arc.at(at)) / std::max(end - arc.at(at), 1.0e-12), 0.0, 1.0);

            ringFrames[i] = frames.at(at) * (1.0 - f) + frames.at(next) * f;
        }
    }

    /*
     * No rope is even. Its strands are a little thicker and thinner along
     * their length and the lay tightens and slackens, each strand on its
     * own: waves of whole numbers along a closed rope, so that it still
     * closes up.
     */
    const auto wave = [phaseScale, turns](double s, double perTurn, double offset)
    {
        return std::sin(std::round(turns * perTurn) / turns * s * phaseScale + offset);
    };

    QVector<float> openness(ringVertices, 1.0f);

    mesh.occlusion.resize(mesh.positions.size() / 3);

    // written through pointers from several threads at once (each ring by
    // one of them)
    float *positions = mesh.positions.data();
    float *normals = mesh.normals.data();
    float *texcoords = mesh.texcoords.data();
    float *occlusion = mesh.occlusion.data();
    float *open = openness.data();

    const float *centerline = mesh.centerline.constData();
    const float *arcs = mesh.centerlineArc.constData();

    const StrandSample *strands = strandTable().data();

    inParallel(n, [&](int first, int last)
    {
        for (int i = first; i < last; ++i)
        {
            const Vec3 centre(centerline[4 * i], centerline[4 * i + 1], centerline[4 * i + 2]);

            const double s = arcs[i];

            double phase = s * phaseScale + 0.09 * wave(s, 0.37, 0.4) + 0.05 * wave(s, 1.13, 2.2);

            // how far the strands open here (a cut end)
            double flare = 1.0;

            if (!closed)
            {
                const double open = 1.0 - std::min(s, total - s) / (EndFlareArc * radius);

                if (open > 0.0)
                    flare = 1.0 + EndFlare * open * open;
            }

            // how far the material has turned here
            if (!ringFrames.isEmpty())
            {
                const Vec3 first(normals[3 * i * segments], normals[3 * i * segments + 1],
                                 normals[3 * i * segments + 2]);

                const int quarter = i * segments + segments / 4;

                const Vec3 second(normals[3 * quarter], normals[3 * quarter + 1], normals[3 * quarter + 2]);

                phase += std::atan2(Vec3::dot(ringFrames.at(i), second), Vec3::dot(ringFrames.at(i), first));
            }

            for (int j = 0; j < segments; ++j)
            {
                const int index = i * segments + j;

                const double theta = 2.0 * Pi * j / segments;

                const Vec3 radial(normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]);

                double rho = 1.0;
                float across = 0.0f;
                float along = float(s / radius);
                float which = 0.0f;
                float groove = 0.0f;

                if (!braided)
                {
                    // the strand this point is on and where across it
                    // (the cross section turns with the lay: tabulated)
                    double x = std::fmod(theta - phase, 2.0 * Pi);

                    if (x < 0.0)
                        x += 2.0 * Pi;

                    const int k = static_cast<int>(x / (2.0 * Pi) * StrandSamples + 0.5) % StrandSamples;
                    const StrandSample &sample = strands[k];

                    across = sample.across;
                    which = sample.which;
                    groove = sample.groove;

                    // this strand, thicker or thinner here
                    rho = sample.rho * (1.0 + 0.016 * wave(s, 0.31, 1.7 + 2.1 * which) +
                                        0.009 * wave(s, 0.83, 4.1 * which));

                    open[index] = sample.open;
                }
                else
                {
                    /*
                     * Plain weave: a yarn of one direction is on top in
                     * every other cell. The yarn on top bulges; where it
                     * dives under a crossing yarn it sinks.
                     */
                    const double one = BraidYarns * (theta - phase) / (2.0 * Pi);
                    const double other = BraidYarns * (theta + phase) / (2.0 * Pi);

                    const double fOne = one - std::floor(one);
                    const double fOther = other - std::floor(other);

                    const double profileOne = std::sqrt(std::max(std::sin(Pi * fOne), 0.0));
                    const double profileOther = std::sqrt(std::max(std::sin(Pi * fOther), 0.0));

                    const double liftOne =
                        0.775 + 0.225 * std::cos(Pi * (other - 0.5 + std::floor(one)));
                    const double liftOther =
                        0.775 - 0.225 * std::cos(Pi * (one - 0.5 + std::floor(other)));

                    const double heightOne = profileOne * liftOne;
                    const double heightOther = profileOther * liftOther;

                    const bool oneOnTop = heightOne >= heightOther;
                    const double height = std::max(heightOne, heightOther);

                    rho = 1.0 - YarnHeight * (1.0 - height);
                    across = float(oneOnTop ? fOne : fOther);
                    along = float(oneOnTop ? other : one);
                    which = float((oneOnTop ? 0 : 1) + 2 * ((oneOnTop ? int(std::floor(one)) : int(std::floor(other))) & 7));
                    groove = float(1.0 - height);

                    open[index] = float(0.55 + 0.45 * std::sqrt(height));
                }

                const Vec3 position = centre + radial * (rho * flare * radius);

                positions[3 * index] = float(position.x);
                positions[3 * index + 1] = float(position.y);
                positions[3 * index + 2] = float(position.z);

                texcoords[4 * index] = across;
                texcoords[4 * index + 1] = along;
                texcoords[4 * index + 2] = which;
                texcoords[4 * index + 3] = groove;
            }
        }
    });

    /*
     * Normals of the real surface, from its neighbours on the grid of
     * rings and segments.
     */
    inParallel(n, [&](int first, int last)
    {
        auto at = [positions, segments](int ring, int segment)
        {
            const int k = 3 * (ring * segments + segment);

            return Vec3(positions[k], positions[k + 1], positions[k + 2]);
        };

        for (int i = first; i < last; ++i)
        {
            const int previous = closed ? (i - 1 + n) % n : std::max(i - 1, 0);
            const int next = closed ? (i + 1) % n : std::min(i + 1, n - 1);

            for (int j = 0; j < segments; ++j)
            {
                const int left = (j - 1 + segments) % segments;
                const int right = (j + 1) % segments;

                const Vec3 around = at(i, right) - at(i, left);
                const Vec3 along = at(next, j) - at(previous, j);

                Vec3 normal = Vec3::cross(around, along).normalized();

                const int index = i * segments + j;

                const Vec3 radial(normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]);

                if (Vec3::dot(normal, radial) < 0.0)
                    normal = normal * -1.0;

                normals[3 * index] = float(normal.x);
                normals[3 * index + 1] = float(normal.y);
                normals[3 * index + 2] = float(normal.z);
            }
        }
    });

    /*
     * The face of a cut end was made for a round tube, with a tube's texture
     * coordinates: it is laid over the end of the strands instead, and marked
     * for the shader.
     */
    if (!closed)
    {
        for (int cap = 0; cap < 2; ++cap)
        {
            const int ring = cap == 0 ? 0 : n - 1;
            const int middle = ringVertices + cap * (segments + 1);

            const auto vertex = [positions](int v)
            {
                return Vec3(positions[3 * v], positions[3 * v + 1], positions[3 * v + 2]);
            };

            // places on the face, in radii
            const Vec3 centre = vertex(middle);
            const Vec3 across = (vertex(ring * segments) - centre).normalized();
            const Vec3 out(normals[3 * middle], normals[3 * middle + 1], normals[3 * middle + 2]);
            const Vec3 up = Vec3::cross(out, across);

            for (int j = -1; j < segments; ++j)
            {
                const int v = middle + 1 + j;
                const int rim = ring * segments + std::max(j, 0);

                if (j >= 0)
                {
                    positions[3 * v] = positions[3 * rim];
                    positions[3 * v + 1] = positions[3 * rim + 1];
                    positions[3 * v + 2] = positions[3 * rim + 2];
                }

                const Vec3 place = vertex(v) - centre;

                texcoords[4 * v] = float(Vec3::dot(place, across) / radius);
                texcoords[4 * v + 1] = float(Vec3::dot(place, up) / radius);
                texcoords[4 * v + 2] = texcoords[4 * rim + 2];
                texcoords[4 * v + 3] = EndGrain;
            }
        }
    }

    /*
     * What the rest of the rope hides from each point: the view factor of
     * the nearby parts of the rope (cylinders of radius r), summed over
     * the segments of its centre line,
     *     dF = 2 r |s x d| max(n . d, 0) ds / (pi D^2),
     * s the direction of the segment, d the unit direction to it, D the
     * distance (for an endless straight rope this adds up to exactly the
     * view factor of a cylinder, r/D facing it, r^2/(x^2 + r^2) from a
     * table it lies on). Parts within a few radii along the rope are the
     * point's own neighbourhood (the grooves are counted separately).
     * Worked out for twelve directions around every ring and interpolated.
     */
    const int stride = std::max(1, static_cast<int>(std::lround(0.5 * radius / ringSpacing)));

    QVector<Vec3> axis;

    for (int i = 0; i < n; i += stride)
        axis.push_back(Vec3(centerline[4 * i], centerline[4 * i + 1], centerline[4 * i + 2]));

    const int m = axis.size();
    const double reach = 12.0 * radius;
    const int ownArc = static_cast<int>(std::ceil(3.0 * radius / (stride * ringSpacing)));

    constexpr int Directions = 12;

    QVector<float> hidden(n * Directions, 0.0f);

    float *hiddenData = hidden.data();

    inParallel(quick ? 0 : n, [&](int first, int last)
    {
        for (int i = first; i < last; ++i)
        {
            const Vec3 centre(centerline[4 * i], centerline[4 * i + 1], centerline[4 * i + 2]);
            const int own = i / stride;

            for (int dir = 0; dir < Directions; ++dir)
            {
                const int j = dir * segments / Directions;
                const int index = i * segments + j;

                const Vec3 normal(normals[3 * index], normals[3 * index + 1], normals[3 * index + 2]);

                // the outside of the rope at this angle
                const Vec3 radial(
                    positions[3 * index] - centre.x,
                    positions[3 * index + 1] - centre.y,
                    positions[3 * index + 2] - centre.z);

                const Vec3 point = centre + radial.normalized() * radius;

                double sum = 0.0;

                for (int k = 0; k < m; ++k)
                {
                    if (arcGap(k, own, m, closed) <= ownArc)
                        continue;

                    const int k1 = closed ? (k + 1) % m : k + 1;

                    if (k1 >= m)
                        continue;

                    const Vec3 middle = (axis.at(k) + axis.at(k1)) * 0.5;
                    Vec3 d = middle - point;

                    const double distance = d.length();

                    if (distance > reach || distance < 1.0e-9)
                        continue;

                    d = d / distance;

                    const double facing = Vec3::dot(normal, d);

                    if (facing <= 0.0)
                        continue;

                    const Vec3 piece = axis.at(k1) - axis.at(k);
                    const double ds = piece.length();

                    if (ds < 1.0e-12)
                        continue;

                    const double sine = Vec3::cross(piece / ds, d).length();
                    const double clamped = std::max(distance, 1.2 * radius);

                    sum += 2.0 * radius * sine * facing * ds / (Pi * clamped * clamped);
                }

                hiddenData[i * Directions + dir] = float(std::min(sum, 0.9));
            }
        }
    });

    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < segments; ++j)
        {
            const double u = double(j) * Directions / segments;
            const int a = static_cast<int>(u) % Directions;
            const int b = (a + 1) % Directions;
            const double f = u - std::floor(u);

            const double rope =
                hiddenData[i * Directions + a] * (1.0 - f) + hiddenData[i * Directions + b] * f;

            const int index = i * segments + j;

            occlusion[index] = float((1.0 - rope) * open[index]);
        }
    }

    // the end caps of an open rope: as open as their ring
    for (int k = ringVertices; k < mesh.occlusion.size(); ++k)
        mesh.occlusion[k] = 1.0f;

    addLooseFibres(mesh, closed, radius, braided, quick);

    return mesh;
}

} // namespace Photo
