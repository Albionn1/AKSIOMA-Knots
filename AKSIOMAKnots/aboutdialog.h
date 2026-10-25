#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>

// About AKSIOMA Knots: what the program is, and the credits (knot data from
// KnotInfo, Qt, the mathematics used).
class AboutDialog final
    : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

#endif // ABOUTDIALOG_H
