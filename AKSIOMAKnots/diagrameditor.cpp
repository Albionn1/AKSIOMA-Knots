#include "diagrameditor.h"

#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
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
}

const KnotMath::KnotDiagram &DiagramCanvas::diagram() const
{
    return diagram_;
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

void DiagramCanvas::clearDrawing()
{
    diagram_ = KnotMath::KnotDiagram();
    stroke_.clear();
    drawing_ = false;
    tooShort_ = false;
    hover_ = -1;

    update();

    emit diagramChanged();
}

void DiagramCanvas::alternate()
{
    if (!diagram_.isValid())
        return;

    KnotMath::alternateDiagram(diagram_);

    update();

    emit diagramChanged();
}

void DiagramCanvas::mirror()
{
    if (!diagram_.isValid())
        return;

    KnotMath::mirrorDiagram(diagram_);

    update();

    emit diagramChanged();
}

void DiagramCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const QPointF pos = event->position();

    if (diagram_.isValid())
    {
        const double scale = widgetScale();

        const double x = (pos.x() - 0.5 * width()) / scale;
        const double y = (0.5 * height() - pos.y()) / scale;

        const int k = KnotMath::nearestDiagramCrossing(
            diagram_, x, y, 16.0 / scale);

        if (k >= 0)
        {
            diagram_.crossings[k].aOver = !diagram_.crossings[k].aOver;

            update();

            emit diagramChanged();

            return;
        }
    }

    // anything else starts a new drawing
    diagram_ = KnotMath::KnotDiagram();
    stroke_.clear();
    stroke_.push_back(pos);
    drawing_ = true;
    tooShort_ = false;
    hover_ = -1;

    update();

    emit diagramChanged();
}

void DiagramCanvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();

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

    if (diagram_.isValid())
    {
        const double scale = widgetScale();

        const double x = (pos.x() - 0.5 * width()) / scale;
        const double y = (0.5 * height() - pos.y()) / scale;

        const int k = KnotMath::nearestDiagramCrossing(
            diagram_, x, y, 16.0 / scale);

        if (k != hover_)
        {
            hover_ = k;

            setCursor(k >= 0 ? Qt::PointingHandCursor : Qt::CrossCursor);

            update();
        }
    }
}

void DiagramCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !drawing_)
        return;

    drawing_ = false;

    const double scale = widgetScale();

    QVector<KnotMath::Vec3> points;

    points.reserve(stroke_.size());

    for (const QPointF &p : stroke_)
    {
        points.push_back(KnotMath::Vec3(
            (p.x() - 0.5 * width()) / scale,
            (0.5 * height() - p.y()) / scale,
            0.0));
    }

    diagram_ = KnotMath::makeDiagramFromStroke(points, 160, kHalfExtent);

    tooShort_ = !diagram_.isValid();

    stroke_.clear();

    update();

    emit diagramChanged();
}

// Appends the part of the polyline around parameter s (in samples) to path.
void DiagramCanvas::strandRange(
    QPainterPath &path,
    double s,
    double halfSamples) const
{
    const int n = diagram_.points.size();

    if (n < 3)
        return;

    auto pointAt = [&](double u)
    {
        double w = std::fmod(u, static_cast<double>(n));

        if (w < 0.0)
            w += n;

        const int i = static_cast<int>(std::floor(w)) % n;
        const double f = w - std::floor(w);

        const KnotMath::Vec3 &a = diagram_.points[i];
        const KnotMath::Vec3 &b = diagram_.points[(i + 1) % n];

        return toWidget(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
    };

    const int steps = std::max(4, static_cast<int>(std::ceil(halfSamples * 2.0)) * 2);

    path.moveTo(pointAt(s - halfSamples));

    for (int i = 1; i <= steps; ++i)
    {
        const double u = s - halfSamples +
                         2.0 * halfSamples * static_cast<double>(i) / steps;

        path.lineTo(pointAt(u));
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

    const int n = diagram_.points.size();

    // main curve
    QPainterPath curve;

    curve.moveTo(toWidget(diagram_.points[0].x, diagram_.points[0].y));

    for (int i = 1; i < n; ++i)
        curve.lineTo(toWidget(diagram_.points[i].x, diagram_.points[i].y));

    curve.closeSubpath();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kLine(), kLineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(curve);

    // pixel length of one sample
    double perimeter = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const QPointF a = toWidget(diagram_.points[i].x, diagram_.points[i].y);
        const QPointF b = toWidget(diagram_.points[(i + 1) % n].x,
                                   diagram_.points[(i + 1) % n].y);

        perimeter += std::hypot(a.x() - b.x(), a.y() - b.y());
    }

    const double pxPerSample = std::max(0.5, perimeter / n);
    const double gapSamples = std::max(1.5, kGapPixels / pxPerSample);

    // gaps in the under strands
    for (const KnotMath::DiagramCrossing &c : diagram_.crossings)
    {
        const double sUnder = c.aOver ? c.segB + c.tB : c.segA + c.tA;

        QPainterPath gap;

        strandRange(gap, sUnder, gapSamples);

        painter.setPen(QPen(kBackground(), kLineWidth + 9.0, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
        painter.drawPath(gap);
    }

    // the over strands again, on top of the gaps
    for (const KnotMath::DiagramCrossing &c : diagram_.crossings)
    {
        const double sOver = c.aOver ? c.segA + c.tA : c.segB + c.tB;

        QPainterPath over;

        strandRange(over, sOver, gapSamples * 1.8);

        painter.setPen(QPen(kLine(), kLineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(over);
    }

    // crossing markers
    for (int k = 0; k < diagram_.crossings.size(); ++k)
    {
        const KnotMath::Vec3 &p = diagram_.crossings.at(k).position;

        const QPointF centre = toWidget(p.x, p.y);

        painter.setBrush(Qt::NoBrush);

        if (k == hover_)
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
}

/*
 * =========================================================
 * DIALOG
 * =========================================================
 */

DiagramEditorDialog::DiagramEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Draw a knot"));

    setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(this);

    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    QLabel *help = new QLabel(
        tr("Draw one closed loop with the left mouse button. "
           "Crossings are found automatically; click a crossing "
           "(circle) to swap which strand goes over."),
        this);

    help->setObjectName(QStringLiteral("Subtitle"));
    help->setWordWrap(true);

    layout->addWidget(help);

    canvas_ = new DiagramCanvas(this);

    layout->addWidget(canvas_, 1);

    statusLabel_ = new QLabel(this);

    statusLabel_->setObjectName(QStringLiteral("Subtitle"));

    layout->addWidget(statusLabel_);

    QHBoxLayout *buttons = new QHBoxLayout();

    QPushButton *clearButton = new QPushButton(tr("Clear"), this);

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
    buttons->addWidget(alternateButton_);
    buttons->addWidget(mirrorButton_);
    buttons->addStretch(1);
    buttons->addWidget(cancelButton);
    buttons->addWidget(okButton_);

    layout->addLayout(buttons);

    connect(clearButton, &QPushButton::clicked,
            canvas_, &DiagramCanvas::clearDrawing);

    connect(alternateButton_, &QPushButton::clicked,
            canvas_, &DiagramCanvas::alternate);

    connect(mirrorButton_, &QPushButton::clicked,
            canvas_, &DiagramCanvas::mirror);

    connect(cancelButton, &QPushButton::clicked,
            this, &QDialog::reject);

    connect(okButton_, &QPushButton::clicked,
            this, &QDialog::accept);

    connect(canvas_, &DiagramCanvas::diagramChanged,
            this, [this]() { updateStatus(); });

    resize(760, 680);

    updateStatus();
}

const KnotMath::KnotDiagram &DiagramEditorDialog::diagram() const
{
    return canvas_->diagram();
}

void DiagramEditorDialog::updateStatus()
{
    const KnotMath::KnotDiagram &d = canvas_->diagram();

    const bool valid = d.isValid();

    okButton_->setEnabled(valid);
    alternateButton_->setEnabled(valid && !d.crossings.isEmpty());
    mirrorButton_->setEnabled(valid && !d.crossings.isEmpty());

    if (!valid)
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
        statusLabel_->setText(
            tr("%1 crossings. Click a crossing to flip it.")
                .arg(d.crossings.size()));
    }
}
