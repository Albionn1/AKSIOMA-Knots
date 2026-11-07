#include "knotoverlay.h"

#include "theme.h"
#include "trainlayer.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QRadialGradient>
#include <QTextDocument>

#include <algorithm>
#include <cmath>

KnotOverlay::KnotOverlay(Aksioma::Renderer &view, const TrainLayer &train)
    : view_(view),
      train_(train),
      captionClock_(view.clock())
{
}

void KnotOverlay::resetJourney()
{
    journeyBridge_ = 0;
}

bool KnotOverlay::film() const
{
    return train_.cinema();
}

bool KnotOverlay::journeyShown() const
{
    return train_.showTrain() && !train_.journey().isEmpty() && !film();
}

bool KnotOverlay::labelsShown() const
{
    // (the bridges are always numbered: the journey panel speaks of them)
    return (showCrossingNumbers_ || journeyShown()) && !crossings_.isEmpty() && !film();
}

bool KnotOverlay::hasOverlay(const Aksioma::Renderer &) const
{
    const bool flashes = !flashPositions_.isEmpty();
    const bool caption = !captionTitle_.isEmpty() && !film();

    return labelsShown() || flashes || showInset_ || caption || film() || journeyShown();
}

void KnotOverlay::paintOverlay(Aksioma::Renderer &, QPainter &painter)
{
    const bool film = this->film();
    const bool journey = journeyShown();
    const bool labels = labelsShown();
    const bool flashes = !flashPositions_.isEmpty();
    const bool caption = !captionTitle_.isEmpty() && !film;

    // the wide picture of a film, between black bars
    if (film)
    {
        const int bar = std::max(0, (view_.height() - static_cast<int>(view_.width() / 2.39f)) / 2);

        painter.fillRect(0, 0, view_.width(), bar, Qt::black);
        painter.fillRect(0, view_.height() - bar, view_.width(), bar, Qt::black);
    }

    if (labels)
        drawCrossingLabels(painter);

    if (flashes)
        drawFlashes(painter);

    if (showInset_)
        drawDiagramInset(painter);

    if (caption)
        drawCaption(painter);

    if (journey)
        drawJourney(painter);
}

void KnotOverlay::setFlashes(
    const QVector<KnotMath::Vec3> &positions,
    const QVector<float> &progress,
    const QVector<int> &numbers)
{
    flashPositions_ = positions;
    flashProgress_ = progress;
    flashNumbers_ = numbers;
    view_.update();
}

void KnotOverlay::clearFlashes()
{
    flashPositions_.clear();
    flashProgress_.clear();
    flashNumbers_.clear();
    view_.update();
}

void KnotOverlay::drawFlashes(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    const double scale =
        std::max(
            1.0,
            static_cast<double>(std::min(view_.width(), view_.height())) / 600.0);

    for (int i = 0; i < flashPositions_.size() &&
                    i < flashProgress_.size(); ++i)
    {
        const double p = std::clamp(
            static_cast<double>(flashProgress_.at(i)), 0.0, 1.0);

        if (p <= 0.0 || p >= 1.0)
            continue;

        const QPointF centre = view_.projectPointToScreen(flashPositions_.at(i));

        const double fade = (1.0 - p) * (1.0 - p);
        const double radius = (10.0 + 90.0 * p) * scale;

        const QColor ring(255, 235, 140,
                          static_cast<int>(255.0 * fade));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ring, 3.0 * (1.0 - p) + 1.0));
        painter.drawEllipse(centre, radius, radius);

        const double r2 = radius * 0.55;
        painter.setPen(QPen(QColor(255, 255, 255,
                                   static_cast<int>(200.0 * fade)), 2.0));
        painter.drawEllipse(centre, r2, r2);

        QRadialGradient glow(centre, radius * 0.8);
        glow.setColorAt(0.0, QColor(255, 250, 200,
                                    static_cast<int>(170.0 * fade)));
        glow.setColorAt(1.0, QColor(255, 220, 90, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(centre, radius * 0.8, radius * 0.8);

        // number 0: a flash without a number
        if (i < flashNumbers_.size() && flashNumbers_.at(i) > 0)
        {
            QFont font = painter.font();
            font.setBold(true);
            font.setPointSize(11);
            painter.setFont(font);
            painter.setPen(QColor(255, 245, 190,
                                  static_cast<int>(255.0 * (1.0 - p))));
            painter.drawText(
                QPointF(centre.x() + radius * 0.7,
                        centre.y() - radius * 0.7 - 6.0 * p * scale),
                QString::number(flashNumbers_.at(i)));
        }
    }
}

KnotOverlay::DiagramInset KnotOverlay::DiagramInset::fromUnknotting(
    const KnotMath::UnknottingAnimation::Diagram &diagram,
    int changesDone,
    int activeChange)
{
    DiagramInset inset;

    inset.points = diagram.points;

    for (const KnotMath::UnknottingAnimation::DiagramCrossing &crossing :
         diagram.crossings)
    {
        Crossing shown;

        shown.position = crossing.position;
        shown.change = crossing.change;

        // after its crossing change the other strand is on top
        const bool changed =
            crossing.change > 0 && crossing.change <= changesDone;

        const int over =
            changed ? 1 - crossing.overAtStart : crossing.overAtStart;

        shown.overU = crossing.u[over];
        shown.underU = crossing.u[1 - over];

        if (crossing.change == 0)
            shown.state = 0;
        else if (crossing.change == activeChange)
            shown.state = 2;
        else
            shown.state = changed ? 3 : 1;

        // the colours stay with the strands: blue started on top
        shown.swapped = changed;

        inset.crossings.push_back(shown);
    }

    return inset;
}

void KnotOverlay::setDiagramInset(const DiagramInset &inset)
{
    inset_ = inset;
    showInset_ = inset.points.size() >= 3;
    view_.update();
}

void KnotOverlay::clearDiagramInset()
{
    inset_ = DiagramInset();
    showInset_ = false;
    view_.update();
}

void KnotOverlay::setCaption(const QString &title, const QString &body)
{
    if (title == captionTitle_ && body == captionBody_)
        return;

    // a new caption fades in; the same one with other words stays
    if (title != captionTitle_)
        captionClock_.restart();

    captionTitle_ = title;
    captionBody_ = body;

    view_.update();
}

void KnotOverlay::drawJourney(QPainter &painter)
{
    const int count = train_.journey().size();

    // the front of the train, and the first pass that is not behind it
    const double head = std::fmod(train_.trainAlong() + 48.0, train_.trainLength());

    constexpr double OnIt = 110.0;

    int next = 0;

    while (next < count && train_.journey().at(next).along < head - OnIt)
        ++next;

    const bool lapped = next == count;

    if (lapped)
        next = 0;

    const Photo::TrainTrack::Pass &pass = train_.journey().at(next);

    const bool onIt = !lapped && std::abs(pass.along - head) < OnIt;

    journeyBridge_ = pass.bridge;

    const QString title =
        onIt ? (pass.over ? tr("Over bridge %1").arg(pass.bridge) : tr("Under bridge %1").arg(pass.bridge))
             : (pass.over ? tr("Next: over bridge %1").arg(pass.bridge) : tr("Next: under bridge %1").arg(pass.bridge));

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont titleFont = painter.font();

    titleFont.setBold(true);
    titleFont.setPointSize(10);

    QFont chipFont = titleFont;

    chipFont.setPointSize(8);

    constexpr int Margin = 12;
    constexpr int Pad = 10;
    constexpr int ChipW = 34;
    constexpr int ChipH = 22;
    constexpr int Gap = 4;

    const int room = std::max(4, (view_.width() - 2 * Margin - 2 * Pad - 110) / (ChipW + Gap));
    const int perRow = std::min(count, std::min(room, 13));
    const int rows = (count + perRow - 1) / perRow;

    const QFontMetrics titleMetrics(titleFont);

    const int wide = 2 * Pad + perRow * ChipW + (perRow - 1) * Gap;
    const int high = 2 * Pad + titleMetrics.height() + 6 + rows * ChipH + (rows - 1) * Gap;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(16, 16, 18, 200));
    painter.drawRoundedRect(QRectF(Margin, Margin, wide, high), 8.0, 8.0);

    painter.setFont(titleFont);
    painter.setPen(Qt::white);
    painter.drawText(QRectF(Margin + Pad, Margin + Pad, wide - 2 * Pad, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter, title);

    painter.setFont(chipFont);

    const int top = Margin + Pad + titleMetrics.height() + 6;

    for (int k = 0; k < count; ++k)
    {
        const Photo::TrainTrack::Pass &item = train_.journey().at(k);

        const QRectF chip(Margin + Pad + (k % perRow) * (ChipW + Gap), top + (k / perRow) * (ChipH + Gap), ChipW, ChipH);

        const bool passed = lapped || k < next;

        const QColor colour = item.over ? QColor(226, 160, 66) : QColor(86, 146, 214);

        QColor fill = colour;

        fill.setAlpha(passed ? 235 : (k == next ? 255 : 55));

        painter.setPen(k == next ? QPen(Qt::white, 2.0) : Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(chip, 5.0, 5.0);

        painter.setPen(passed || k == next ? QColor(16, 16, 18) : QColor(230, 230, 230));

        painter.drawText(chip, Qt::AlignCenter, QStringLiteral("%1%2").arg(item.bridge).arg(item.over ? QChar(0x2191) : QChar(0x2193)));
    }
}

void KnotOverlay::drawCaption(QPainter &painter)
{
    // to the right of the diagram in the lower left corner, at the foot
    const double inset = showInset_ ? std::clamp(0.32 * std::min(view_.width(), view_.height()), 180.0, 300.0) + 32.0 : 16.0;

    const double room = view_.width() - inset - 16.0;
    const double wide = std::min(room, 640.0);

    if (wide < 120.0)
        return;

    QTextDocument text;

    QFont font = painter.font();
    font.setPointSizeF(10.0);
    text.setDefaultFont(font);
    text.setDocumentMargin(14.0);
    text.setTextWidth(wide);
    text.setDefaultStyleSheet(QStringLiteral("body { color: #E8E6E0; } b { color: #FFFFFF; }"));
    text.setHtml(QStringLiteral("<body><b>%1</b><br>%2</body>").arg(captionTitle_.toHtmlEscaped(), captionBody_));

    const double tall = text.size().height();

    const QRectF panel(inset + 0.5 * (room - wide), view_.height() - tall - 16.0, wide, tall);

    // fades in over a third of a second
    const double fade =
        captionClock_.isValid()
            ? std::clamp(static_cast<double>(captionClock_.elapsed()) / 350.0, 0.0, 1.0)
            : 1.0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(fade);

    painter.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    painter.setBrush(QColor(10, 10, 10, 190));
    painter.drawRoundedRect(panel, 6.0, 6.0);

    painter.translate(panel.topLeft());
    text.drawContents(&painter);

    painter.restore();

    if (fade < 1.0)
        view_.update();
}

void KnotOverlay::drawDiagramInset(QPainter &painter)
{
    const QVector<QPointF> &points = inset_.points;
    const int n = points.size();

    if (n < 3)
        return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // panel in the lower left corner
    const double side =
        std::clamp(0.32 * std::min(view_.width(), view_.height()), 180.0, 300.0);

    const QRectF panel(16.0, view_.height() - side - 16.0, side, side);

    const QColor background(14, 14, 14, 225);

    painter.setPen(QPen(QColor(48, 47, 45), 1.0));
    painter.setBrush(background);
    painter.drawRoundedRect(panel, 6.0, 6.0);

    QFont font = painter.font();
    font.setPointSizeF(7.5);
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    painter.setFont(font);
    painter.setPen(QColor(138, 135, 128));
    painter.drawText(
        panel.adjusted(12.0, 8.0, -12.0, 0.0),
        Qt::AlignLeft | Qt::AlignTop,
        inset_.caption.isEmpty() ? tr("KNOT DIAGRAM") : inset_.caption);

    paintDiagram(painter, panel.adjusted(18.0, 28.0, -18.0, -18.0), inset_, background);
}

void KnotOverlay::paintDiagram(
    QPainter &painter,
    const QRectF &area,
    const DiagramInset &inset,
    const QColor &background)
{
    const QVector<QPointF> &points = inset.points;
    const int n = points.size();

    if (n < 3)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // fit the diagram (y up) into the area
    double minX = points.first().x();
    double maxX = minX;
    double minY = points.first().y();
    double maxY = minY;

    for (const QPointF &p : points)
    {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }

    const double scale =
        std::min(area.width() / std::max(maxX - minX, 1.0e-9),
                 area.height() / std::max(maxY - minY, 1.0e-9));

    const QPointF offset(
        area.center().x() - 0.5 * (minX + maxX) * scale,
        area.center().y() + 0.5 * (minY + maxY) * scale);

    auto map = [&](const QPointF &p)
    {
        return QPointF(offset.x() + p.x() * scale, offset.y() - p.y() * scale);
    };

    // a short piece of the curve around position u (0..1)
    auto piece = [&](double u, double halfLength)
    {
        QPainterPath path;

        const int reach = std::max(2, static_cast<int>(std::ceil(halfLength * n)));
        const int centre = static_cast<int>(std::lround(u * n));

        for (int k = -reach; k <= reach; ++k)
        {
            const QPointF p = map(points.at(((centre + k) % n + n) % n));

            if (k == -reach)
                path.moveTo(p);
            else
                path.lineTo(p);
        }

        return path;
    };

    const QColor strandColor(205, 201, 192);
    const QColor overColor(91, 155, 255);
    const QColor underColor(217, 155, 66);
    const QColor doneColor(108, 192, 139);

    const double strandWidth = 2.4;
    const double gapWidth = 9.0;
    const double pieceLength = 0.025;

    // whole curve
    QPainterPath curve;
    curve.moveTo(map(points.first()));

    for (int i = 1; i < n; ++i)
        curve.lineTo(map(points.at(i)));

    curve.closeSubpath();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(strandColor, strandWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(curve);

    // crossings: the strand below is broken by the one on top
    for (const DiagramInset::Crossing &crossing : inset.crossings)
    {
        const bool active = crossing.state == 2;

        // blue: the strand that started on top, amber: the other one
        const QColor topColor = crossing.swapped ? underColor : overColor;
        const QColor bottomColor = crossing.swapped ? overColor : underColor;

        if (active)
        {
            painter.setPen(QPen(bottomColor, strandWidth + 0.8, Qt::SolidLine, Qt::RoundCap));
            painter.drawPath(piece(crossing.underU, 1.6 * pieceLength));
        }

        const QPainterPath over = piece(crossing.overU, pieceLength);

        painter.setPen(QPen(background, gapWidth, Qt::SolidLine, Qt::FlatCap));
        painter.drawPath(over);

        painter.setPen(QPen(active ? topColor : strandColor,
                            strandWidth + (active ? 0.8 : 0.0),
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawPath(over);
    }

    // markers of the crossing changes
    QFont numberFont = painter.font();
    numberFont.setPointSizeF(7.5);
    numberFont.setBold(true);
    numberFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    painter.setFont(numberFont);

    for (const DiagramInset::Crossing &crossing : inset.crossings)
    {
        if (crossing.change <= 0)
            continue;

        const QPointF c = map(crossing.position);

        const QColor color =
            crossing.state == 3 ? doneColor : underColor;

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, crossing.state == 2 ? 2.4 : 1.4));
        painter.drawEllipse(c, crossing.state == 2 ? 10.0 : 8.0, crossing.state == 2 ? 10.0 : 8.0);

        painter.setPen(color);
        painter.drawText(
            QRectF(c.x() + 7.0, c.y() - 20.0, 20.0, 14.0),
            Qt::AlignLeft | Qt::AlignVCenter,
            QString::number(crossing.change));
    }

    // base point and walking direction
    const QPointF base = map(points.first());
    const QPointF ahead = map(points.at(std::min(n - 1, 3)));

    QPointF direction = ahead - base;
    const double length = std::hypot(direction.x(), direction.y());

    painter.setPen(QPen(QColor(20, 20, 20), 1.2));
    painter.setBrush(QColor(245, 245, 240));
    painter.drawEllipse(base, 4.5, 4.5);

    if (length > 1.0e-6)
    {
        direction /= length;

        const QPointF normal(-direction.y(), direction.x());
        const QPointF tip = base + direction * 16.0;

        QPolygonF arrow;
        arrow << tip
              << tip - direction * 7.0 + normal * 4.0
              << tip - direction * 7.0 - normal * 4.0;

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(245, 245, 240));
        painter.drawPolygon(arrow);
    }

    painter.restore();
}

void KnotOverlay::setCrossings(
    const QVector<KnotMath::CrossingInfo> &crossings)
{
    crossings_ = crossings;

    view_.update();
}

void KnotOverlay::setShowCrossingNumbers(bool enabled)
{
    showCrossingNumbers_ = enabled;

    view_.update();
}

void KnotOverlay::drawCrossingLabels(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font = painter.font();

    font.setBold(true);
    font.setPointSize(9);

    painter.setFont(font);

    const QFontMetrics metrics(font);

    for (const KnotMath::CrossingInfo &crossing : crossings_)
    {
        const QPointF position = view_.projectPointToScreen(crossing.midpoint);

        if (position.x() < -50.0 ||
            position.x() > static_cast<double>(view_.width()) + 50.0 ||
            position.y() < -50.0 ||
            position.y() > static_cast<double>(view_.height()) + 50.0)
        {
            continue;
        }

        const bool nextBridge = crossing.number == journeyBridge_ && train_.showTrain();

        const QColor labelColor =
            nextBridge ? QColor(255, 214, 120) : KnotMath::crossingColor(crossing.number - 1);

        const QString text = QString::number(crossing.number);

        const QRectF textRect = metrics.boundingRect(text);

        const double radius =
            std::max(
                10.0,
                std::max(textRect.width(), textRect.height()) * 0.75) * (nextBridge ? 1.3 : 1.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(18, 18, 18, 225));
        painter.drawEllipse(position, radius, radius);

        painter.setPen(QPen(labelColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(position, radius, radius);

        painter.setPen(Qt::white);

        const QRectF centeredRect(
            position.x() - radius,
            position.y() - radius,
            2.0 * radius,
            2.0 * radius);

        painter.drawText(centeredRect, Qt::AlignCenter, text);
    }
}
