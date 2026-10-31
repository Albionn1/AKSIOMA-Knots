#include "commandpalette.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Palette
{

QString folded(const QString &text)
{
    // compatibility decomposition: ë -> e + mark, ₁ -> 1
    const QString plain = text.normalized(QString::NormalizationForm_KD);

    QString result;

    result.reserve(plain.size());

    for (const QChar c : plain)
    {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;

        result += c == QLatin1Char('_') ? QChar(QLatin1Char(' ')) : c.toLower();
    }

    return result;
}

int score(const QString &query, const QString &text)
{
    if (query.isEmpty())
        return 1;

    const int at = static_cast<int>(text.indexOf(query));

    if (at == 0)
        return 400 - std::min<int>(static_cast<int>(text.size()), 100);

    if (at > 0)
    {
        const bool wordStart = !text.at(at - 1).isLetterOrNumber();

        return (wordStart ? 300 : 200) - std::min(at, 50);
    }

    // the letters in order, with gaps
    int place = 0;

    for (const QChar c : query)
    {
        if (c.isSpace())
            continue;

        place = static_cast<int>(text.indexOf(c, place));

        if (place < 0)
            return 0;

        ++place;
    }

    return 50;
}

} // namespace Palette

CommandPalette::CommandPalette(
    const QVector<PaletteItem> &items,
    const std::function<QVector<PaletteItem>(const QString &)> &search,
    QWidget *parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog),
      items_(items),
      search_(search)
{
    setObjectName(QStringLiteral("Palette"));

    resize(620, 420);

    QVBoxLayout *layout = new QVBoxLayout(this);

    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    edit_ = new QLineEdit(this);

    edit_->setMinimumHeight(38);
    edit_->setPlaceholderText(tr("Type an action or a knot"));
    edit_->installEventFilter(this);

    list_ = new QListWidget(this);

    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    layout->addWidget(edit_);
    layout->addWidget(list_, 1);

    connect(edit_, &QLineEdit::textChanged, this, &CommandPalette::refill);

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item)
    {
        choose(list_->row(item));
    });

    refill();

    edit_->setFocus();
}

void CommandPalette::refill()
{
    const QString query = Palette::folded(edit_->text()).trimmed();

    QVector<QPair<int, PaletteItem>> found;

    for (const PaletteItem &item : items_)
    {
        const int best = Palette::score(query, Palette::folded(item.title));

        if (best > 0)
            found.push_back({best, item});
    }

    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b)
    {
        return a.first > b.first;
    });

    shown_.clear();

    for (const auto &entry : found)
        shown_.push_back(entry.second);

    // the actions first when the text fits one well; knots after
    if (!query.isEmpty() && search_)
    {
        const QVector<PaletteItem> more = search_(query);

        if (!found.isEmpty() && found.first().first >= 200)
            shown_ += more;
        else
            shown_ = more + shown_;
    }

    if (shown_.size() > 40)
        shown_.resize(40);

    list_->clear();

    for (const PaletteItem &item : shown_)
    {
        const QString line =
            item.detail.isEmpty()
                ? item.title
                : QStringLiteral("%1      %2").arg(item.title, item.detail);

        list_->addItem(line);
    }

    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

void CommandPalette::choose(int row)
{
    if (row < 0 || row >= shown_.size())
        return;

    chosen_ = shown_.at(row);

    accept();
}

bool CommandPalette::eventFilter(QObject *object, QEvent *event)
{
    if (object == edit_ && event->type() == QEvent::KeyPress)
    {
        const int key = static_cast<QKeyEvent *>(event)->key();

        if (key == Qt::Key_Down || key == Qt::Key_Up)
        {
            const int count = list_->count();

            if (count > 0)
                list_->setCurrentRow((list_->currentRow() + (key == Qt::Key_Down ? 1 : count - 1)) % count);

            return true;
        }

        if (key == Qt::Key_Return || key == Qt::Key_Enter)
        {
            choose(list_->currentRow());

            return true;
        }
    }

    return QDialog::eventFilter(object, event);
}
