#include "photoscene.h"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace Photo
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

// Direction of the point (u, v) of an equirectangular picture (both 0..1,
// v = 0 at the top): y up, u = 0.5 straight ahead along -z. The shaders
// use the inverse: u = 0.5 + atan(d.x, -d.z) / 2 pi, v = acos(d.y) / pi.
QVector3D directionAt(double u, double v)
{
    const double phi = (u - 0.5) * 2.0 * Pi;
    const double theta = v * Pi;

    return QVector3D(
        static_cast<float>(std::sin(theta) * std::sin(phi)),
        static_cast<float>(std::cos(theta)),
        static_cast<float>(-std::sin(theta) * std::cos(phi)));
}

float luminance(const QVector3D &c)
{
    return 0.2126f * c.x() + 0.7152f * c.y() + 0.0722f * c.z();
}

// The nine real spherical harmonics up to second order at `d`.
void harmonics(const QVector3D &d, float y[9])
{
    const float x = d.x();
    const float yy = d.y();
    const float z = d.z();

    y[0] = 0.282095f;
    y[1] = 0.488603f * yy;
    y[2] = 0.488603f * z;
    y[3] = 0.488603f * x;
    y[4] = 1.092548f * x * yy;
    y[5] = 1.092548f * yy * z;
    y[6] = 0.315392f * (3.0f * z * z - 1.0f);
    y[7] = 1.092548f * x * z;
    y[8] = 0.546274f * (x * x - yy * yy);
}

// one line of text of the header (without the newline), or false at the end
bool readLine(const QByteArray &data, int &pos, QByteArray &line)
{
    const int end = data.indexOf('\n', pos);

    if (end < 0)
        return false;

    line = data.mid(pos, end - pos);
    pos = end + 1;

    return true;
}

} // namespace

QVector3D Environment::irradianceAt(const QVector3D &normal) const
{
    float y[9];

    harmonics(normal, y);

    QVector3D e;

    for (int i = 0; i < 9; ++i)
        e += irradiance[i] * y[i];

    return QVector3D(std::max(e.x(), 0.0f), std::max(e.y(), 0.0f), std::max(e.z(), 0.0f));
}

Environment loadEnvironment(const QString &path)
{
    Environment environment;

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly))
    {
        environment.error = QStringLiteral("cannot open %1").arg(path);
        return environment;
    }

    const QByteArray data = file.readAll();

    /*
     * Header: "#?RADIANCE", some VAR=value lines, an empty line, then the
     * size as "-Y height +X width" (rows from the top, left to right).
     */
    int pos = 0;
    QByteArray line;

    if (!readLine(data, pos, line) || !line.startsWith("#?"))
    {
        environment.error = QStringLiteral("not a Radiance picture");
        return environment;
    }

    while (readLine(data, pos, line) && !line.isEmpty())
    {
        if (line.startsWith("FORMAT=") && line != "FORMAT=32-bit_rle_rgbe")
        {
            environment.error = QStringLiteral("only RGBE pictures can be read");
            return environment;
        }
    }

    if (!readLine(data, pos, line))
    {
        environment.error = QStringLiteral("no picture size");
        return environment;
    }

    const QList<QByteArray> size = line.split(' ');

    if (size.size() != 4 || size.at(0) != "-Y" || size.at(2) != "+X")
    {
        environment.error = QStringLiteral("unsupported picture orientation");
        return environment;
    }

    const int width = size.at(3).toInt();
    const int height = size.at(1).toInt();

    if (width < 16 || height < 8 || width > 16384 || height > 8192)
    {
        environment.error = QStringLiteral("unsupported picture size");
        return environment;
    }

    /*
     * Pixels: red, green, blue mantissas sharing one exponent. A new-style
     * run-length line starts with 2, 2 and its width, then holds the four
     * channels one after the other, each as runs (count > 128: a repeated
     * byte) and literal stretches. Anything else is a flat line.
     */
    const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData());
    const int total = data.size();

    QVector<unsigned char> row(width * 4);

    environment.pixels.resize(width * height * 3);

    for (int y = 0; y < height; ++y)
    {
        if (pos + 4 > total)
        {
            environment.error = QStringLiteral("the picture is cut short");
            return environment;
        }

        const bool runLength =
            width >= 8 && width < 32768 &&
            bytes[pos] == 2 && bytes[pos + 1] == 2 &&
            ((bytes[pos + 2] << 8) | bytes[pos + 3]) == width;

        if (runLength)
        {
            pos += 4;

            for (int channel = 0; channel < 4; ++channel)
            {
                int x = 0;

                while (x < width)
                {
                    if (pos >= total)
                    {
                        environment.error = QStringLiteral("the picture is cut short");
                        return environment;
                    }

                    int count = bytes[pos++];

                    if (count > 128)
                    {
                        count -= 128;

                        if (count > width - x || pos >= total)
                        {
                            environment.error = QStringLiteral("damaged picture");
                            return environment;
                        }

                        const unsigned char value = bytes[pos++];

                        for (int i = 0; i < count; ++i)
                            row[(x + i) * 4 + channel] = value;
                    }
                    else
                    {
                        if (count == 0 || count > width - x || pos + count > total)
                        {
                            environment.error = QStringLiteral("damaged picture");
                            return environment;
                        }

                        for (int i = 0; i < count; ++i)
                            row[(x + i) * 4 + channel] = bytes[pos++];
                    }

                    x += count;
                }
            }
        }
        else
        {
            if (pos + width * 4 > total)
            {
                environment.error = QStringLiteral("the picture is cut short");
                return environment;
            }

            std::copy(bytes + pos, bytes + pos + width * 4, row.begin());

            pos += width * 4;
        }

        float *out = environment.pixels.data() + y * width * 3;

        for (int x = 0; x < width; ++x)
        {
            const int exponent = row[x * 4 + 3];

            // mantissa / 256 * 2^(exponent - 128)
            const float scale =
                exponent > 0 ? static_cast<float>(std::ldexp(1.0, exponent - 136)) : 0.0f;

            out[x * 3] = row[x * 4] * scale;
            out[x * 3 + 1] = row[x * 4 + 1] * scale;
            out[x * 3 + 2] = row[x * 4 + 2] * scale;
        }
    }

    environment.width = width;
    environment.height = height;

    /*
     * The sun. An unclipped photograph keeps its true brightness: many
     * thousand times the brightest wall. It is the brightest point above
     * the horizon, together with everything around it brighter than a
     * hundredth of that (the disc and the glow of the glass it shines
     * through), found pixel by pixel at full resolution. What it has above
     * the room around it becomes the sun's light; the room keeps the rest,
     * so nothing is counted twice.
     */
    auto luminanceAt = [&environment, width](int x, int y)
    {
        const float *p = environment.pixels.constData() + (y * width + x) * 3;

        return luminance(QVector3D(p[0], p[1], p[2]));
    };

    // solid angle of a pixel in row y
    auto pixelSolidAngle = [width, height](int y)
    {
        return (2.0 * Pi / width) * (Pi / height) * std::sin((y + 0.5) / height * Pi);
    };

    int peakX = 0;
    int peakY = 0;
    float peak = -1.0f;

    for (int y = 0; y < height / 2; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float l = luminanceAt(x, y);

            if (l > peak)
            {
                peak = l;
                peakX = x;
                peakY = y;
            }
        }
    }

    const float threshold = 0.01f * peak;

    // the region around the peak (the picture wraps around left and right)
    QVector<unsigned char> inSun(width * height, 0);
    QVector<int> region;
    QVector<int> queue;

    queue.push_back(peakY * width + peakX);
    inSun[peakY * width + peakX] = 1;

    // at most about 4 degrees across: a sun, not a window
    const int largest = static_cast<int>(0.015 / ((2.0 * Pi / width) * (Pi / height)));

    while (!queue.isEmpty() && region.size() < largest)
    {
        const int i = queue.takeLast();

        region.push_back(i);

        const int x = i % width;
        const int y = i / width;

        const int neighbours[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};

        for (const auto &n : neighbours)
        {
            const int nx = (n[0] + width) % width;
            const int ny = n[1];

            if (ny < 0 || ny >= height)
                continue;

            const int j = ny * width + nx;

            if (inSun[j] == 0 && luminanceAt(nx, ny) > threshold)
            {
                inSun[j] = 1;
                queue.push_back(j);
            }
        }
    }

    // the room around it: the median of the pixels next to the region
    QVector<float> rim;

    for (const int i : region)
    {
        const int x = i % width;
        const int y = i / width;

        for (int dy = -2; dy <= 2; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                const int nx = (x + dx + width) % width;
                const int ny = y + dy;

                if (ny >= 0 && ny < height && inSun[ny * width + nx] == 0)
                    rim.push_back(luminanceAt(nx, ny));
            }
        }
    }

    float around = 0.0f;

    if (!rim.isEmpty())
    {
        std::nth_element(rim.begin(), rim.begin() + rim.size() / 2, rim.end());
        around = rim.at(rim.size() / 2);
    }

    // its light, its middle (weighted by brightness) and its size; the
    // picture keeps the sun (it is seen in the room and in reflections),
    // the room's soft light is computed without it
    QVector<QVector3D> taken(region.size());

    QVector3D sunLight;
    QVector3D middle;
    double weight = 0.0;

    for (int k = 0; k < region.size(); ++k)
    {
        const int i = region.at(k);
        const int x = i % width;
        const int y = i / width;

        float *p = environment.pixels.data() + i * 3;

        const QVector3D c(p[0], p[1], p[2]);
        const float l = luminance(c);

        if (l <= around)
            continue;

        // what lies above the room, in its own colour
        const QVector3D above = c * ((l - around) / l);
        const double omega = pixelSolidAngle(y);

        sunLight += above * static_cast<float>(omega);
        middle += directionAt((x + 0.5) / width, (y + 0.5) / height) *
                  static_cast<float>((l - around) * omega);
        weight += (l - around) * omega;

        // the room is left with the light around the sun
        const QVector3D rest = c * (around / l);

        taken[k] = c - rest;

        p[0] = rest.x();
        p[1] = rest.y();
        p[2] = rest.z();
    }

    if (weight > 0.0)
    {
        environment.sunDirection = middle.normalized();

        // for light spread evenly over a disc of radius r the mean square
        // angle from its middle is r^2 / 2
        double spread = 0.0;

        for (const int i : region)
        {
            const QVector3D d =
                directionAt((i % width + 0.5) / width, (i / width + 0.5) / height);

            const double angle = std::acos(std::clamp(
                static_cast<double>(QVector3D::dotProduct(d, environment.sunDirection)),
                -1.0,
                1.0));

            spread += angle * angle * pixelSolidAngle(i / width);
        }

        // the size of the region itself (each pixel equally): the light is
        // concentrated in its middle, so this is the most it can be
        double area = 0.0;

        for (const int i : region)
            area += pixelSolidAngle(i / width);

        const double meanSquare = spread / std::max(area, 1.0e-12);

        // the sun itself is 0.27 degrees in radius; the glow around it
        // (glass, haze) makes the light larger and its shadows softer
        environment.sunRadius = static_cast<float>(std::clamp(
            std::sqrt(2.0 * meanSquare),
            0.0047,
            0.05));

        environment.sunIrradiance = sunLight;
    }

    /*
     * The room's light as spherical harmonics, multiplied by the cosine
     * lobe (pi, 2 pi / 3, pi / 4 for the three orders): irradiance. Only
     * the upper half: below the horizon the knot sees the surface it lies
     * on, not the room's floor far below. Done on a small copy of the
     * picture (each texel the average of a block).
     */
    const int smallWidth = std::min(256, width);
    const int smallHeight = std::min(128, height);

    const int blockX = width / smallWidth;
    const int blockY = height / smallHeight;

    for (QVector3D &c : environment.irradiance)
        c = QVector3D();

    for (int ty = 0; ty < smallHeight / 2; ++ty)
    {
        const double v = (ty + 0.5) / smallHeight;

        const float omega = static_cast<float>(
            (2.0 * Pi / smallWidth) * (Pi / smallHeight) * std::sin(v * Pi));

        for (int tx = 0; tx < smallWidth; ++tx)
        {
            QVector3D sum;

            for (int by = 0; by < blockY; ++by)
            {
                const float *p =
                    environment.pixels.constData() +
                    ((ty * blockY + by) * width + tx * blockX) * 3;

                for (int bx = 0; bx < blockX; ++bx)
                    sum += QVector3D(p[bx * 3], p[bx * 3 + 1], p[bx * 3 + 2]);
            }

            const QVector3D color = sum / static_cast<float>(blockX * blockY);

            float y[9];

            harmonics(directionAt((tx + 0.5) / smallWidth, v), y);

            for (int k = 0; k < 9; ++k)
                environment.irradiance[k] += color * (y[k] * omega);
        }
    }

    const float lobe[9] = {
        static_cast<float>(Pi),
        static_cast<float>(2.0 * Pi / 3.0),
        static_cast<float>(2.0 * Pi / 3.0),
        static_cast<float>(2.0 * Pi / 3.0),
        static_cast<float>(Pi / 4.0),
        static_cast<float>(Pi / 4.0),
        static_cast<float>(Pi / 4.0),
        static_cast<float>(Pi / 4.0),
        static_cast<float>(Pi / 4.0)};

    for (int k = 0; k < 9; ++k)
        environment.irradiance[k] *= lobe[k];

    // the sun back into the picture
    for (int k = 0; k < region.size(); ++k)
    {
        float *p = environment.pixels.data() + region.at(k) * 3;

        p[0] += taken.at(k).x();
        p[1] += taken.at(k).y();
        p[2] += taken.at(k).z();
    }

    environment.valid = true;

    return environment;
}

QMatrix4x4 flatPose(const QVector<QVector3D> &points)
{
    /*
     * A rope lies on a table with its thinnest direction up. That is the
     * direction in which the points spread least: the eigenvector of the
     * smallest eigenvalue of their covariance (Jacobi rotations). The
     * longest direction goes along x.
     */
    QMatrix4x4 pose;

    const int samples = points.size();

    if (samples < 3)
        return pose;

    double mean[3] = {0.0, 0.0, 0.0};

    for (const QVector3D &p : points)
    {
        mean[0] += p.x();
        mean[1] += p.y();
        mean[2] += p.z();
    }

    for (double &m : mean)
        m /= samples;

    double a[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};

    for (const QVector3D &p : points)
    {
        const double d[3] = {p.x() - mean[0], p.y() - mean[1], p.z() - mean[2]};

        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
                a[i][j] += d[i] * d[j];
        }
    }

    // eigenvectors are the columns of v
    double v[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

    for (int sweep = 0; sweep < 50; ++sweep)
    {
        // the largest entry off the diagonal
        int p = 0;
        int q = 1;

        if (std::abs(a[0][2]) > std::abs(a[p][q]))
        {
            p = 0;
            q = 2;
        }

        if (std::abs(a[1][2]) > std::abs(a[p][q]))
        {
            p = 1;
            q = 2;
        }

        const double scale = std::abs(a[0][0]) + std::abs(a[1][1]) + std::abs(a[2][2]);

        if (std::abs(a[p][q]) <= 1.0e-12 * std::max(scale, 1.0e-30))
            break;

        // the rotation in the (p, q) plane that clears a[p][q]
        const double theta = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        const double c = std::cos(theta);
        const double s = std::sin(theta);

        for (int k = 0; k < 3; ++k)
        {
            const double kp = a[k][p];
            const double kq = a[k][q];

            a[k][p] = c * kp - s * kq;
            a[k][q] = s * kp + c * kq;
        }

        for (int k = 0; k < 3; ++k)
        {
            const double pk = a[p][k];
            const double qk = a[q][k];

            a[p][k] = c * pk - s * qk;
            a[q][k] = s * pk + c * qk;
        }

        for (int k = 0; k < 3; ++k)
        {
            const double kp = v[k][p];
            const double kq = v[k][q];

            v[k][p] = c * kp - s * kq;
            v[k][q] = s * kp + c * kq;
        }
    }

    int smallest = 0;
    int largestAxis = 0;

    for (int i = 1; i < 3; ++i)
    {
        if (a[i][i] < a[smallest][smallest])
            smallest = i;

        if (a[i][i] > a[largestAxis][largestAxis])
            largestAxis = i;
    }

    if (smallest == largestAxis)
        return pose;

    auto column = [&v](int i)
    {
        return QVector3D(
            static_cast<float>(v[0][i]),
            static_cast<float>(v[1][i]),
            static_cast<float>(v[2][i])).normalized();
    };

    QVector3D up = column(smallest);
    QVector3D along = column(largestAxis);

    // the least turn: the side that faced up (or the camera) stays up
    if (up.y() + 0.5f * up.z() < 0.0f)
        up = -up;

    if (along.x() < 0.0f)
        along = -along;

    // exactly at right angles to up
    along = (along - up * QVector3D::dotProduct(along, up)).normalized();

    const QVector3D third = QVector3D::crossProduct(along, up);

    // rows: the knot's directions go to x, y (up) and z
    const QMatrix4x4 rotation(
        along.x(), along.y(), along.z(), 0.0f,
        up.x(), up.y(), up.z(), 0.0f,
        third.x(), third.y(), third.z(), 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);

    const QVector3D centre(
        static_cast<float>(mean[0]),
        static_cast<float>(mean[1]),
        static_cast<float>(mean[2]));

    // turned about its own middle, so it stays where it was
    pose.translate(centre);
    pose *= rotation;
    pose.translate(-centre);

    return pose;
}

SceneInfo sceneInfo(Scene scene)
{
    SceneInfo info;

    switch (scene)
    {
    case Scene::Sneaker:
        // the ballroom's floor, a navy canvas sneaker on it
        info.environment = QStringLiteral(":/data/photo/country_club_4k.hdr");
        info.cameraHeightM = 1.6;
        info.roomRadiusM = 9.0;

        info.surfaceColor = QStringLiteral(":/data/photo/wood_floor_worn_diff.jpg");
        info.surfaceNormal = QStringLiteral(":/data/photo/wood_floor_worn_nor_gl.jpg");
        info.surfaceRoughness = QStringLiteral(":/data/photo/wood_floor_worn_rough.jpg");
        info.surfaceTileMm = 2000.0;

        // a white braided cotton lace, 4.5 mm (it fits the eyelets); it
        // holds on the canvas a little better than on the floor
        info.rope.construction = 2;
        info.rope.diameterMm = 4.5;
        info.rope.bendingLengthMm = 20.0;
        info.rope.frictionSurface = 0.6;
        info.rope.frictionSelf = 0.5;
        info.rope.color = QColor(240, 238, 232);
        info.rope.roughness = 0.8f;

        info.sneaker = true;
        info.canvas = QColor(28, 38, 66);
        break;

    case Scene::BallroomFloor:
        // a Victorian ballroom in the morning sun: tall windows, a worn
        // board floor
        info.environment = QStringLiteral(":/data/photo/country_club_4k.hdr");
        info.cameraHeightM = 1.6;
        info.roomRadiusM = 9.0;

        info.surfaceColor = QStringLiteral(":/data/photo/wood_floor_worn_diff.jpg");
        info.surfaceNormal = QStringLiteral(":/data/photo/wood_floor_worn_nor_gl.jpg");
        info.surfaceRoughness = QStringLiteral(":/data/photo/wood_floor_worn_rough.jpg");
        info.surfaceTileMm = 2000.0;

        // a round braided cotton lace, 5 mm: it bends under its own weight
        // within a couple of centimetres
        info.rope.construction = 2;
        info.rope.diameterMm = 5.0;
        info.rope.bendingLengthMm = 20.0;
        info.rope.frictionSurface = 0.4;
        info.rope.frictionSelf = 0.5;
        info.rope.color = QColor(236, 232, 224);
        info.rope.roughness = 0.8f;
        break;

    case Scene::LoungeTable:
    case Scene::Count:
    default:
        // a ship's lounge at sunrise: the low sun through the windows
        info.environment = QStringLiteral(":/data/photo/aft_lounge_4k.hdr");
        info.cameraHeightM = 1.6;
        info.roomRadiusM = 12.0;

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
        info.rope.frictionSurface = 0.5;
        info.rope.frictionSelf = 0.7;
        info.rope.color = QColor(196, 166, 118);
        info.rope.roughness = 0.75f;
        break;
    }

    return info;
}

} // namespace Photo
