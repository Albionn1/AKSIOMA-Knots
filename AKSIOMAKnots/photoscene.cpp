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
     * The light is found on a small copy of the picture (each texel the
     * average of a block), with the solid angle every texel covers.
     */
    const int smallWidth = std::min(256, width);
    const int smallHeight = std::min(128, height);

    const int blockX = width / smallWidth;
    const int blockY = height / smallHeight;

    const int texels = smallWidth * smallHeight;

    QVector<QVector3D> color(texels);
    QVector<QVector3D> direction(texels);
    QVector<float> solidAngle(texels);

    for (int ty = 0; ty < smallHeight; ++ty)
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

            const int i = ty * smallWidth + tx;

            color[i] = sum / static_cast<float>(blockX * blockY);
            direction[i] = directionAt((tx + 0.5) / smallWidth, v);
            solidAngle[i] = omega;
        }
    }

    /*
     * The main light: texels much brighter than the room around them. The
     * cut is the brightest 5% of the texels (a window is large), and at
     * least twice the average brightness: about as much light comes from
     * the main light as from the rest of the room together, or more, as in
     * a photograph lit by a window. The light is the brightest cluster of what lies
     * above the cut: the 25 degree cone holding most of it, widened to 35
     * degrees. What it has above the cut becomes the light; the room keeps
     * the rest (so nothing is counted twice).
     */
    QVector<float> lum(texels);

    double average = 0.0;
    double area = 0.0;

    for (int i = 0; i < texels; ++i)
    {
        lum[i] = luminance(color[i]);

        average += lum[i] * solidAngle[i];
        area += solidAngle[i];
    }

    average /= std::max(area, 1e-9);

    QVector<float> sorted = lum;

    std::nth_element(sorted.begin(), sorted.begin() + (texels * 95) / 100, sorted.end());

    const float cut =
        std::max(sorted.at((texels * 95) / 100), static_cast<float>(2.0 * average));

    QVector<int> bright;

    for (int i = 0; i < texels; ++i)
    {
        if (lum[i] > cut)
            bright.push_back(i);
    }

    auto excess = [&](int i)
    {
        return (lum[i] - cut) * solidAngle[i];
    };

    QVector<QVector3D> room = color;

    if (!bright.isEmpty())
    {
        const float narrow = static_cast<float>(std::cos(25.0 * Pi / 180.0));
        const float wide = static_cast<float>(std::cos(35.0 * Pi / 180.0));

        // the cone of 25 degrees around a bright texel holding the most
        int best = bright.first();
        float bestEnergy = -1.0f;

        for (const int centre : bright)
        {
            float energy = 0.0f;

            for (const int i : bright)
            {
                if (QVector3D::dotProduct(direction[i], direction[centre]) > narrow)
                    energy += excess(i);
            }

            if (energy > bestEnergy)
            {
                bestEnergy = energy;
                best = centre;
            }
        }

        // its middle, weighted by brightness
        QVector3D middle;

        for (const int i : bright)
        {
            if (QVector3D::dotProduct(direction[i], direction[best]) > narrow)
                middle += direction[i] * excess(i);
        }

        const QVector3D key = middle.normalized();

        QVector3D irradiance;
        double weight = 0.0;
        double spread = 0.0;

        for (const int i : bright)
        {
            const float facing = QVector3D::dotProduct(direction[i], key);

            if (facing <= wide)
                continue;

            // what lies above the cut, in its own colour
            const QVector3D above = color[i] * ((lum[i] - cut) / lum[i]);

            irradiance += above * (solidAngle[i] * facing);

            weight += excess(i);
            spread += excess(i) * std::acos(std::min(facing, 1.0f));

            room[i] = color[i] * (cut / lum[i]);
        }

        environment.keyDirection = key;
        environment.keyIrradiance = irradiance;

        // for light spread evenly over a disk of radius r the average angle
        // from its middle is 2r/3
        environment.keyRadius = static_cast<float>(std::clamp(
            1.5 * spread / std::max(weight, 1e-12),
            0.02,
            0.35));
    }

    /*
     * The room's light as spherical harmonics, multiplied by the cosine
     * lobe (pi, 2 pi / 3, pi / 4 for the three orders): irradiance.
     */
    for (QVector3D &c : environment.irradiance)
        c = QVector3D();

    for (int i = 0; i < texels; ++i)
    {
        float y[9];

        harmonics(direction[i], y);

        for (int k = 0; k < 9; ++k)
            environment.irradiance[k] += room[i] * (y[k] * solidAngle[i]);
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

    environment.valid = true;

    return environment;
}

SceneInfo sceneInfo(Scene scene)
{
    SceneInfo info;

    switch (scene)
    {
    case Scene::Shoelaces:
        info.environment = QStringLiteral(":/data/photo/lebombo_2k.hdr");
        info.floorColor = QStringLiteral(":/data/photo/laminate_floor_02_diff.jpg");
        info.floorNormal = QStringLiteral(":/data/photo/laminate_floor_02_nor_gl.jpg");
        info.floorRoughness = QStringLiteral(":/data/photo/laminate_floor_02_rough.jpg");
        info.floorTileMm = 1700.0;
        info.environmentYaw = 0.0f;
        info.material = 4;
        info.color = QColor(236, 232, 224);
        info.roughness = 0.75f;
        info.ropeMm = 5.0;
        break;

    case Scene::StudioTable:
    case Scene::Count:
    default:
        info.environment = QStringLiteral(":/data/photo/brown_photostudio_02_2k.hdr");
        info.floorColor = QStringLiteral(":/data/photo/wood_table_001_diff.jpg");
        info.floorNormal = QStringLiteral(":/data/photo/wood_table_001_nor_gl.jpg");
        info.floorRoughness = QStringLiteral(":/data/photo/wood_table_001_rough.jpg");
        info.floorTileMm = 1500.0;
        info.environmentYaw = 0.0f;
        info.material = 3;
        info.color = QColor(201, 170, 120);
        info.roughness = 0.7f;
        info.ropeMm = 12.0;
        break;
    }

    return info;
}

} // namespace Photo
