#include "diagrameditor.h"

#include "theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
// from the current theme
QColor kBackground() { return Theme::colors().window; }
QColor kLine() { return Theme::colors().text; }
QColor kMuted() { return Theme::colors().muted; }
QColor kAccent() { return Theme::colors().accent; }

constexpr double kLineWidth = 5.0;
constexpr double kGapPixels = 12.0;
constexpr double kHalfExtent = 2.0;

// a press that moves less than this is a click (it makes a move)
constexpr double kClickPixels = 6.0;

// the ring around a place too tight for the tube (red on either theme)
constexpr double kTightRingPixels = 22.0;
const QColor kTight(224, 82, 74);

// how long each move takes on screen (ms)
constexpr int kMoveDuration[4] = {0, 1100, 1300, 1500};

// Undo keeps this many steps
constexpr int kHistory = 60;
}

/*
 * =========================================================
 * CANVAS
 * =========================================================
 */

DiagramCanvas::DiagramCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    animation_ = new QVariantAnimation(this);

    animation_->setStartValue(0.0);
    animation_->setEndValue(1.0);

    // the frames ease in and out themselves
    animation_->setEasingCurve(QEasingCurve::Linear);

    connect(animation_, &QVariantAnimation::valueChanged,
            this, [this](const QVariant &value)
            {
                frame_ = KnotMath::reidemeisterFrame(activeMove_, value.toDouble());

                update();
            });

    connect(animation_, &QVariantAnimation::finished,
            this, [this]()
            {
                const int type = activeMove_.type;

                setDiagram(activeMove_.result, false);

                activeMove_ = KnotMath::ReidemeisterMove();
                frame_ = KnotMath::KnotDiagram();

                emit diagramChanged();
                emit moveFinished(type);
            });
}

const KnotMath::KnotDiagram &DiagramCanvas::diagram() const
{
    return diagram_;
}

const QVector<KnotMath::ReidemeisterMove> &DiagramCanvas::moves() const
{
    return moves_;
}

int DiagramCanvas::hoveredMoveType() const
{
    return (hoverMove_ >= 0 && hoverMove_ < moves_.size())
               ? moves_.at(hoverMove_).type
               : 0;
}

int DiagramCanvas::activeMoveType() const
{
    return isAnimating() ? activeMove_.type : 0;
}

bool DiagramCanvas::isAnimating() const
{
    return animation_->state() == QAbstractAnimation::Running;
}

bool DiagramCanvas::canUndo() const
{
    return !history_.isEmpty() && !isAnimating();
}

QSize DiagramCanvas::minimumSizeHint() const
{
    return QSize(420, 340);
}

QSize DiagramCanvas::sizeHint() const
{
    return QSize(640, 520);
}

double DiagramCanvas::widgetScale() const
{
    return 0.44 * std::min(width(), height()) / kHalfExtent;
}

QPointF DiagramCanvas::toWidget(double x, double y) const
{
    const double scale = widgetScale();

    return QPointF(
        0.5 * width() + x * scale,
        0.5 * height() - y * scale);
}

KnotMath::Vec3 DiagramCanvas::toDiagram(const QPointF &pos) const
{
    const double scale = widgetScale();

    return KnotMath::Vec3(
        (pos.x() - 0.5 * width()) / scale,
        (0.5 * height() - pos.y()) / scale,
        0.0);
}

void DiagramCanvas::setDiagram(
    const KnotMath::KnotDiagram &diagram,
    bool remember)
{
    if (remember && diagram_.isValid())
    {
        history_.push_back(diagram_);

        if (history_.size() > kHistory)
            history_.removeFirst();
    }

    diagram_ = diagram;

    moves_ = KnotMath::findReidemeisterMoves(diagram_);

    tightSpots_ =
        tubeRadius_ > 0.0 ? KnotMath::findTightSpots(diagram_, tubeRadius_) : QVector<KnotMath::TightSpot>();

    hover_ = -1;
    hoverMove_ = -1;
    picked_ = -1;

    setCursor(reviewing_ ? Qt::ArrowCursor : Qt::CrossCursor);

    update();
}

void DiagramCanvas::setTubeRadius(double radius)
{
    tubeRadius_ = radius;

    setDiagram(diagram_, false);
}

const QVector<KnotMath::TightSpot> &DiagramCanvas::tightSpots() const
{
    return tightSpots_;
}

void DiagramCanvas::setReviewing(bool reviewing)
{
    reviewing_ = reviewing;
    picked_ = -1;
    hover_ = -1;
    hoverMove_ = -1;

    setCursor(reviewing_ ? Qt::ArrowCursor : Qt::CrossCursor);

    update();
}

int DiagramCanvas::pickedCrossing() const
{
    return picked_;
}

bool DiagramCanvas::spreadApart()
{
    KnotMath::KnotDiagram d = diagram_;

    if (!KnotMath::spreadDiagram(d, tubeRadius_))
        return false;

    setDiagram(d, true);

    emit diagramChanged();

    return true;
}

void DiagramCanvas::flipPicked()
{
    if (picked_ < 0 || picked_ >= diagram_.crossings.size())
        return;

    const int picked = picked_;

    KnotMath::KnotDiagram d = diagram_;

    d.crossings[picked].aOver = !d.crossings.at(picked).aOver;

    setDiagram(d, true);

    picked_ = picked;

    emit diagramChanged();
}

int DiagramCanvas::removePickedLoop()
{
    if (picked_ < 0 || picked_ >= diagram_.crossings.size())
        return 0;

    KnotMath::KnotDiagram d = diagram_;

    if (!KnotMath::removeDiagramLoop(d, picked_))
        return 0;

    const int gone = diagram_.crossings.size() - d.crossings.size();

    setDiagram(d, true);

    emit diagramChanged();

    return gone == 1 ? 1 : 2;
}

int DiagramCanvas::tightSpotAt(const QPointF &pos) const
{
    for (int i = 0; i < tightSpots_.size(); ++i)
    {
        const KnotMath::Vec3 &p = tightSpots_.at(i).position;
        const QPointF centre = toWidget(p.x, p.y);

        if (std::hypot(pos.x() - centre.x(), pos.y() - centre.y()) <= kTightRingPixels)
            return i;
    }

    return -1;
}

void DiagramCanvas::pickAt(const QPointF &pos)
{
    const KnotMath::Vec3 p = toDiagram(pos);

    int crossing = KnotMath::nearestDiagramCrossing(diagram_, p.x, p.y, 16.0 / widgetScale());

    if (crossing < 0)
    {
        const int spot = tightSpotAt(pos);

        if (spot >= 0)
            crossing = tightSpots_.at(spot).crossing;
    }

    picked_ = crossing;

    update();

    emit pickChanged();
}

void DiagramCanvas::clearDrawing()
{
    animation_->stop();

    setDiagram(KnotMath::KnotDiagram(), true);

    stroke_.clear();
    drawing_ = false;
    tooShort_ = false;

    emit diagramChanged();
}

void DiagramCanvas::alternate()
{
    if (!diagram_.isValid() || isAnimating())
        return;

    KnotMath::KnotDiagram d = diagram_;

    KnotMath::alternateDiagram(d);

    setDiagram(d, true);

    emit diagramChanged();
}

void DiagramCanvas::mirror()
{
    if (!diagram_.isValid() || isAnimating())
        return;

    KnotMath::KnotDiagram d = diagram_;

    KnotMath::mirrorDiagram(d);

    setDiagram(d, true);

    emit diagramChanged();
}

void DiagramCanvas::undo()
{
    if (!canUndo())
        return;

    const KnotMath::KnotDiagram previous = history_.takeLast();

    setDiagram(previous, false);

    tooShort_ = false;

    emit diagramChanged();
}

void DiagramCanvas::startDrawing(const QPointF &pos)
{
    beforeDrawing_ = diagram_;

    diagram_ = KnotMath::KnotDiagram();
    moves_.clear();

    stroke_.clear();
    stroke_.push_back(pos);
    drawing_ = true;
    tooShort_ = false;
    hover_ = -1;
    hoverMove_ = -1;

    setCursor(Qt::CrossCursor);

    update();

    emit diagramChanged();
}

void DiagramCanvas::startMove(int index)
{
    if (index < 0 || index >= moves_.size())
        return;

    // A last check that the move keeps the knot (the moves are built to,
    // but the Alexander polynomial costs only a moment).
    if (!KnotMath::reidemeisterMoveKeepsKnot(diagram_, moves_.at(index)))
    {
        emit moveRefused();
        return;
    }

    activeMove_ = moves_.at(index);
    frame_ = KnotMath::reidemeisterFrame(activeMove_, 0.0);

    // remembered for Undo; the moves of the old picture are gone
    history_.push_back(diagram_);

    if (history_.size() > kHistory)
        history_.removeFirst();

    moves_.clear();
    hover_ = -1;
    hoverMove_ = -1;

    setCursor(Qt::ArrowCursor);

    animation_->setDuration(kMoveDuration[std::clamp(activeMove_.type, 1, 3)]);
    animation_->start();

    emit hoverChanged();
}

void DiagramCanvas::updateHover(const QPointF &pos)
{
    if (!diagram_.isValid())
        return;

    if (reviewing_)
    {
        const KnotMath::Vec3 q = toDiagram(pos);

        const bool onSomething =
            KnotMath::nearestDiagramCrossing(diagram_, q.x, q.y, 16.0 / widgetScale()) >= 0 ||
            tightSpotAt(pos) >= 0;

        setCursor(onSomething ? Qt::PointingHandCursor : Qt::ArrowCursor);

        return;
    }

    const KnotMath::Vec3 p = toDiagram(pos);

    const int k = KnotMath::nearestDiagramCrossing(
        diagram_, p.x, p.y, 16.0 / widgetScale());

    // a crossing's circle comes before the region around it
    const int m = (k >= 0) ? -1 : KnotMath::reidemeisterMoveAt(moves_, p.x, p.y);

    if (k == hover_ && m == hoverMove_)
        return;

    const bool moveChanged = (m != hoverMove_);

    hover_ = k;
    hoverMove_ = m;

    setCursor((k >= 0 || m >= 0) ? Qt::PointingHandCursor : Qt::CrossCursor);

    update();

    if (moveChanged)
        emit hoverChanged();
}

void DiagramCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || isAnimating())
        return;

    const QPointF pos = event->position();

    if (reviewing_)
    {
        pickAt(pos);
        return;
    }

    if (diagram_.isValid())
    {
        const KnotMath::Vec3 p = toDiagram(pos);

        const int k = KnotMath::nearestDiagramCrossing(
            diagram_, p.x, p.y, 16.0 / widgetScale());

        if (k >= 0)
        {
            KnotMath::KnotDiagram d = diagram_;

            d.crossings[k].aOver = !d.crossings[k].aOver;

            setDiagram(d, true);

            // the mouse is still over the crossing
            updateHover(pos);

            emit diagramChanged();

            return;
        }

        // on a move's region: wait - a click makes the move, a drag draws
        const int m = KnotMath::reidemeisterMoveAt(moves_, p.x, p.y);

        if (m >= 0)
        {
            pressedMove_ = m;
            pressPos_ = pos;

            return;
        }
    }

    // anything else starts a new drawing
    startDrawing(pos);
}

void DiagramCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();

    if (pressedMove_ >= 0)
    {
        if (std::hypot(pos.x() - pressPos_.x(), pos.y() - pressPos_.y()) <= kClickPixels)
            return;

        // it is a drag after all: a new drawing from where it started
        pressedMove_ = -1;

        startDrawing(pressPos_);
    }

    if (drawing_)
    {
        if (stroke_.isEmpty() ||
            std::hypot(pos.x() - stroke_.last().x(),
                       pos.y() - stroke_.last().y()) > 2.0)
        {
            stroke_.push_back(pos);

            update();
        }

        return;
    }

    if (!isAnimating())
        updateHover(pos);
}

void DiagramCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (pressedMove_ >= 0)
    {
        const int m = pressedMove_;

        pressedMove_ = -1;

        startMove(m);

        return;
    }

    if (!drawing_)
        return;

    drawing_ = false;

    QVector<KnotMath::Vec3> points;

    points.reserve(stroke_.size());

    for (const QPointF &p : stroke_)
        points.push_back(toDiagram(p));

    const KnotMath::KnotDiagram drawn =
        KnotMath::makeDiagramFromStroke(points, 160, kHalfExtent);

    stroke_.clear();

    // the picture before the drawing can be brought back with Undo
    diagram_ = beforeDrawing_;

    setDiagram(drawn, true);

    tooShort_ = !drawn.isValid();

    emit diagramChanged();
}

void DiagramCanvas::drawDiagram(
    QPainter &painter,
    const KnotMath::KnotDiagram &diagram) const
{
    const int n = diagram.points.size();

    if (n < 3)
        return;

    auto widgetPoint = [&](int i)
    {
        const KnotMath::Vec3 &p = diagram.points[i % n];

        return toWidget(p.x, p.y);
    };

    // main curve
    QPainterPath curve;

    curve.moveTo(widgetPoint(0));

    for (int i = 1; i < n; ++i)
        curve.lineTo(widgetPoint(i));

    curve.closeSubpath();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kLine(), kLineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(curve);

    if (diagram.crossings.isEmpty())
        return;

    // Arc length along the curve, so the gaps are the same size wherever
    // the points lie closer together (during a move they do).
    QVector<double> arc(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const KnotMath::Vec3 &a = diagram.points[i];
        const KnotMath::Vec3 &b = diagram.points[(i + 1) % n];

        arc[i + 1] = arc[i] + std::hypot(b.x - a.x, b.y - a.y);
    }

    const double total = arc[n];

    if (total <= 1e-12)
        return;

    auto lengthAt = [&](int segment, double t)
    {
        return arc[segment] + t * (arc[segment + 1] - arc[segment]);
    };

    auto pointAtLength = [&](double length)
    {
        double w = std::fmod(length, total);

        if (w < 0.0)
            w += total;

        int i = static_cast<int>(std::upper_bound(arc.begin(), arc.end(), w) - arc.begin()) - 1;

        i = std::clamp(i, 0, n - 1);

        const double span = arc[i + 1] - arc[i];
        const double f = span > 1e-15 ? (w - arc[i]) / span : 0.0;

        const KnotMath::Vec3 &a = diagram.points[i];
        const KnotMath::Vec3 &b = diagram.points[(i + 1) % n];

        return toWidget(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
    };

    // the part of the curve around arc length `centre`
    auto strand = [&](QPainterPath &path, double centre, double half)
    {
        constexpr int steps = 16;

        path.moveTo(pointAtLength(centre - half));

        for (int i = 1; i <= steps; ++i)
            path.lineTo(pointAtLength(centre - half + 2.0 * half * i / steps));
    };

    const double gap = kGapPixels / widgetScale();

    // gaps in the under strands
    for (const KnotMath::DiagramCrossing &c : diagram.crossings)
    {
        QPainterPath path;

        strand(path, c.aOver ? lengthAt(c.segB, c.tB) : lengthAt(c.segA, c.tA), gap);

        painter.setPen(QPen(kBackground(), kLineWidth + 9.0, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        painter.drawPath(path);
    }

    // the over strands again, on top of the gaps
    for (const KnotMath::DiagramCrossing &c : diagram.crossings)
    {
        QPainterPath path;

        strand(path, c.aOver ? lengthAt(c.segA, c.tA) : lengthAt(c.segB, c.tB), gap * 1.8);

        painter.setPen(QPen(kLine(), kLineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
}

// The regions of the moves, lightly shaded (the one under the mouse more).
void DiagramCanvas::drawMoves(QPainter &painter) const
{
    for (int i = 0; i < moves_.size(); ++i)
    {
        const KnotMath::ReidemeisterMove &move = moves_.at(i);

        QPainterPath region;

        for (int j = 0; j < move.region.size(); ++j)
        {
            const QPointF p = toWidget(move.region[j].x, move.region[j].y);

            if (j == 0)
                region.moveTo(p);
            else
                region.lineTo(p);
        }

        region.closeSubpath();

        QColor fill = kAccent();

        fill.setAlpha(i == hoverMove_ ? 80 : 28);

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPath(region);
    }
}

// The places too tight for the tube: a numbered ring around each.
void DiagramCanvas::drawTightSpots(QPainter &painter) const
{
    QFont font = painter.font();

    font.setPointSize(9);
    font.setBold(true);

    painter.setFont(font);

    const QColor warning = kTight;

    for (int i = 0; i < tightSpots_.size(); ++i)
    {
        const KnotMath::Vec3 &p = tightSpots_.at(i).position;

        const QPointF centre = toWidget(p.x, p.y);

        QColor fill = warning;

        fill.setAlpha(40);

        painter.setBrush(fill);
        painter.setPen(QPen(warning, 2.0));
        painter.drawEllipse(centre, kTightRingPixels, kTightRingPixels);

        painter.setBrush(warning);
        painter.setPen(Qt::NoPen);

        const QPointF badge = centre + QPointF(kTightRingPixels * 0.8, -kTightRingPixels * 0.8);

        painter.drawEllipse(badge, 9.0, 9.0);

        painter.setPen(kBackground());
        painter.drawText(
            QRectF(badge.x() - 9.0, badge.y() - 9.0, 18.0, 18.0),
            Qt::AlignCenter,
            QString::number(i + 1));
    }
}

void DiagramCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), kBackground());

    if (drawing_ && stroke_.size() > 1)
    {
        QPainterPath path;

        path.moveTo(stroke_.first());

        for (int i = 1; i < stroke_.size(); ++i)
            path.lineTo(stroke_.at(i));

        painter.setPen(QPen(kLine(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        return;
    }

    if (isAnimating())
    {
        drawDiagram(painter, frame_);

        return;
    }

    if (!diagram_.isValid())
    {
        painter.setPen(kMuted());

        QFont font = painter.font();
        font.setPointSize(11);
        painter.setFont(font);

        painter.drawText(
            rect(),
            Qt::AlignCenter,
            tooShort_
                ? tr("That was too short - draw a bigger loop.")
                : tr("Draw a closed loop with the mouse.\n"
                     "Make it cross itself to tie a knot."));

        return;
    }

    // (in the review the strands are what counts, not the moves)
    if (!reviewing_)
        drawMoves(painter);

    drawDiagram(painter, diagram_);

    drawTightSpots(painter);

    // the crossings of the move under the mouse
    QVector<int> moveCrossings;

    if (hoverMove_ >= 0 && hoverMove_ < moves_.size())
        moveCrossings = moves_.at(hoverMove_).crossings;

    // crossing markers
    for (int k = 0; k < diagram_.crossings.size(); ++k)
    {
        const KnotMath::Vec3 &p = diagram_.crossings.at(k).position;

        const QPointF centre = toWidget(p.x, p.y);

        painter.setBrush(Qt::NoBrush);

        if (k == picked_)
        {
            painter.setPen(QPen(kAccent(), 3.0));
            painter.drawEllipse(centre, 17.0, 17.0);
        }
        else if (k == hover_ || moveCrossings.contains(k))
        {
            painter.setPen(QPen(kAccent(), 2.0));
            painter.drawEllipse(centre, 15.0, 15.0);
        }
        else
        {
            painter.setPen(QPen(kMuted(), 1.0));
            painter.drawEllipse(centre, 13.0, 13.0);
        }
    }

    if (reviewing_)
        return;

    // the names of the moves, in their regions
    QFont font = painter.font();

    font.setPointSize(9);
    font.setBold(true);

    painter.setFont(font);

    for (int i = 0; i < moves_.size(); ++i)
    {
        const KnotMath::ReidemeisterMove &move = moves_.at(i);

        const QPointF centre = toWidget(move.centre.x, move.centre.y);

        painter.setPen(i == hoverMove_ ? kAccent() : kMuted());

        painter.drawText(
            QRectF(centre.x() - 20.0, centre.y() - 10.0, 40.0, 20.0),
            Qt::AlignCenter,
            QStringLiteral("R%1").arg(move.type));
    }
}

/*
 * =========================================================
 * DIALOG
 * =========================================================
 */

DiagramEditorDialog::DiagramEditorDialog(QWidget *parent, double tubeRadius)
    : QDialog(parent),
      tubeRadius_(tubeRadius)
{
    setWindowTitle(tr("Draw a knot"));

    setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(this);

    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    helpLabel_ = new QLabel(this);

    helpLabel_->setObjectName(QStringLiteral("Subtitle"));
    helpLabel_->setWordWrap(true);

    layout->addWidget(helpLabel_);

    canvas_ = new DiagramCanvas(this);

    canvas_->setTubeRadius(tubeRadius_);

    layout->addWidget(canvas_, 1);

    statusLabel_ = new QLabel(this);

    statusLabel_->setObjectName(QStringLiteral("Subtitle"));
    statusLabel_->setWordWrap(true);

    layout->addWidget(statusLabel_);

    moveLabel_ = new QLabel(this);

    moveLabel_->setWordWrap(true);

    layout->addWidget(moveLabel_);

    drawButtons_ = new QWidget(this);

    QHBoxLayout *buttons = new QHBoxLayout(drawButtons_);

    buttons->setContentsMargins(0, 0, 0, 0);

    QPushButton *clearButton = new QPushButton(tr("Clear"), this);

    undoButton_ = new QPushButton(tr("Undo"), this);
    undoButton_->setToolTip(tr("Undo the last change (Ctrl+Z)."));
    undoButton_->setShortcut(QKeySequence::Undo);

    alternateButton_ = new QPushButton(tr("Alternate"), this);
    alternateButton_->setToolTip(
        tr("Over, under, over, under ... along the whole curve."));

    mirrorButton_ = new QPushButton(tr("Mirror"), this);
    mirrorButton_->setToolTip(
        tr("Swap over and under at every crossing."));

    QPushButton *cancelButton = new QPushButton(tr("Cancel"), this);

    okButton_ = new QPushButton(tr("USE THIS KNOT"), this);
    okButton_->setObjectName(QStringLiteral("GenerateButton"));
    okButton_->setDefault(true);

    buttons->addWidget(clearButton);
    buttons->addWidget(undoButton_);
    buttons->addWidget(alternateButton_);
    buttons->addWidget(mirrorButton_);
    buttons->addStretch(1);
    buttons->addWidget(cancelButton);
    buttons->addWidget(okButton_);

    layout->addWidget(drawButtons_);

    /*
     * The review of a drawing too tight for the tube: the strands can be
     * moved apart, a crossing changed, a loop taken out; or the drawing is
     * used as it is, thin.
     */
    reviewButtons_ = new QWidget(this);

    QHBoxLayout *review = new QHBoxLayout(reviewButtons_);

    review->setContentsMargins(0, 0, 0, 0);

    QPushButton *spreadButton = new QPushButton(tr("Spread apart"), this);

    spreadButton->setObjectName(QStringLiteral("GenerateButton"));
    spreadButton->setToolTip(
        tr("Moves the strands away from each other. The crossings stay, so "
           "it is the same knot."));

    flipButton_ = new QPushButton(tr("Flip crossing"), this);
    flipButton_->setToolTip(tr("Swap which strand goes over at the chosen crossing."));

    removeButton_ = new QPushButton(tr("Remove loop"), this);
    removeButton_->setToolTip(
        tr("Takes out the smaller loop at the chosen crossing. If other "
           "strands cross that loop, the knot can change."));

    reviewUndoButton_ = new QPushButton(tr("Undo"), this);

    QPushButton *backButton = new QPushButton(tr("Back to drawing"), this);
    reviewUseButton_ = new QPushButton(this);

    reviewUseButton_->setObjectName(QStringLiteral("GenerateButton"));

    review->addWidget(spreadButton);
    review->addWidget(flipButton_);
    review->addWidget(removeButton_);
    review->addWidget(reviewUndoButton_);
    review->addStretch(1);
    review->addWidget(backButton);
    review->addWidget(reviewUseButton_);

    reviewButtons_->hide();

    layout->addWidget(reviewButtons_);

    connect(spreadButton, &QPushButton::clicked,
            this, [this]()
            {
                QApplication::setOverrideCursor(Qt::WaitCursor);

                const bool done = canvas_->spreadApart();

                QApplication::restoreOverrideCursor();

                reviewNote_ =
                    !done
                        ? tr("There is no more room to move these strands: "
                             "remove a loop or go back and draw it larger.")
                    : canvas_->tightSpots().isEmpty()
                        ? tr("The strands are apart: it is the same knot.")
                        : tr("Moved as far as the picture has room: it is the "
                             "same knot. Remove a loop for more room.");

                updateStatus();
            });

    connect(flipButton_, &QPushButton::clicked,
            this, [this]()
            {
                canvas_->flipPicked();

                reviewNote_ = tr("The crossing is flipped: this can change the knot.");

                updateStatus();
            });

    connect(removeButton_, &QPushButton::clicked,
            this, [this]()
            {
                const int result = canvas_->removePickedLoop();

                reviewNote_ =
                    result == 1
                        ? tr("The loop is gone: it was only a twist, so it is the same knot.")
                    : result == 2
                        ? tr("The loop is gone, with the crossings on it: this "
                             "can be another knot now.")
                        : tr("That loop cannot be taken out without changing "
                             "the rest of the picture.");

                updateStatus();
            });

    connect(reviewUndoButton_, &QPushButton::clicked,
            canvas_, &DiagramCanvas::undo);

    connect(backButton, &QPushButton::clicked,
            this, [this]() { setReviewing(false); });

    connect(reviewUseButton_, &QPushButton::clicked,
            this, &QDialog::accept);

    connect(canvas_, &DiagramCanvas::pickChanged,
            this, [this]()
            {
                reviewNote_.clear();

                updateStatus();
            });

    connect(clearButton, &QPushButton::clicked,
            canvas_, &DiagramCanvas::clearDrawing);

    connect(undoButton_, &QPushButton::clicked,
            canvas_, &DiagramCanvas::undo);

    connect(alternateButton_, &QPushButton::clicked,
            canvas_, &DiagramCanvas::alternate);

    connect(mirrorButton_, &QPushButton::clicked,
            canvas_, &DiagramCanvas::mirror);

    connect(cancelButton, &QPushButton::clicked,
            this, &QDialog::reject);

    connect(okButton_, &QPushButton::clicked,
            this, &DiagramEditorDialog::useKnot);

    // any change of the picture ends the note about the last move (a
    // finished move sets its own note right after)
    connect(canvas_, &DiagramCanvas::diagramChanged,
            this, [this]()
            {
                lastMove_.clear();
                reviewNote_.clear();

                updateStatus();
            });

    connect(canvas_, &DiagramCanvas::hoverChanged,
            this, [this]()
            {
                // pointing at another move: its description replaces the
                // note about the last one
                if (canvas_->hoveredMoveType() != 0)
                    lastMove_.clear();

                updateStatus();
            });

    connect(canvas_, &DiagramCanvas::moveFinished,
            this, [this](int type)
            {
                if (type == 1)
                {
                    lastMove_ = tr("Reidemeister move I: the twist is gone - "
                                   "the same knot with one crossing fewer.");
                }
                else if (type == 2)
                {
                    lastMove_ = tr("Reidemeister move II: the strands are apart - "
                                   "the same knot with two crossings fewer.");
                }
                else
                {
                    lastMove_ = tr("Reidemeister move III: the strand slid across "
                                   "the crossing - the same knot, drawn differently.");
                }

                updateStatus();
            });

    connect(canvas_, &DiagramCanvas::moveRefused,
            this, [this]()
            {
                lastMove_ = tr("That move would change the knot here, so it was not made.");

                updateStatus();
            });

    resize(760, 720);

    setReviewing(false);
}

const KnotMath::KnotDiagram &DiagramEditorDialog::diagram() const
{
    return canvas_->diagram();
}

void DiagramEditorDialog::setReviewing(bool reviewing)
{
    reviewing_ = reviewing;
    reviewNote_.clear();

    canvas_->setReviewing(reviewing);

    drawButtons_->setVisible(!reviewing);
    reviewButtons_->setVisible(reviewing);

    setWindowTitle(reviewing ? tr("Check your knot") : tr("Draw a knot"));

    helpLabel_->setText(
        reviewing
            ? tr("Some strands lie too close together, and one tight place "
                 "makes the whole tube thin. Click a ring or a crossing to "
                 "choose it, then change it or take its loop out.")
            : tr("Draw one closed loop with the left mouse button. "
                 "Crossings are found automatically; click a crossing "
                 "(circle) to swap which strand goes over. The shaded regions "
                 "R1, R2 and R3 are Reidemeister moves: click one to watch it."));

    updateStatus();
}

void DiagramEditorDialog::useKnot()
{
    // too tight for the tube: a look at it first
    if (!canvas_->tightSpots().isEmpty())
    {
        setReviewing(true);
        return;
    }

    accept();
}

void DiagramEditorDialog::updateReviewStatus()
{
    const KnotMath::KnotDiagram &d = canvas_->diagram();

    const int places = canvas_->tightSpots().size();
    const double thickness = KnotMath::diagramTubeRadius(d, tubeRadius_);

    // (the thickness measured, not the one asked for)
    if (places == 0)
    {
        statusLabel_->setText(
            tr("All clear: the tube fits at full thickness (%1).")
                .arg(thickness, 0, 'f', 2));
    }
    else
    {
        statusLabel_->setText(
            tr("Too tight in %1 place(s): the tube would be %2 thick instead of %3.")
                .arg(places)
                .arg(thickness, 0, 'f', 2)
                .arg(tubeRadius_, 0, 'f', 2));
    }

    // (still too tight, it is used only if that is what is wanted)
    reviewUseButton_->setText(places == 0 ? tr("USE THIS KNOT") : tr("USE IT ANYWAY"));

    const bool picked = canvas_->pickedCrossing() >= 0;

    flipButton_->setEnabled(picked);
    removeButton_->setEnabled(picked);
    reviewUndoButton_->setEnabled(canvas_->canUndo());

    QString line = reviewNote_;

    if (line.isEmpty())
    {
        line = picked
                   ? tr("Crossing chosen: flip it, or take out its loop.")
                   : tr("Nothing chosen. Spread apart needs no choice.");
    }

    moveLabel_->setText(line);
    moveLabel_->setVisible(true);
}

void DiagramEditorDialog::updateStatus()
{
    if (reviewing_)
    {
        updateReviewStatus();
        return;
    }

    const KnotMath::KnotDiagram &d = canvas_->diagram();

    const bool valid = d.isValid();
    const bool busy = canvas_->isAnimating();

    okButton_->setEnabled(valid && !busy);
    undoButton_->setEnabled(canvas_->canUndo());
    alternateButton_->setEnabled(valid && !busy && !d.crossings.isEmpty());
    mirrorButton_->setEnabled(valid && !busy && !d.crossings.isEmpty());

    if (busy)
    {
        // the canvas shows the move; the text below says what it is
    }
    else if (!valid)
    {
        statusLabel_->setText(tr("No knot drawn yet."));
    }
    else if (d.crossings.isEmpty())
    {
        statusLabel_->setText(
            tr("0 crossings: this loop is the unknot."));
    }
    else
    {
        // how many moves of each kind there are
        int count[4] = {0, 0, 0, 0};

        for (const KnotMath::ReidemeisterMove &move : canvas_->moves())
            ++count[std::clamp(move.type, 1, 3)];

        QStringList kinds;

        for (int type = 1; type <= 3; ++type)
        {
            if (count[type] == 1)
                kinds << QStringLiteral("R%1").arg(type);
            else if (count[type] > 1)
                kinds << QStringLiteral("R%1 ×%2").arg(type).arg(count[type]);
        }

        QString text = tr("%1 crossings. Click a crossing to flip it.")
                           .arg(d.crossings.size());

        if (!kinds.isEmpty())
        {
            text += QLatin1Char(' ') +
                    tr("Reidemeister moves here: %1.").arg(kinds.join(QStringLiteral(", ")));
        }

        statusLabel_->setText(text);
    }

    // second line: the move being made or under the mouse, or what the
    // last one did
    const int shown = busy ? canvas_->activeMoveType() : canvas_->hoveredMoveType();

    QString line;

    if (shown == 1)
        line = tr("R1 - untwist this loop: one crossing fewer.");
    else if (shown == 2)
        line = tr("R2 - pull this strand off the other one: two crossings fewer.");
    else if (shown == 3)
        line = tr("R3 - slide this strand across the crossing: the same number of crossings.");

    if (shown != 0)
    {
        if (!busy)
            line += QLatin1Char(' ') + tr("Click to watch.");
    }
    else if (!lastMove_.isEmpty())
    {
        line = lastMove_;
    }
    else if (valid && !busy && !canvas_->moves().isEmpty())
    {
        line = tr("Point at a shaded region to see which move it is.");
    }

    // what comes first: a drawing too tight for the tube says so
    if (valid && !busy && !canvas_->tightSpots().isEmpty() && shown == 0)
        line = tr("Ringed: strands too close for the tube. You can fix them on the next screen.");

    moveLabel_->setText(line);
    moveLabel_->setVisible(!line.isEmpty());
}
