#include "unknotsummarydialog.h"

#include "icons.h"
#include "theme.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace
{

// A knot diagram on a dark panel with a caption, like the one in the corner
// of the 3D view during the animation.
class DiagramView final
    : public QWidget
{
public:
    explicit DiagramView(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(240, 240);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setInset(const KnotGLWidget::DiagramInset &inset)
    {
        inset_ = inset;
        update();
    }

    int heightForWidth(int width) const override
    {
        return width;
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const double side = std::min(width(), height()) - 2.0;

        const QRectF panel(
            0.5 * (width() - side), 0.5 * (height() - side), side, side);

        const QColor background(14, 14, 14);

        painter.setPen(QPen(QColor(48, 47, 45), 1.0));
        painter.setBrush(background);
        painter.drawRoundedRect(panel, 8.0, 8.0);

        QFont font = painter.font();
        font.setPointSizeF(8.0);
        font.setBold(true);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
        painter.setFont(font);
        painter.setPen(QColor(138, 135, 128));
        painter.drawText(
            panel.adjusted(14.0, 10.0, -14.0, 0.0),
            Qt::AlignLeft | Qt::AlignTop,
            inset_.caption);

        KnotGLWidget::paintDiagram(
            painter,
            panel.adjusted(24.0, 36.0, -24.0, -24.0),
            inset_,
            background);
    }

private:
    KnotGLWidget::DiagramInset inset_;
};

DiagramView *view(QWidget *widget)
{
    return static_cast<DiagramView *>(widget);
}

} // namespace

UnknotSummaryDialog::UnknotSummaryDialog(
    const Summary &summary,
    QWidget *parent)
    : QDialog(parent)
    , summary_(summary)
{
    setWindowTitle(tr("Unknotting summary"));
    resize(900, 620);

    const int changes = changeCount();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    /*
     * Header
     */

    QString subtitle =
        tr("Knot diagram with %n crossing(s)", "", summary_.diagram.crossings.size());

    if (!summary_.knotName.isEmpty())
        subtitle = summary_.knotName.toHtmlEscaped() + QStringLiteral(" · ") + subtitle;

    if (!summary_.known.isEmpty())
        subtitle += QStringLiteral(" · ") + tr("known unknotting number %1").arg(summary_.known);

    QLabel *header = new QLabel(this);
    header->setWordWrap(true);
    header->setText(Theme::html(
        QStringLiteral(
            "<p style='font-size:17pt; font-weight:700; color:#D99B42; margin:0;'>%1</p>"
            "<p style='color:#8A8780; margin-top:4px;'>%2</p>")
            .arg(changes == 0
                     ? tr("Unknotted without any crossing change")
                     : tr("Unknotted with %n crossing change(s)", "", changes),
                 subtitle)));

    layout->addWidget(header);

    /*
     * Steps on the left, the diagrams and the explanation on the right
     */

    QHBoxLayout *body = new QHBoxLayout();
    body->setSpacing(16);

    steps_ = new QListWidget(this);
    steps_->setMinimumWidth(200);
    steps_->setMaximumWidth(260);

    steps_->addItem(tr("Starting diagram"));

    for (int k = 1; k <= changes; ++k)
        steps_->addItem(tr("Crossing change %1").arg(k));

    steps_->addItem(tr("Descending diagram: unknot"));

    body->addWidget(steps_);

    QVBoxLayout *detail = new QVBoxLayout();
    detail->setSpacing(10);

    stepTitle_ = new QLabel(this);
    stepTitle_->setWordWrap(true);

    detail->addWidget(stepTitle_);

    QHBoxLayout *views = new QHBoxLayout();
    views->setSpacing(12);

    beforeView_ = new DiagramView(this);
    afterView_ = new DiagramView(this);

    views->addWidget(beforeView_, 1);
    views->addWidget(afterView_, 1);

    detail->addLayout(views, 1);

    stepText_ = new QLabel(this);
    stepText_->setWordWrap(true);
    stepText_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    stepText_->setMinimumHeight(90);

    detail->addWidget(stepText_);

    body->addLayout(detail, 1);

    layout->addLayout(body, 1);

    /*
     * Buttons
     */

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    previousButton_ = buttons->addButton(tr("Previous"), QDialogButtonBox::ActionRole);
    nextButton_ = buttons->addButton(tr("Next"), QDialogButtonBox::ActionRole);

    Icons::set(previousButton_, QStringLiteral("chevron_left"));
    Icons::set(nextButton_, QStringLiteral("chevron_right"), true);

    QPushButton *show3d = buttons->addButton(tr("Show in 3D"), QDialogButtonBox::ActionRole);

    show3d->setToolTip(tr("Takes the animation to this moment."));

    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(steps_, &QListWidget::currentRowChanged, this, &UnknotSummaryDialog::showStep);

    connect(previousButton_, &QPushButton::clicked, this,
            [this]() { steps_->setCurrentRow(std::max(0, steps_->currentRow() - 1)); });

    connect(nextButton_, &QPushButton::clicked, this,
            [this]() { steps_->setCurrentRow(std::min(steps_->count() - 1, steps_->currentRow() + 1)); });

    connect(show3d, &QPushButton::clicked, this,
            [this]()
            {
                const int step = steps_->currentRow();
                const int count = changeCount();

                double t = summary_.startTime;

                if (step > count)
                    t = summary_.resultTime;
                else if (step >= 1)
                    t = summary_.changeTimes.at(step - 1);

                emit showMoment(t);
            });

    // a change is the most interesting place to start
    steps_->setCurrentRow(changes > 0 ? 1 : 0);
}

int UnknotSummaryDialog::changeCount() const
{
    return summary_.changeTimes.size();
}

void UnknotSummaryDialog::showStep(int step)
{
    if (step < 0)
        return;

    const int changes = changeCount();
    const int crossings = summary_.diagram.crossings.size();

    using Inset = KnotGLWidget::DiagramInset;

    previousButton_->setEnabled(step > 0);
    nextButton_->setEnabled(step < steps_->count() - 1);

    QString title;
    QString text;

    if (step == 0)
    {
        Inset inset = Inset::fromUnknotting(summary_.diagram, 0, 0);
        inset.caption = tr("STARTING DIAGRAM");

        view(beforeView_)->setInset(inset);
        afterView_->hide();

        title = tr("The knot as a picture");

        text = tr("The animation first turns the knot to the view that needs the "
                  "fewest changes and flattens it into this diagram with %n "
                  "crossing(s). Walk along it from the white dot in the direction "
                  "of the arrow.", "", crossings);

        text += QStringLiteral("<br><br>");

        if (changes == 0)
        {
            text += tr("Every crossing is reached on the upper strand first: the "
                       "diagram is already descending, so the curve is an unknot "
                       "and no crossing has to be changed.");
        }
        else
        {
            text += tr("%n crossing(s) are reached on the lower strand first. They "
                       "are circled in <span style='color:#D99B42;'>amber</span> "
                       "and numbered in the order they are changed.", "", changes);
        }
    }
    else if (step <= changes)
    {
        Inset before = Inset::fromUnknotting(summary_.diagram, step - 1, step);
        before.caption = tr("BEFORE");

        Inset after = Inset::fromUnknotting(summary_.diagram, step, step);
        after.caption = tr("AFTER");

        view(beforeView_)->setInset(before);
        view(afterView_)->setInset(after);
        afterView_->show();

        title = tr("Crossing change %1 of %2").arg(step).arg(changes);

        // the crossing changed in this step
        const KnotMath::UnknottingAnimation::DiagramCrossing *crossing = nullptr;

        for (const KnotMath::UnknottingAnimation::DiagramCrossing &c :
             summary_.diagram.crossings)
        {
            if (c.change == step)
                crossing = &c;
        }

        if (crossing != nullptr)
        {
            const int under = 1 - crossing->overAtStart;
            const int over = crossing->overAtStart;

            const int first = static_cast<int>(std::lround(100.0 * crossing->u[under]));
            const int second = static_cast<int>(std::lround(100.0 * crossing->u[over]));

            text = tr("Walking from the white dot, this crossing is reached first "
                      "%1 % of the way along, on the strand "
                      "<span style='color:#D99B42;'>below</span>, and again at %2 % "
                      "on the strand <span style='color:#5B9BFF;'>on top</span>.")
                       .arg(first)
                       .arg(second);
        }

        text += QStringLiteral("<br><br>") +
                tr("A descending diagram meets every crossing on top first, so the "
                   "two strands pass through each other: now the "
                   "<span style='color:#D99B42;'>amber</span> strand runs over the "
                   "<span style='color:#5B9BFF;'>blue</span> one. Real rope cannot "
                   "do this - it is the one move that changes the knot.");
    }
    else
    {
        Inset inset = Inset::fromUnknotting(summary_.diagram, changes, 0);
        inset.caption = tr("DESCENDING DIAGRAM");

        view(beforeView_)->setInset(inset);
        afterView_->hide();

        title = tr("Downhill all the way: an unknot");

        text = tr("Now every crossing is reached on top first (the circles are "
                  "<span style='color:#6CC08B;'>green</span>). Such a curve only "
                  "goes downhill, so it can be lifted apart into a plain circle "
                  "without passing any strand through another.");

        if (changes > 0)
        {
            text += QStringLiteral("<br><br>") +
                    tr("So this knot can be undone with %n crossing change(s): an "
                       "upper bound for its unknotting number.", "", changes);

            bool isNumber = false;
            const int known = summary_.known.toInt(&isNumber);

            if (isNumber && known == changes)
            {
                text += QLatin1Char(' ') +
                        tr("Its known unknotting number is %1, so no way with "
                           "fewer changes exists.")
                            .arg(known);
            }
            else if (isNumber && known < changes)
            {
                text += QLatin1Char(' ') +
                        tr("Its known unknotting number is %1: a cleverer choice "
                           "(of the diagram or of the crossings) needs fewer "
                           "changes.")
                            .arg(known);
            }
            else if (!summary_.known.isEmpty())
            {
                text += QLatin1Char(' ') +
                        tr("Known unknotting number: %1.").arg(summary_.known);
            }
        }
    }

    stepTitle_->setText(Theme::html(
        QStringLiteral("<span style='font-size:12pt; font-weight:700; color:#BAB9B4;'>%1</span>")
            .arg(title)));

    stepText_->setText(Theme::html(text));
}
