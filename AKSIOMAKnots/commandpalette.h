#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

#include <QDialog>
#include <QString>
#include <QVector>

#include <functional>

class QLineEdit;
class QListWidget;

/*
 * Ctrl+K: one box to find anything, an action of the program or a knot of
 * the table, by typing a few letters. It forgives accents and marks, so that
 * "cilesimet" finds "Cilësimet" and "8 19", "8_19" and "819" all find 8₁₉.
 */
struct PaletteItem
{
    QString title;

    // where it belongs, or its shortcut
    QString detail;

    std::function<void()> run;
};

namespace Palette
{

// Lower case, no accents, subscripts as plain digits, `_` as a space.
QString folded(const QString &text);

// How well `query` fits `text` (both folded): at the start best, then at the
// start of a word, inside a word, then its letters in order; 0: not at all.
int score(const QString &query, const QString &text);

} // namespace Palette

class CommandPalette final
    : public QDialog
{
    Q_OBJECT

public:
    // `items` are always searched; `search` adds what matches the text typed
    // (the table of knots is too long to list up front)
    CommandPalette(
        const QVector<PaletteItem> &items,
        const std::function<QVector<PaletteItem>(const QString &)> &search,
        QWidget *parent);

    // the item chosen (no `run`: none); it runs after this window is gone
    const PaletteItem &chosen() const { return chosen_; }

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void refill();
    void choose(int row);

    QVector<PaletteItem> items_;
    QVector<PaletteItem> shown_;
    std::function<QVector<PaletteItem>(const QString &)> search_;

    PaletteItem chosen_;

    QLineEdit *edit_ = nullptr;
    QListWidget *list_ = nullptr;
};

#endif // COMMANDPALETTE_H
