#include "practicedialog.h"

#include "diagrameditor.h"
#include "knotexport.h"
#include "knottable.h"
#include "theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRandomGenerator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{
// from the current theme
QColor kBackground() { return Theme::colors().window; }
QColor kLine() { return Theme::colors().text; }
QColor kMuted() { return Theme::colors().muted; }
QColor kQuiet() { return Theme::colors().quiet; }
QColor kAccent() { return Theme::colors().accent; }

const QColor kRight(48, 164, 108);
const QColor kWrong(229, 72, 77);

constexpr double kLineWidth = 5.0;
constexpr double kGapPixels = 11.0;
constexpr double kHalfExtent = 2.0;
constexpr double kPickPixels = 14.0;

// More crossings than this make the exercises tedious (and the picture
// too crowded to click).
constexpr int kMaxPracticeCrossings = 24;

QString signText(int sign)
{
    return sign > 0 ? QStringLiteral("+1") : QStringLiteral("−1");
}
}

/*
 * =========================================================
 * CANVAS
 * =========================================================
 */

PracticeCanvas::PracticeCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

QSize PracticeCanvas::minimumSizeHint() const
{
    return QSize(420, 340);
}

QSize PracticeCanvas::sizeHint() const
{
    return QSize(640, 520);
}

void PracticeCanvas::setDiagram(const KnotMath::KnotDiagram &diagram)
{
    diagram_ = diagram;
    arcs_ = KnotMath::diagramArcs(diagram_);

    const int n = diagram_.points.size();

    cum_ = QVector<double>(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
    {
        const KnotMath::Vec3 &a = diagram_.points[i];
        const KnotMath::Vec3 &b = diagram_.points[(i + 1) % n];

        cum_[i + 1] = cum_[i] + std::hypot(b.x - a.x, b.y - a.y);
    }

    colors_ = QVector<int>(arcs_.arcCount, -1);
    signAnswers_ = QVector<int>(diagram_.crossings.size(), 0);
    signsChecked_ = false;
    highlightArc_ = -1;
    hoverArc_ = -1;
    hoverCrossing_ = -1;

    update();
}

const KnotMath::KnotDiagram &PracticeCanvas::diagram() const
{
    return diagram_;
}

const KnotMath::DiagramArcs &PracticeCanvas::arcs() const
{
    return arcs_;
}

void PracticeCanvas::setMode(Mode mode)
{
    mode_ = mode;
    hoverArc_ = -1;
    hoverCrossing_ = -1;
    highlightArc_ = -1;

    update();
}

void PracticeCanvas::setBrush(int color)
{
    brush_ = color;
}

const QVector<int> &PracticeCanvas::colors() const
{
    return colors_;
}

void PracticeCanvas::setColors(const QVector<int> &colors)
{
    colors_ = colors;
    colors_.resize(arcs_.arcCount);

    update();
}

const QVector<int> &PracticeCanvas::signAnswers() const
{
    return signAnswers_;
}

void PracticeCanvas::setSignAnswers(const QVector<int> &answers)
{
    signAnswers_ = answers;
    signAnswers_.resize(diagram_.crossings.size());

    update();
}

void PracticeCanvas::setSignsChecked(bool checked)
{
    signsChecked_ = checked;

    update();
}

void PracticeCanvas::setHighlightArc(int arc)
{
    highlightArc_ = arc;

    update();
}

QColor PracticeCanvas::tricolor(int color)
{
    switch (color)
    {
    case 0:
        return QColor(229, 72, 77);
    case 1:
        return QColor(48, 164, 108);
    case 2:
        return QColor(62, 142, 247);
    default:
        return kLine();
    }
}

double PracticeCanvas::widgetScale() const
{
    return 0.44 * std::min(width(), height()) / kHalfExtent;
}

QPointF PracticeCanvas::toWidget(double x, double y) const
{
    const double scale = widgetScale();

    return QPointF(
        0.5 * width() + x * scale,
        0.5 * height() - y * scale);
}

QPointF PracticeCanvas::toDiagram(const QPointF &p) const
{
    const double scale = widgetScale();

    return QPointF(
        (p.x() - 0.5 * width()) / scale,
        (0.5 * height() - p.y()) / scale);
}

double PracticeCanvas::lengthAt(double s) const
{
    const int n = diagram_.points.size();
    const double total = cum_.last();

    const double wraps = std::floor(s / n);

    s -= wraps * n;

    const int i = std::clamp(static_cast<int>(std::floor(s)), 0, n - 1);
    const double f = s - i;

    return cum_[i] + f * (cum_[i + 1] - cum_[i]) + wraps * total;
}

double PracticeCanvas::sAtLength(double length) const
{
    const int n = diagram_.points.size();
    const double total = cum_.last();

    const double wraps = std::floor(length / total);

    length -= wraps * total;

    const int i = std::clamp(
        static_cast<int>(std::upper_bound(cum_.begin(), cum_.end(), length) -
                         cum_.begin()) - 1,
        0, n - 1);

    const double segment = cum_[i + 1] - cum_[i];
    const double f = segment > 1e-15 ? (length - cum_[i]) / segment : 0.0;

    return i + f + wraps * n;
}

QPointF PracticeCanvas::pointAt(double s) const
{
    const int n = diagram_.points.size();

    double w = std::fmod(s, static_cast<double>(n));

    if (w < 0.0)
        w += n;

    const int i = static_cast<int>(std::floor(w)) % n;
    const double f = w - std::floor(w);

    const KnotMath::Vec3 &a = diagram_.points[i];
    const KnotMath::Vec3 &b = diagram_.points[(i + 1) % n];

    return toWidget(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
}

QPointF PracticeCanvas::tangentAt(double s) const
{
    const QPointF d = pointAt(s + 0.5) - pointAt(s - 0.5);
    const double length = std::hypot(d.x(), d.y());

    return length > 1e-9 ? d / length : QPointF(1.0, 0.0);
}

double PracticeCanvas::gapLength() const
{
    return kGapPixels / widgetScale();
}

QPainterPath PracticeCanvas::arcPath(int arc) const
{
    const double scale = widgetScale();

    // diagram -> widget, as toWidget()
    const QTransform transform(scale, 0.0, 0.0, -scale, 0.5 * width(), 0.5 * height());

    return KnotExport::diagramArcPath(diagram_, arcs_, arc, gapLength(), transform);
}

int PracticeCanvas::arcAtWidget(const QPointF &point) const
{
    const int n = diagram_.points.size();

    if (n < 3)
        return -1;

    const double total = cum_.last();
    const double gap = gapLength();

    double best = kPickPixels;
    int bestArc = -1;

    for (int i = 0; i < n; ++i)
    {
        const QPointF a = pointAt(i);
        const QPointF b = pointAt(i + 1);
        const QPointF ab = b - a;

        const double len2 = QPointF::dotProduct(ab, ab);

        const double t = len2 > 1e-12
                             ? std::clamp(QPointF::dotProduct(point - a, ab) / len2, 0.0, 1.0)
                             : 0.0;

        const QPointF q = a + ab * t;
        const double d = std::hypot(point.x() - q.x(), point.y() - q.y());

        if (d >= best)
            continue;

        const double s = i + t;

        // inside a gap the strand is not drawn (the click is meant for the
        // over strand)
        bool inGap = false;

        for (double u : arcs_.underS)
        {
            double dl = std::fmod(std::abs(lengthAt(s) - lengthAt(u)), total);

            dl = std::min(dl, total - dl);

            if (dl < gap)
            {
                inGap = true;
                break;
            }
        }

        if (inGap)
            continue;

        best = d;
        bestArc = KnotMath::arcAt(arcs_, s);
    }

    return bestArc;
}

int PracticeCanvas::crossingAtWidget(const QPointF &point) const
{
    const QPointF p = toDiagram(point);

    return KnotMath::nearestDiagramCrossing(
        diagram_, p.x(), p.y(), 18.0 / widgetScale());
}

void PracticeCanvas::mousePressEvent(QMouseEvent *event)
{
    if (!diagram_.isValid())
        return;

    const bool right = event->button() == Qt::RightButton;

    if (event->button() != Qt::LeftButton && !right)
        return;

    if (mode_ == Mode::Coloring)
    {
        const int arc = arcAtWidget(event->position());

        if (arc < 0)
            return;

        colors_[arc] = right ? -1 : brush_;
        highlightArc_ = -1;
    }
    else
    {
        const int k = crossingAtWidget(event->position());

        if (k < 0)
            return;

        // left: ? -> +1 -> -1 -> ?, right: the other way
        int &answer = signAnswers_[k];

        if (!right)
            answer = answer == 0 ? 1 : (answer == 1 ? -1 : 0);
        else
            answer = answer == 0 ? -1 : (answer == -1 ? 1 : 0);

        signsChecked_ = false;
    }

    update();

    emit changed();
}

void PracticeCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!diagram_.isValid())
        return;

    int arc = -1;
    int crossing = -1;

    if (mode_ == Mode::Coloring)
        arc = arcAtWidget(event->position());
    else
        crossing = crossingAtWidget(event->position());

    if (arc != hoverArc_ || crossing != hoverCrossing_)
    {
        hoverArc_ = arc;
        hoverCrossing_ = crossing;

        setCursor(arc >= 0 || crossing >= 0 ? Qt::PointingHandCursor
                                            : Qt::ArrowCursor);

        update();
    }
}

void PracticeCanvas::drawArrow(QPainter &painter, double s, const QColor &color) const
{
    const QPointF tip = pointAt(s);
    const QPointF t = tangentAt(s);
    const QPointF normal(-t.y(), t.x());

    constexpr double length = 13.0;
    constexpr double width = 7.0;

    QPainterPath head;

    head.moveTo(tip + t * (0.5 * length));
    head.lineTo(tip - t * (0.5 * length) + normal * width);
    head.lineTo(tip - t * (0.5 * length) - normal * width);
    head.closeSubpath();

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(head);
}

// Two small crossings in the corner: what +1 and -1 look like.
void PracticeCanvas::drawSignLegend(QPainter &painter) const
{
    const QRectF box(12.0, 12.0, 170.0, 92.0);

    painter.setPen(QPen(kQuiet(), 1.0));
    painter.setBrush(kBackground());
    painter.drawRect(box);

    auto arrowHead = [&painter](const QPointF &from, const QPointF &to, const QColor &color)
    {
        const QPointF d = to - from;
        const double length = std::hypot(d.x(), d.y());
        const QPointF t = d / length;
        const QPointF normal(-t.y(), t.x());

        QPainterPath head;

        head.moveTo(to);
        head.lineTo(to - t * 8.0 + normal * 4.5);
        head.lineTo(to - t * 8.0 - normal * 4.5);
        head.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(head);
    };

    for (int i = 0; i < 2; ++i)
    {
        const bool positive = i == 0;

        const QPointF centre(box.left() + 45.0 + i * 80.0, box.top() + 38.0);
        constexpr double r = 20.0;

        // over strand bottom-left -> top-right for +1, bottom-right ->
        // top-left for -1; the under strand the other diagonal, upwards
        const QPointF overFrom = centre + QPointF(positive ? -r : r, r);
        const QPointF overTo = centre + QPointF(positive ? r : -r, -r);
        const QPointF underFrom = centre + QPointF(positive ? r : -r, r);
        const QPointF underTo = centre + QPointF(positive ? -r : r, -r);

        const QPointF underDir = (underTo - underFrom) / (2.0 * std::sqrt(2.0) * r);

        painter.setPen(QPen(kLine(), 3.0, Qt::SolidLine, Qt::FlatCap));
        painter.drawLine(underFrom, centre - underDir * 8.0);
        painter.drawLine(centre + underDir * 8.0, underTo);
        painter.drawLine(overFrom, overTo);

        arrowHead(overFrom, overTo, kLine());
        arrowHead(centre + underDir * 8.0, underTo, kLine());

        painter.setPen(positive ? kAccent() : kMuted());

        QFont font = painter.font();
        font.setBold(true);
        painter.setFont(font);

        painter.drawText(
            QRectF(centre.x() - 30.0, box.top() + 66.0, 60.0, 20.0),
            Qt::AlignCenter,
            signText(positive ? 1 : -1));
    }
}

void PracticeCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), kBackground());

    if (!diagram_.isValid())
        return;

    const int c = diagram_.crossings.size();

    // hint glow
    if (mode_ == Mode::Coloring && highlightArc_ >= 0)
    {
        QColor glow = kAccent();
        glow.setAlpha(110);

        painter.setPen(QPen(glow, kLineWidth + 12.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(arcPath(highlightArc_));
    }

    for (int arc = 0; arc < arcs_.arcCount; ++arc)
    {
        QColor color = kLine();

        if (mode_ == Mode::Coloring)
        {
            const int value = colors_.value(arc, -1);

            color = value >= 0 ? tricolor(value) : kLine();
        }

        const double width =
            (mode_ == Mode::Coloring && arc == hoverArc_) ? kLineWidth + 2.5 : kLineWidth;

        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(arcPath(arc));
    }

    if (mode_ == Mode::Coloring)
    {
        // crossings that break the rule (a ring at every crossing would
        // hide the gaps that show which strand is under)
        for (int k = 0; k < c; ++k)
        {
            if (KnotMath::crossingColoringOk(arcs_, k, colors_))
                continue;

            const KnotMath::Vec3 &p = diagram_.crossings.at(k).position;

            painter.setPen(QPen(kAccent(), 3.0));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(toWidget(p.x, p.y), 18.0, 18.0);
        }

        return;
    }

    // orientation: an arrow halfway along every arc (two on a lone loop)
    const double total = cum_.last();

    if (c == 0)
    {
        drawArrow(painter, sAtLength(0.25 * total), kLine());
        drawArrow(painter, sAtLength(0.75 * total), kLine());
    }
    else
    {
        for (int arc = 0; arc < c; ++arc)
        {
            const double l0 = lengthAt(arcs_.underS[arc]);
            double l1 = lengthAt(arcs_.underS[(arc + 1) % c]);

            if (l1 <= l0)
                l1 += total;

            drawArrow(painter, sAtLength(0.5 * (l0 + l1)), kLine());
        }
    }

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.3);
    painter.setFont(font);

    for (int k = 0; k < c; ++k)
    {
        const KnotMath::Vec3 &p = diagram_.crossings.at(k).position;
        const QPointF centre = toWidget(p.x, p.y);

        const int answer = signAnswers_.value(k, 0);

        QColor ring = answer == 0 ? kQuiet() : kAccent();

        if (signsChecked_ && answer != 0)
            ring = answer == arcs_.sign.value(k) ? kRight : kWrong;

        const double radius = k == hoverCrossing_ ? 20.0 : 18.0;

        painter.setPen(QPen(ring, k == hoverCrossing_ ? 2.5 : 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(centre, radius, radius);

        // badge beside the crossing, on the bisector of two strands so that
        // it sits in an empty sector (for unit tangents both the sum and the
        // difference are bisectors)
        const QPointF over = tangentAt(arcs_.overS.value(k));
        const QPointF under = tangentAt(arcs_.underPassS.value(k));

        QPointF out = over + under;

        if (std::hypot(out.x(), out.y()) < 0.5)
            out = over - under;

        out /= std::hypot(out.x(), out.y());

        const QPointF badge = centre + out * 32.0;

        painter.setPen(QPen(ring, 1.5));
        painter.setBrush(kBackground());
        painter.drawEllipse(badge, 12.0, 12.0);

        painter.setPen(answer == 0 ? kMuted() : ring);
        painter.drawText(
            QRectF(badge.x() - 14.0, badge.y() - 12.0, 28.0, 24.0),
            Qt::AlignCenter,
            answer == 0 ? QStringLiteral("?")
                        : (answer > 0 ? QStringLiteral("+") : QStringLiteral("−")));
    }

    drawSignLegend(painter);
}

/*
 * =========================================================
 * DIALOG
 * =========================================================
 */

PracticeDialog::PracticeDialog(
    const QVector<KnotMath::Vec3> &currentCurve,
    const QString &currentName,
    QWidget *parent)
    : QDialog(parent),
      currentCurve_(currentCurve),
      currentName_(currentName)
{
    setWindowTitle(tr("Practice"));

    setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(this);

    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    /*
     * exercise and knot
     */

    QHBoxLayout *top = new QHBoxLayout();

    exerciseCombo_ = new QComboBox(this);
    exerciseCombo_->addItem(tr("Tricolouring"));
    exerciseCombo_->addItem(tr("Crossing signs"));
    exerciseCombo_->setMinimumHeight(32);

    knotLabel_ = new QLabel(this);
    knotLabel_->setObjectName(QStringLiteral("Subtitle"));

    currentButton_ = new QPushButton(tr("Current knot"), this);
    currentButton_->setToolTip(tr("The knot shown in the 3D view."));
    currentButton_->setEnabled(!currentCurve_.isEmpty());

    QPushButton *randomButton = new QPushButton(tr("Mystery knot"), this);
    randomButton->setToolTip(
        tr("A knot with 3 to 8 crossings from the knot table; its name is "
           "shown when you are done."));

    QPushButton *drawButton = new QPushButton(tr("Draw..."), this);
    drawButton->setToolTip(tr("Draw your own knot diagram."));

    top->addWidget(exerciseCombo_);
    top->addSpacing(10);
    top->addWidget(knotLabel_, 1);
    top->addWidget(currentButton_);
    top->addWidget(randomButton);
    top->addWidget(drawButton);

    layout->addLayout(top);

    instructionsLabel_ = new QLabel(this);
    instructionsLabel_->setWordWrap(true);
    instructionsLabel_->setTextFormat(Qt::RichText);

    layout->addWidget(instructionsLabel_);

    canvas_ = new PracticeCanvas(this);

    layout->addWidget(canvas_, 1);

    /*
     * tools of the two exercises
     */

    coloringTools_ = new QWidget(this);

    QHBoxLayout *coloringLayout = new QHBoxLayout(coloringTools_);

    coloringLayout->setContentsMargins(0, 0, 0, 0);

    const QStringList brushNames = {tr("Red"), tr("Green"), tr("Blue")};

    for (int color = 0; color < 3; ++color)
    {
        QPushButton *button = new QPushButton(brushNames.at(color), coloringTools_);

        QPixmap swatch(14, 14);
        swatch.fill(Qt::transparent);

        {
            QPainter p(&swatch);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(PracticeCanvas::tricolor(color));
            p.drawEllipse(QRectF(1.0, 1.0, 12.0, 12.0));
        }

        button->setIcon(QIcon(swatch));
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("BrushButton"));
        button->setToolTip(tr("Click an arc to give it this colour (key %1). "
                              "Right-click an arc to remove its colour.")
                               .arg(color + 1));
        button->setShortcut(QKeySequence(QString::number(color + 1)));

        connect(button, &QPushButton::clicked,
                this, [this, color]() { brushChosen(color); });

        brushButtons_.push_back(button);
        coloringLayout->addWidget(button);
    }

    QPushButton *hintButton = new QPushButton(tr("Hint"), coloringTools_);
    QPushButton *impossibleButton = new QPushButton(tr("It can't be done"), coloringTools_);
    impossibleButton->setToolTip(
        tr("Claim that no colouring with two or more colours exists."));
    QPushButton *solutionButton = new QPushButton(tr("Show a solution"), coloringTools_);
    QPushButton *clearColorsButton = new QPushButton(tr("Clear"), coloringTools_);

    coloringLayout->addStretch(1);
    coloringLayout->addWidget(hintButton);
    coloringLayout->addWidget(impossibleButton);
    coloringLayout->addWidget(solutionButton);
    coloringLayout->addWidget(clearColorsButton);

    connect(hintButton, &QPushButton::clicked, this, &PracticeDialog::coloringHint);
    connect(impossibleButton, &QPushButton::clicked, this, &PracticeDialog::coloringImpossible);
    connect(solutionButton, &QPushButton::clicked, this, &PracticeDialog::coloringSolution);
    connect(clearColorsButton, &QPushButton::clicked, this, &PracticeDialog::coloringClear);

    layout->addWidget(coloringTools_);

    signTools_ = new QWidget(this);

    QHBoxLayout *signLayout = new QHBoxLayout(signTools_);

    signLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *checkButton = new QPushButton(tr("Check"), signTools_);
    checkButton->setObjectName(QStringLiteral("GenerateButton"));
    QPushButton *showButton = new QPushButton(tr("Show answers"), signTools_);
    QPushButton *clearSignsButton = new QPushButton(tr("Clear"), signTools_);

    signLayout->addStretch(1);
    signLayout->addWidget(checkButton);
    signLayout->addWidget(showButton);
    signLayout->addWidget(clearSignsButton);

    connect(checkButton, &QPushButton::clicked, this, &PracticeDialog::signsCheck);
    connect(showButton, &QPushButton::clicked, this, &PracticeDialog::signsShow);
    connect(clearSignsButton, &QPushButton::clicked, this, &PracticeDialog::signsClear);

    layout->addWidget(signTools_);

    feedbackLabel_ = new QLabel(this);
    feedbackLabel_->setWordWrap(true);
    feedbackLabel_->setTextFormat(Qt::RichText);
    feedbackLabel_->setMinimumHeight(54);
    feedbackLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    layout->addWidget(feedbackLabel_);

    QHBoxLayout *bottom = new QHBoxLayout();

    QPushButton *closeButton = new QPushButton(tr("Close"), this);

    bottom->addStretch(1);
    bottom->addWidget(closeButton);

    layout->addLayout(bottom);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(exerciseCombo_, &QComboBox::currentIndexChanged,
            this, &PracticeDialog::exerciseChanged);

    connect(currentButton_, &QPushButton::clicked, this, &PracticeDialog::useCurrentKnot);
    connect(randomButton, &QPushButton::clicked, this, &PracticeDialog::useRandomKnot);
    connect(drawButton, &QPushButton::clicked, this, &PracticeDialog::drawOwnKnot);

    connect(canvas_, &PracticeCanvas::changed,
            this,
            [this]()
            {
                if (exerciseCombo_->currentIndex() == 0)
                    coloringChanged();
                else
                    signsChanged();
            });

    resize(820, 780);

    brushChosen(0);
    exerciseChanged();

    if (!currentCurve_.isEmpty())
        useCurrentKnot();
    else
        useRandomKnot();
}

void PracticeDialog::exerciseChanged()
{
    const bool coloring = exerciseCombo_->currentIndex() == 0;

    canvas_->setMode(coloring ? PracticeCanvas::Mode::Coloring
                              : PracticeCanvas::Mode::Signs);

    coloringTools_->setVisible(coloring);
    signTools_->setVisible(!coloring);

    for (QPushButton *button : brushButtons_)
        button->setEnabled(coloring);

    updateInstructions();

    if (!canvas_->diagram().isValid())
        return;

    if (coloring)
        coloringChanged();
    else
        signsChanged();
}

void PracticeDialog::useCurrentKnot()
{
    QString error;

    const KnotMath::KnotDiagram diagram =
        KnotMath::diagramFromCurve(currentCurve_, kHalfExtent, error);

    setKnot(diagram, currentName_, false, error);
}

void PracticeDialog::useRandomKnot()
{
    const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();

    if (!table.isLoaded())
    {
        setKnot(KnotMath::KnotDiagram(), QString(), false, table.error());
        return;
    }

    // every crossing number equally likely (there is one 3-crossing knot
    // and 21 with 8 crossings)
    QVector<QVector<int>> byCrossings(9);

    for (int i = 0; i < table.size(); ++i)
    {
        const int c = table.at(i).crossings;

        if (c >= 3 && c <= 8)
            byCrossings[c].push_back(i);
    }

    QRandomGenerator *random = QRandomGenerator::global();

    QString lastError;

    // the table curves do not always project nicely; try a few
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const QVector<int> &pool = byCrossings[3 + random->bounded(6)];
        const int index = pool.at(random->bounded(pool.size()));

        QString error;

        const KnotMath::KnotDiagram diagram =
            KnotMath::diagramFromCurve(table.curve(index), kHalfExtent, error);

        if (!diagram.isValid() || diagram.crossings.size() > 14)
        {
            lastError = error;
            continue;
        }

        const KnotMath::KnotTableRecord &record = table.at(index);

        QString name = KnotMath::KnotTable::displayName(record.name);

        const QString common = KnotMath::KnotTable::commonName(record.name);

        if (!common.isEmpty())
            name = QStringLiteral("%1 (%2)").arg(common, name);

        setKnot(diagram, name, true);

        return;
    }

    setKnot(KnotMath::KnotDiagram(), QString(), false, lastError);
}

void PracticeDialog::drawOwnKnot()
{
    DiagramEditorDialog dialog(this);

    if (dialog.exec() != QDialog::Accepted || !dialog.diagram().isValid())
        return;

    setKnot(dialog.diagram(), tr("Your drawing"), false);
}

void PracticeDialog::setKnot(
    const KnotMath::KnotDiagram &diagram,
    const QString &name,
    bool mystery,
    const QString &error)
{
    knotName_ = name;
    mystery_ = mystery;
    solved_ = false;

    if (!diagram.isValid())
    {
        canvas_->setDiagram(KnotMath::KnotDiagram());

        updateKnotLabel();

        setFeedback(tr("This knot cannot be used for practice: %1")
                        .arg(error.isEmpty() ? tr("no clean picture of it was found.") : error));

        return;
    }

    if (diagram.crossings.size() > kMaxPracticeCrossings)
    {
        canvas_->setDiagram(KnotMath::KnotDiagram());

        updateKnotLabel();

        setFeedback(tr("The cleanest picture of this knot has %1 crossings - too many "
                       "to practise on. Choose a simpler knot, or let it relax in "
                       "the 3D view first.")
                        .arg(diagram.crossings.size()));

        return;
    }

    canvas_->setDiagram(diagram);

    updateKnotLabel();

    exerciseChanged();
}

void PracticeDialog::brushChosen(int color)
{
    for (int i = 0; i < brushButtons_.size(); ++i)
        brushButtons_[i]->setChecked(i == color);

    canvas_->setBrush(color);
}

QString PracticeDialog::revealName()
{
    const bool wasHidden = mystery_ && !solved_;

    solved_ = true;

    updateKnotLabel();

    return wasHidden ? tr("The mystery knot was <b>%1</b>. ").arg(knotName_)
                     : QString();
}

void PracticeDialog::updateKnotLabel()
{
    const int c = canvas_->diagram().crossings.size();

    QString name = knotName_;

    if (mystery_ && !solved_)
        name = tr("Mystery knot");

    if (!canvas_->diagram().isValid())
    {
        knotLabel_->setText(name);
        return;
    }

    knotLabel_->setText(
        tr("%1  ·  %2 crossings, %3 arcs")
            .arg(name)
            .arg(c)
            .arg(canvas_->arcs().arcCount));
}

void PracticeDialog::updateInstructions()
{
    if (exerciseCombo_->currentIndex() == 0)
    {
        instructionsLabel_->setText(
            tr("<b>Colour the arcs with three colours.</b> An <i>arc</i> runs from "
               "one under-crossing to the next. At every crossing the three arcs "
               "that meet must be <b>all the same</b> colour or <b>all different</b> "
               "colours, and you must use at least two colours. Pick a colour, then "
               "click arcs; a crossing that breaks the rule gets a highlighted ring."));
    }
    else
    {
        instructionsLabel_->setText(
            tr("<b>Find the sign of every crossing.</b> The arrows show the direction "
               "of travel. Look along the over strand: if the under strand passes "
               "from your right to your left, the crossing is <b>+1</b>, otherwise "
               "<b>−1</b> (see the corner). Click a crossing to mark it, then "
               "press Check."));
    }
}

void PracticeDialog::setFeedback(const QString &html)
{
    feedbackLabel_->setText(html);
}

QString PracticeDialog::determinantText() const
{
    const long long det = KnotMath::diagramDeterminant(canvas_->arcs());

    if (det < 0)
        return QString();

    return tr("The determinant of this knot is %1 (= |Δ(−1)|, the Alexander "
              "polynomial at t = −1). A knot can be tricoloured exactly when "
              "3 divides its determinant.")
        .arg(det);
}

/*
 * TRICOLOURING
 */

void PracticeDialog::coloringChanged()
{
    const KnotMath::DiagramArcs &arcs = canvas_->arcs();
    const QVector<int> &colors = canvas_->colors();

    int colored = 0;
    QVector<bool> used(3, false);

    for (int color : colors)
    {
        if (color >= 0)
        {
            ++colored;
            used[color] = true;
        }
    }

    const int distinct = static_cast<int>(std::count(used.begin(), used.end(), true));

    int broken = 0;

    for (int k = 0; k < arcs.overArc.size(); ++k)
    {
        if (!KnotMath::crossingColoringOk(arcs, k, colors))
            ++broken;
    }

    if (colored < arcs.arcCount)
    {
        QString text = tr("%1 of %2 arcs coloured.").arg(colored).arg(arcs.arcCount);

        if (broken > 0)
            text += QStringLiteral(" ") +
                    tr("<b>%n crossing(s) break the rule</b> (highlighted rings).", "", broken);

        setFeedback(text);
        return;
    }

    if (broken > 0)
    {
        setFeedback(tr("All arcs are coloured, but <b>%n crossing(s) break the rule</b> "
                       "(highlighted rings).", "", broken));
        return;
    }

    if (distinct < 2)
    {
        setFeedback(tr("Every crossing is fine, but you used only one colour. One colour "
                       "always works - even for the unknot - so it proves nothing. Use at "
                       "least two colours."));
        return;
    }

    const bool wasSolved = solved_;

    setFeedback(
        (wasSolved ? QString() : tr("<b>Solved!</b> ")) +
        revealName() +
        tr("A valid tricolouring proves that this knot is <b>not the unknot</b>: "
           "Reidemeister moves never destroy a tricolouring, and the plain circle "
           "only has one-colour ones. ") +
        determinantText());
}

void PracticeDialog::coloringHint()
{
    const KnotMath::DiagramArcs &arcs = canvas_->arcs();

    if (!canvas_->diagram().isValid())
        return;

    QVector<int> colors = canvas_->colors();

    const QVector<int> solution = KnotMath::completeTricoloring(arcs, colors);

    if (!solution.isEmpty())
    {
        // prefer an arc that meets an already coloured arc at a crossing
        int pick = -1;

        for (int k = 0; k < arcs.overArc.size() && pick < 0; ++k)
        {
            const int ends[3] = {arcs.overArc[k], arcs.inArc[k], arcs.outArc[k]};

            bool touchesColored = false;

            for (int arc : ends)
                touchesColored = touchesColored || colors[arc] >= 0;

            if (!touchesColored)
                continue;

            for (int arc : ends)
            {
                if (colors[arc] < 0)
                {
                    pick = arc;
                    break;
                }
            }
        }

        for (int arc = 0; arc < arcs.arcCount && pick < 0; ++arc)
        {
            if (colors[arc] < 0)
                pick = arc;
        }

        if (pick < 0)
        {
            // everything is coloured and consistent: only one colour used
            setFeedback(tr("Hint: change one arc to a different colour and follow "
                           "the rule from there - one colour proves nothing."));
            return;
        }

        colors[pick] = solution[pick];

        canvas_->setColors(colors);
        canvas_->setHighlightArc(pick);

        coloringChanged();

        setFeedback(tr("Hint: the glowing arc can be coloured like this. ") +
                    feedbackLabel_->text());
        return;
    }

    if (KnotMath::coloringDimension(arcs) < 2)
    {
        setFeedback(tr("Hint: does every attempt end with all arcs forced to the same "
                       "colour? Then perhaps it can't be done at all."));
        return;
    }

    // which single arc is in the way?
    for (int arc = 0; arc < arcs.arcCount; ++arc)
    {
        if (colors[arc] < 0)
            continue;

        QVector<int> without = colors;

        without[arc] = -1;

        if (!KnotMath::completeTricoloring(arcs, without).isEmpty())
        {
            canvas_->setHighlightArc(arc);

            setFeedback(tr("Hint: your colours can't all be kept. Try a different colour "
                           "for the glowing arc."));
            return;
        }
    }

    setFeedback(tr("Hint: your colours can't be completed. Clear them and start with "
                   "three different colours at one crossing."));
}

void PracticeDialog::coloringImpossible()
{
    const KnotMath::DiagramArcs &arcs = canvas_->arcs();

    if (!canvas_->diagram().isValid())
        return;

    if (KnotMath::coloringDimension(arcs) >= 2)
    {
        setFeedback(tr("<b>Not so fast</b> - this knot <i>can</i> be tricoloured. "
                       "Keep trying, or ask for a hint."));
        return;
    }

    QString text =
        tr("<b>Right!</b> ") +
        revealName() +
        tr("No colouring with two or more colours exists, so this knot is "
           "<b>not tricolourable</b>. ");

    text += determinantText();

    if (arcs.arcCount > 1)
    {
        text += QStringLiteral(" ") +
                tr("Careful: this does <i>not</i> prove it is the unknot - the "
                   "figure-eight knot can't be tricoloured either, and it is knotted.");
    }

    setFeedback(text);
}

void PracticeDialog::coloringSolution()
{
    const KnotMath::DiagramArcs &arcs = canvas_->arcs();

    if (!canvas_->diagram().isValid())
        return;

    QVector<int> solution = KnotMath::completeTricoloring(arcs, canvas_->colors());

    if (solution.isEmpty())
        solution = KnotMath::completeTricoloring(arcs, QVector<int>(arcs.arcCount, -1));

    if (solution.isEmpty())
    {
        setFeedback(revealName() +
                    tr("There is no solution: this knot can't be tricoloured. ") +
                    determinantText());
        return;
    }

    canvas_->setHighlightArc(-1);
    canvas_->setColors(solution);

    setFeedback(revealName() +
                tr("Here is a valid tricolouring. Check a few crossings yourself: "
                   "all the same or all different. ") +
                determinantText());
}

void PracticeDialog::coloringClear()
{
    canvas_->setHighlightArc(-1);
    canvas_->setColors(QVector<int>(canvas_->arcs().arcCount, -1));

    coloringChanged();
}

/*
 * CROSSING SIGNS
 */

void PracticeDialog::signsChanged()
{
    const QVector<int> &answers = canvas_->signAnswers();

    const int c = answers.size();

    if (c == 0)
    {
        setFeedback(tr("This diagram has no crossings, so its writhe is 0."));
        return;
    }

    int marked = 0;
    int sum = 0;

    for (int answer : answers)
    {
        if (answer != 0)
            ++marked;

        sum += answer;
    }

    setFeedback(tr("%1 of %2 crossings marked. Your sum so far (the writhe of the "
                   "diagram): %3.")
                    .arg(marked)
                    .arg(c)
                    .arg(sum > 0 ? QStringLiteral("+%1").arg(sum)
                                 : (sum < 0 ? QStringLiteral("−%1").arg(-sum)
                                            : QStringLiteral("0"))));
}

void PracticeDialog::signsCheck()
{
    const QVector<int> &answers = canvas_->signAnswers();
    const QVector<int> &signs = canvas_->arcs().sign;

    const int c = answers.size();

    if (c == 0)
        return;

    int right = 0;
    int unanswered = 0;
    int writhe = 0;

    for (int k = 0; k < c; ++k)
    {
        writhe += signs[k];

        if (answers[k] == 0)
            ++unanswered;
        else if (answers[k] == signs[k])
            ++right;
    }

    canvas_->setSignsChecked(true);

    if (right == c)
    {
        const QString reveal = revealName();

        setFeedback(tr("<b>All %1 correct!</b> ").arg(c) + reveal +
                    tr("The writhe of this diagram is %1. "
                       "The writhe is <i>not</i> a knot invariant - a single twist "
                       "(Reidemeister move I) changes it by ±1 - but it is the "
                       "correction that turns the Kauffman bracket into the Jones "
                       "polynomial. For two separate loops, half the sum of the signs "
                       "where they cross is the linking number, which <i>is</i> an "
                       "invariant.")
                        .arg(writhe > 0 ? QStringLiteral("+%1").arg(writhe)
                                        : (writhe < 0 ? QStringLiteral("−%1").arg(-writhe)
                                                      : QStringLiteral("0"))));
        return;
    }

    QString text = tr("%1 of %2 correct.").arg(right).arg(c - unanswered);

    if (unanswered > 0)
        text += QStringLiteral(" ") + tr("%n crossing(s) not marked yet.", "", unanswered);

    text += QStringLiteral(" ") +
            tr("Wrong answers have a red ring - follow the arrows once more.");

    setFeedback(text);
}

void PracticeDialog::signsShow()
{
    canvas_->setSignAnswers(canvas_->arcs().sign);

    signsCheck();
}

void PracticeDialog::signsClear()
{
    canvas_->setSignAnswers(QVector<int>(canvas_->diagram().crossings.size(), 0));
    canvas_->setSignsChecked(false);

    signsChanged();
}
