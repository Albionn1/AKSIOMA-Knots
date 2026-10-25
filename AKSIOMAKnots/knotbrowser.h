#ifndef KNOTBROWSER_H
#define KNOTBROWSER_H

#include <QDialog>
#include <QVector>

class KnotGLWidget;
class KnotListModel;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QModelIndex;
class QPushButton;

// Window to choose one of the 12,965 prime knots with up to 13 crossings
// (KnotInfo): search and filters, a list, a rotating 3D preview and the
// knot's invariants.
class KnotBrowserDialog final
    : public QDialog
{
    Q_OBJECT

public:
    // `current`: KnotInfo name to select at the start (may be empty)
    explicit KnotBrowserDialog(const QString &current, QWidget *parent = nullptr);

    // KnotInfo name of the chosen knot, empty if none
    QString selectedKnot() const;

private:
    void applyFilter();
    void showKnot(int tableIndex);
    void currentChanged(const QModelIndex &index);

    QLineEdit *search_ = nullptr;
    QComboBox *crossingFilter_ = nullptr;
    QComboBox *typeFilter_ = nullptr;
    QLabel *countLabel_ = nullptr;

    QListView *list_ = nullptr;
    KnotListModel *model_ = nullptr;

    KnotGLWidget *preview_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *details_ = nullptr;
    QPushButton *loadButton_ = nullptr;

    int selected_ = -1;
};

#endif // KNOTBROWSER_H
