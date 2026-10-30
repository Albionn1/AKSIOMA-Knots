#include "icons.h"

#include "theme.h"

#include <QAbstractButton>
#include <QEvent>
#include <QFile>
#include <QFontMetricsF>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSvgRenderer>
#include <QTimer>

#include <cmath>

namespace
{

// the SVG of an icon, read once
const QByteArray &svgSource(const QString &name)
{
    static QHash<QString, QByteArray> sources;

    auto found = sources.find(name);

    if (found == sources.end())
    {
        QFile file(QStringLiteral(":/ui/icons/svg/%1.svg").arg(name));

        found = sources.insert(name, file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
    }

    return found.value();
}

/*
 * The colour of the button's text now, as the style sheet (theme.cpp)
 * gives it: the reference colours there are the dark theme's, and
 * Theme::colors() has them for the current theme.
 */
QColor textColour(const QAbstractButton *button)
{
    const Theme::Colors &c = Theme::colors();

    if (!button->isEnabled())
        return c.muted;

    const QString kind = button->objectName();

    const bool down = button->isDown();
    const bool hover = button->underMouse();

    // light button, dark text; dark with light text under the mouse
    if (kind == QStringLiteral("GenerateButton"))
        return down || hover ? c.text : c.window;

    // text-only buttons
    if (kind == QStringLiteral("DisclosureButton") || kind == QStringLiteral("TopBarButton"))
        return down ? c.accent : (hover || button->isChecked() ? c.text : c.quiet);

    // outlined buttons: filled with the text colour while pressed
    return down ? c.window : c.text;
}

// keeps one button's icon in the colour and size of its text
class Tint : public QObject
{
public:
    explicit Tint(QAbstractButton *button)
        : QObject(button),
          button_(button)
    {
        button->installEventFilter(this);

        connect(button, &QAbstractButton::pressed, this, &Tint::refresh);
        connect(button, &QAbstractButton::released, this, &Tint::refresh);
        connect(button, &QAbstractButton::toggled, this, &Tint::refresh);
    }

    QString name;

    void refresh()
    {
        if (button_ == nullptr)
            return;

        if (name.isEmpty())
        {
            button_->setIcon(QIcon());
            return;
        }

        // a little smaller than the letters are tall
        const qreal size = std::round(QFontMetricsF(button_->font()).height() * 0.78);
        const qreal ratio = button_->devicePixelRatioF();

        const QColor colour = textColour(button_);

        // the same picture again: nothing to do
        const QString key = QStringLiteral("%1 %2 %3 %4").arg(name, colour.name()).arg(size).arg(ratio);

        if (key == drawn_)
            return;

        drawn_ = key;

        QByteArray svg = svgSource(name);

        svg.replace("currentColor", colour.name().toLatin1());

        QSvgRenderer renderer(svg);

        QPixmap picture(QSize(qRound(size * ratio), qRound(size * ratio)));

        picture.fill(Qt::transparent);

        {
            QPainter painter(&picture);

            painter.setRenderHint(QPainter::Antialiasing);
            renderer.render(&painter);
        }

        picture.setDevicePixelRatio(ratio);

        // the same picture in every mode: the colour is chosen above
        QIcon icon;

        for (const QIcon::Mode mode : {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected})
        {
            icon.addPixmap(picture, mode, QIcon::Off);
            icon.addPixmap(picture, mode, QIcon::On);
        }

        button_->setIcon(icon);
        button_->setIconSize(QSize(qRound(size), qRound(size)));
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::EnabledChange:
        case QEvent::StyleChange:
        case QEvent::PaletteChange:
        case QEvent::FontChange:
        case QEvent::Show:
        case QEvent::ScreenChangeInternal:
            // after the button has taken in the change
            QTimer::singleShot(0, this, &Tint::refresh);
            break;

        default:
            break;
        }

        return QObject::eventFilter(object, event);
    }

private:
    QPointer<QAbstractButton> button_;
    QString drawn_;
};

} // namespace

namespace Icons
{

void set(
    QAbstractButton *button,
    const QString &name,
    bool after)
{
    if (button == nullptr)
        return;

    Tint *tint = dynamic_cast<Tint *>(
        button->findChild<QObject *>(QStringLiteral("IconTint"), Qt::FindDirectChildrenOnly));

    if (tint == nullptr)
    {
        tint = new Tint(button);
        tint->setObjectName(QStringLiteral("IconTint"));
    }

    tint->name = name;

    // the picture after the text (as on a "Next" button)
    button->setLayoutDirection(after ? Qt::RightToLeft : Qt::LeftToRight);

    tint->refresh();
}

} // namespace Icons
