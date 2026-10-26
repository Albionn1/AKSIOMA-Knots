#include "ropephysics.h"

#include <algorithm>
#include <cmath>

namespace Photo
{

namespace
{

using KnotMath::Vec3;

constexpr double Pi = 3.14159265358979323846;

constexpr double Gravity = 9.81; // m/s^2

/*
 * The simulation works in units of the rope's radius (r = 1) and of the
 * time sqrt(r / g) (g = 1); the rope weighs 1 per unit of length.
 */

// points per radius of rope
constexpr double Spacing = 0.5;

// parts of the rope closer than this along it never collide (they are
// neighbours; tubes this close along the curve may overlap, as in
// KnotMath::safeTubeRadius)
constexpr double NeighbourArc = 4.0;

// time step, and the longest a rope may take to come to rest
constexpr double TimeStep = 0.04;
constexpr double LongestTime = 200.0;

// at rest: no point faster than this (radii per time unit) for RestSteps
constexpr double RestSpeed = 0.003;
constexpr int RestSteps = 150;

// constraint passes per step
constexpr int Passes = 3;

// internal damping of the fibres (per time unit)
constexpr double Damping = 0.8;

// no point moves more than this in one step (no tunnelling through the
// rope: two radii wide)
constexpr double MaxStep = 0.12;

// closest points of the segments a0-a1 and b0-b1: parameters s, t (0..1)
void closestPoints(
    const Vec3 &a0,
    const Vec3 &a1,
    const Vec3 &b0,
    const Vec3 &b1,
    double &s,
    double &t)
{
    const Vec3 d1 = a1 - a0;
    const Vec3 d2 = b1 - b0;
    const Vec3 r = a0 - b0;

    const double a = Vec3::dot(d1, d1);
    const double e = Vec3::dot(d2, d2);
    const double f = Vec3::dot(d2, r);
    const double c = Vec3::dot(d1, r);
    const double b = Vec3::dot(d1, d2);

    const double denominator = a * e - b * b;

    s = denominator > 1.0e-12 ? std::clamp((b * f - c * e) / denominator, 0.0, 1.0) : 0.0;
    t = (b * s + f) / e;

    if (t < 0.0)
    {
        t = 0.0;
        s = std::clamp(-c / a, 0.0, 1.0);
    }
    else if (t > 1.0)
    {
        t = 1.0;
        s = std::clamp((b - c) / a, 0.0, 1.0);
    }
}

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
    const RopeMaterial &material)
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

    // the true spacing along the rope (the resampling is close to even)
    const int segments = closed ? n : n - 1;

    QVector<double> rest(segments);

    for (int i = 0; i < segments; ++i)
        rest[i] = (x.at((i + 1) % n) - x.at(i)).length();

    double h = 0.0;

    for (const double l : rest)
        h += l;

    h /= std::max(segments, 1);

    /*
     * The material in these units. Bending stiffness B = w c^3 with w = 1:
     * B = c^3, c the bending length in radii. One point stands for a
     * length h of rope: mass h (inverse mass 1/h).
     */
    const double radiusMm = 0.5 * material.diameterMm;
    const double c = material.bendingLengthMm / radiusMm;
    const double bending = c * c * c;

    const double dt = TimeStep;

    // XPBD: compliance of the bending constraint C = turning angle at a
    // point (curvature theta / h, energy B h kappa^2 / 2 = B theta^2 / 2h)
    const double compliance = h / bending / (dt * dt);

    const double muSurface = material.frictionSurface;
    const double muSelf = material.frictionSelf;

    const int neighbours = static_cast<int>(std::ceil(NeighbourArc / h));

    QVector<Vec3> v(n);
    QVector<Vec3> p(n);

    // segments sorted into columns of a grid on the table (x, z), each a
    // little wider than the reach of a contact
    constexpr double Cell = 2.6;

    QVector<int> cellOf(segments);
    QVector<int> order(segments);
    QVector<int> cellStart;

    int quiet = 0;
    int step = 0;
    double fastest = 0.0;

    const int longest = static_cast<int>(LongestTime / dt);

    for (step = 0; step < longest; ++step)
    {
        // gravity, damping, predicted positions
        const double keep = std::exp(-Damping * dt);

        for (int i = 0; i < n; ++i)
        {
            v[i].y -= dt;
            v[i] *= keep;

            const double speed = v.at(i).length();

            if (speed * dt > MaxStep)
                v[i] *= MaxStep / (speed * dt);

            p[i] = x.at(i) + v.at(i) * dt;
        }

        for (int pass = 0; pass < Passes; ++pass)
        {
            // the rope does not stretch
            for (int i = 0; i < segments; ++i)
            {
                const int j = (i + 1) % n;

                const Vec3 d = p.at(j) - p.at(i);
                const double l = d.length();

                if (l < 1.0e-9)
                    continue;

                const Vec3 move = d * (0.5 * (l - rest.at(i)) / l);

                p[i] += move;
                p[j] -= move;
            }

            /*
             * It resists bending: the turning angle theta between the two
             * pieces at a point, with the gradients
             *     d theta / d a = n1 / |e1|,  d theta / d b = -n2 / |e2|,
             *     d theta / d i = -(d theta / d a + d theta / d b),
             * e1 = i - a, e2 = b - i, n1 and n2 the unit directions at
             * right angles to them in their plane, towards each other.
             */
            for (int i = closed ? 0 : 1; i < (closed ? n : n - 1); ++i)
            {
                const int a = (i - 1 + n) % n;
                const int b = (i + 1) % n;

                const Vec3 e1 = p.at(i) - p.at(a);
                const Vec3 e2 = p.at(b) - p.at(i);

                const double l1 = e1.length();
                const double l2 = e2.length();

                if (l1 < 1.0e-9 || l2 < 1.0e-9)
                    continue;

                const Vec3 u1 = e1 / l1;
                const Vec3 u2 = e2 / l2;

                const double cosine = std::clamp(Vec3::dot(u1, u2), -1.0, 1.0);
                const double sine = Vec3::cross(u1, u2).length();
                const double theta = std::atan2(sine, cosine);

                if (sine < 1.0e-6)
                    continue;

                const Vec3 n1 = (u2 - u1 * cosine) / sine;
                const Vec3 n2 = (u1 - u2 * cosine) / sine;

                const Vec3 ga = n1 / l1;
                const Vec3 gb = n2 * (-1.0 / l2);
                const Vec3 gi = (ga + gb) * -1.0;

                const double inverseMass = 1.0 / h;

                const double denominator =
                    inverseMass * (ga.lengthSquared() + gb.lengthSquared() + gi.lengthSquared()) +
                    compliance;

                const double dl = -theta / denominator;

                p[a] += ga * (inverseMass * dl);
                p[b] += gb * (inverseMass * dl);
                p[i] += gi * (inverseMass * dl);
            }
        }

        /*
         * Contacts of the rope with itself: two segments far apart along
         * the rope stay two radii apart (centre lines). The push is shared
         * by their four points by where the closest points lie; friction
         * takes away the sliding of the two parts against each other.
         */
        double minX = 1.0e30;
        double minZ = 1.0e30;
        double maxX = -1.0e30;
        double maxZ = -1.0e30;

        for (const Vec3 &q : p)
        {
            minX = std::min(minX, q.x);
            minZ = std::min(minZ, q.z);
            maxX = std::max(maxX, q.x);
            maxZ = std::max(maxZ, q.z);
        }

        const int columns = std::max(1, static_cast<int>((maxX - minX) / Cell) + 1);
        const int rows = std::max(1, static_cast<int>((maxZ - minZ) / Cell) + 1);

        cellStart.fill(0, columns * rows + 1);

        for (int i = 0; i < segments; ++i)
        {
            const Vec3 middle = (p.at(i) + p.at((i + 1) % n)) * 0.5;

            const int cx = std::clamp(static_cast<int>((middle.x - minX) / Cell), 0, columns - 1);
            const int cz = std::clamp(static_cast<int>((middle.z - minZ) / Cell), 0, rows - 1);

            cellOf[i] = cz * columns + cx;
            ++cellStart[cellOf.at(i) + 1];
        }

        for (int k = 0; k < columns * rows; ++k)
            cellStart[k + 1] += cellStart.at(k);

        {
            QVector<int> fill = cellStart;

            for (int i = 0; i < segments; ++i)
                order[fill[cellOf.at(i)]++] = i;
        }

        for (int i = 0; i < segments; ++i)
        {
            const int cx = cellOf.at(i) % columns;
            const int cz = cellOf.at(i) / columns;

            for (int dz = -1; dz <= 1; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int ox = cx + dx;
                    const int oz = cz + dz;

                    if (ox < 0 || oz < 0 || ox >= columns || oz >= rows)
                        continue;

                    const int cell = oz * columns + ox;

                    for (int k = cellStart.at(cell); k < cellStart.at(cell + 1); ++k)
                    {
                        const int j = order.at(k);

                        if (j <= i || arcGap(i, j, n, closed) <= neighbours)
                            continue;

                        const int i1 = (i + 1) % n;
                        const int j1 = (j + 1) % n;

                        double s = 0.0;
                        double t = 0.0;

                        closestPoints(p.at(i), p.at(i1), p.at(j), p.at(j1), s, t);

                        const Vec3 pa = p.at(i) * (1.0 - s) + p.at(i1) * s;
                        const Vec3 pb = p.at(j) * (1.0 - t) + p.at(j1) * t;

                        const Vec3 d = pa - pb;
                        const double distance = d.length();

                        if (distance >= 2.0)
                            continue;

                        const Vec3 normal =
                            distance > 1.0e-9 ? d / distance : Vec3(0.0, 1.0, 0.0);

                        const double depth = 2.0 - distance;

                        const double wa0 = 1.0 - s;
                        const double wa1 = s;
                        const double wb0 = 1.0 - t;
                        const double wb1 = t;

                        const double share = wa0 * wa0 + wa1 * wa1 + wb0 * wb0 + wb1 * wb1;

                        const Vec3 push = normal * (depth / share);

                        p[i] += push * wa0;
                        p[i1] += push * wa1;
                        p[j] -= push * wb0;
                        p[j1] -= push * wb1;

                        // sliding of the two parts against each other in this step
                        const Vec3 moveA =
                            (p.at(i) - x.at(i)) * wa0 + (p.at(i1) - x.at(i1)) * wa1;
                        const Vec3 moveB =
                            (p.at(j) - x.at(j)) * wb0 + (p.at(j1) - x.at(j1)) * wb1;

                        Vec3 slide = moveA - moveB;
                        slide -= normal * Vec3::dot(slide, normal);

                        const double amount = slide.length();

                        if (amount < 1.0e-12)
                            continue;

                        // static friction holds, kinetic friction slows
                        const double stop =
                            amount < muSelf * depth ? 1.0 : std::min(muSelf * depth / amount, 1.0);

                        const Vec3 hold = slide * (stop / share);

                        p[i] -= hold * wa0;
                        p[i1] -= hold * wa1;
                        p[j] += hold * wb0;
                        p[j1] += hold * wb1;
                    }
                }
            }
        }

        // the table (with friction)
        for (int i = 0; i < n; ++i)
        {
            if (p.at(i).y >= 1.0)
                continue;

            const double depth = 1.0 - p.at(i).y;

            p[i].y = 1.0;

            const double sx = p.at(i).x - x.at(i).x;
            const double sz = p.at(i).z - x.at(i).z;
            const double amount = std::sqrt(sx * sx + sz * sz);

            if (amount < muSurface * depth)
            {
                p[i].x = x.at(i).x;
                p[i].z = x.at(i).z;
            }
            else
            {
                const double slow = std::min(muSurface * depth / amount, 1.0);

                p[i].x -= sx * slow;
                p[i].z -= sz * slow;
            }
        }

        // velocities from the motion
        fastest = 0.0;

        for (int i = 0; i < n; ++i)
        {
            v[i] = (p.at(i) - x.at(i)) / dt;
            x[i] = p.at(i);

            fastest = std::max(fastest, v.at(i).length());
        }

        quiet = fastest < RestSpeed ? quiet + 1 : 0;

        if (quiet >= RestSteps)
            break;
    }

    const double timeUnit = std::sqrt(radiusMm * 0.001 / Gravity);

    result.steps = step;
    result.seconds = step * dt * timeUnit;
    result.finalSpeed = fastest * radiusMm / timeUnit;
    result.settled = quiet >= RestSteps;

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

    result.valid = true;

    return result;
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

} // namespace

KnotMath::Mesh buildRopeMesh(
    const QVector<Vec3> &curve,
    bool closed,
    double radius,
    const RopeMaterial &material)
{
    const bool braided = material.construction == 2;

    // rings close enough to follow the strands or yarns, and around
    const double ringSpacing = (braided ? 0.12 : 0.2) * radius;
    const int segments = braided ? 96 : 72;

    const QVector<Vec3> smooth = KnotMath::refineCurve(curve, 4, closed);

    double length = 0.0;

    for (int i = 1; i < smooth.size(); ++i)
        length += (smooth.at(i) - smooth.at(i - 1)).length();

    if (closed)
        length += (smooth.first() - smooth.last()).length();

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

    // the groove's openness across a strand, tabulated
    constexpr int GrooveTable = 64;

    double grooveTable[GrooveTable + 1];

    if (!braided)
    {
        for (int k = 0; k <= GrooveTable; ++k)
            grooveTable[k] = grooveVisibility((k / double(GrooveTable) - 0.5) * 2.0 * Pi / 3.0);
    }

    const double deepest = strandProfile(Pi / 3.0);

    QVector<float> openness(ringVertices, 1.0f);

    mesh.occlusion.resize(mesh.positions.size() / 3);

    for (int i = 0; i < n; ++i)
    {
        const Vec3 centre(
            mesh.centerline.at(4 * i),
            mesh.centerline.at(4 * i + 1),
            mesh.centerline.at(4 * i + 2));

        const double s = mesh.centerlineArc.at(i);
        const double phase = s * phaseScale;

        for (int j = 0; j < segments; ++j)
        {
            const int index = i * segments + j;

            const double theta = 2.0 * Pi * j / segments;

            const Vec3 radial(
                mesh.normals.at(3 * index),
                mesh.normals.at(3 * index + 1),
                mesh.normals.at(3 * index + 2));

            double rho = 1.0;
            float across = 0.0f;
            float along = float(s / radius);
            float which = 0.0f;
            float groove = 0.0f;

            if (!braided)
            {
                // the strand this point is on: the one standing out most
                int strand = 0;
                double best = -1.0;
                double delta = 0.0;

                for (int k = 0; k < 3; ++k)
                {
                    const double d = wrapAngle(theta - phase - 2.0 * Pi * k / 3.0);
                    const double r = strandProfile(d);

                    if (r > best)
                    {
                        best = r;
                        strand = k;
                        delta = d;
                    }
                }

                rho = best;
                across = float(0.5 + delta / (2.0 * Pi / 3.0));
                which = float(strand);
                groove = float((1.0 - best) / (1.0 - deepest));

                const double u = std::clamp(0.5 + delta / (2.0 * Pi / 3.0), 0.0, 1.0) * GrooveTable;
                const int k = std::min(static_cast<int>(u), GrooveTable - 1);

                openness[index] =
                    float(grooveTable[k] + (grooveTable[k + 1] - grooveTable[k]) * (u - k));
            }
            else
            {
                /*
                 * Plain weave: a yarn of one direction is on top in every
                 * other cell. The yarn on top bulges; where it dives under
                 * a crossing yarn it sinks.
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

                openness[index] = float(0.55 + 0.45 * std::sqrt(height));
            }

            const Vec3 position = centre + radial * (rho * radius);

            mesh.positions[3 * index] = float(position.x);
            mesh.positions[3 * index + 1] = float(position.y);
            mesh.positions[3 * index + 2] = float(position.z);

            mesh.texcoords[4 * index] = across;
            mesh.texcoords[4 * index + 1] = along;
            mesh.texcoords[4 * index + 2] = which;
            mesh.texcoords[4 * index + 3] = groove;
        }
    }

    /*
     * Normals of the real surface, from its neighbours on the grid of
     * rings and segments.
     */
    for (int i = 0; i < n; ++i)
    {
        const int previous = closed ? (i - 1 + n) % n : std::max(i - 1, 0);
        const int next = closed ? (i + 1) % n : std::min(i + 1, n - 1);

        for (int j = 0; j < segments; ++j)
        {
            const int left = (j - 1 + segments) % segments;
            const int right = (j + 1) % segments;

            auto at = [&mesh, segments](int ring, int segment)
            {
                const int k = 3 * (ring * segments + segment);

                return Vec3(mesh.positions.at(k), mesh.positions.at(k + 1), mesh.positions.at(k + 2));
            };

            const Vec3 around = at(i, right) - at(i, left);
            const Vec3 along = at(next, j) - at(previous, j);

            Vec3 normal = Vec3::cross(around, along).normalized();

            const int index = i * segments + j;

            const Vec3 radial(
                mesh.normals.at(3 * index),
                mesh.normals.at(3 * index + 1),
                mesh.normals.at(3 * index + 2));

            if (Vec3::dot(normal, radial) < 0.0)
                normal = normal * -1.0;

            mesh.normals[3 * index] = float(normal.x);
            mesh.normals[3 * index + 1] = float(normal.y);
            mesh.normals[3 * index + 2] = float(normal.z);
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
    QVector<int> axisRing;

    for (int i = 0; i < n; i += stride)
    {
        axis.push_back(Vec3(mesh.centerline.at(4 * i), mesh.centerline.at(4 * i + 1), mesh.centerline.at(4 * i + 2)));
        axisRing.push_back(i);
    }

    const int m = axis.size();
    const double reach = 12.0 * radius;
    const int ownArc = static_cast<int>(std::ceil(3.0 * radius / (stride * ringSpacing)));

    constexpr int Directions = 12;

    QVector<float> hidden(n * Directions, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const Vec3 centre(mesh.centerline.at(4 * i), mesh.centerline.at(4 * i + 1), mesh.centerline.at(4 * i + 2));
        const int own = i / stride;

        for (int dir = 0; dir < Directions; ++dir)
        {
            const int j = dir * segments / Directions;
            const int index = i * segments + j;

            const Vec3 normal(
                mesh.normals.at(3 * index),
                mesh.normals.at(3 * index + 1),
                mesh.normals.at(3 * index + 2));

            // the outside of the rope at this angle
            const Vec3 radial(
                mesh.positions.at(3 * index) - centre.x,
                mesh.positions.at(3 * index + 1) - centre.y,
                mesh.positions.at(3 * index + 2) - centre.z);

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

            hidden[i * Directions + dir] = float(std::min(sum, 0.9));
        }
    }

    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < segments; ++j)
        {
            const double u = double(j) * Directions / segments;
            const int a = static_cast<int>(u) % Directions;
            const int b = (a + 1) % Directions;
            const double f = u - std::floor(u);

            const double rope =
                hidden.at(i * Directions + a) * (1.0 - f) + hidden.at(i * Directions + b) * f;

            const int index = i * segments + j;

            mesh.occlusion[index] = float((1.0 - rope) * openness.at(index));
        }
    }

    // the end caps of an open rope: as open as their ring
    for (int k = ringVertices; k < mesh.occlusion.size(); ++k)
        mesh.occlusion[k] = 1.0f;

    return mesh;
}

} // namespace Photo
