#include "aboutdialog.h"

#include "theme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About AKSIOMA Knots"));
    resize(620, 820);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);

    // The logo on its own dark background (it is drawn for one), in both
    // themes; loaded at full size and scaled down for sharp high-DPI screens.
    QLabel *logo = new QLabel(this);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QStringLiteral(
        "QLabel { background-color: #090909; border-radius: 8px; padding: 14px; }"));

    QPixmap logoImage(QStringLiteral(":/ui/icons/aksioma_logo.png"));

    const qreal ratio = devicePixelRatioF();

    logoImage = logoImage.scaledToWidth(
        static_cast<int>(260 * ratio),
        Qt::SmoothTransformation);
    logoImage.setDevicePixelRatio(ratio);

    logo->setPixmap(logoImage);

    layout->addWidget(logo);

    QTextBrowser *text = new QTextBrowser(this);
    text->setOpenExternalLinks(true);
    text->setFrameShape(QFrame::NoFrame);

    const QString heading =
        QStringLiteral("style='font-size:10.5pt; font-weight:700; color:#BAB9B4; "
                       "margin-top:14px;'");

    text->setHtml(Theme::html(
        QStringLiteral(
            "<p style='font-size:20pt; font-weight:700; color:#D99B42; margin-bottom:0;'>"
            "AKSIOMA Knots</p>"
            "<p style='color:#8A8780; margin-top:2px;'>%1</p>"
            "<p>%2</p>"
            "<p>%3</p>"

            "<div %4>%5</div>"
            "<p>%6</p>"
            "<p style='margin-left:12px;'>C. Livingston and A. H. Moore, "
            "<i>KnotInfo: Table of Knot Invariants</i>, "
            "<a href='https://knotinfo.org' style='color:#D99B42;'>knotinfo.org</a>, "
            "October 24, 2026.</p>"
            "<p>%7</p>"

            "<div %4>%8</div>"
            "<p>%9</p>"

            "<div %4>%15</div>"
            "<p>%16</p>"

            "<div %4>%10</div>"
            "<ul style='margin-left:-20px;'>"
            "<li>%11</li>"
            "<li>%12</li>"
            "<li>%13</li>"
            "<li>%14</li>"
            "</ul>")
            .arg(tr("Version %1").arg(QApplication::applicationVersion()) +
                     QStringLiteral("<br>") +
                     tr("Created by <b style='color:#BAB9B4;'>Albion Berisha</b>, "
                        "creator and CEO of AKSIOMA"),
                 tr("An interactive 3D laboratory for knot theory: explore knots, "
                    "compute their invariants, identify drawn knots and watch "
                    "them being unknotted."),
                 tr("Copyright © 2026 AKSIOMA. Free software, licensed under the "
                    "<a href='https://www.gnu.org/licenses/gpl-3.0.html' "
                    "style='color:#D99B42;'>GNU General Public License version 3</a> "
                    "(see the LICENSE file). You may share and change it under "
                    "the terms of that license. This program comes with "
                    "ABSOLUTELY NO WARRANTY."),
                 heading,
                 tr("KNOT TABLE"),
                 tr("The 12,965 prime knots with up to 13 crossings in the Knot "
                    "Browser - their 3D shapes, names and invariants - come from "
                    "KnotInfo:"),
                 tr("KnotInfo in turn credits many sources, among them the Knot "
                    "Atlas (D. Bar-Natan) and Knotscape (J. Hoste and M. "
                    "Thistlethwaite). Many thanks to Chuck Livingston and Allison "
                    "Moore for making the data available."),
                 tr("BUILT WITH"),
                 tr("Qt (The Qt Company), used under the GNU Lesser General Public "
                    "License version 3."),
                 tr("MATHEMATICS"))
            .arg(tr("Writhe and linking numbers: exact Gauss integrals of polygons "
                    "(K. Klenin and J. Langowski, Biopolymers 54, 2000)."),
                 tr("Alexander polynomial: Wirtinger presentation and Fox free "
                    "differential calculus, exact integer arithmetic."),
                 tr("Jones polynomial: Kauffman bracket state sum (L. H. Kauffman, "
                    "1987)."),
                 tr("Unknotting animation: crossing changes to a descending "
                    "diagram, which is always an unknot."))
            .arg(tr("PHOTO MODE"),
                 tr("The rooms and surfaces of Photo mode come from "
                    "<a href='https://polyhaven.com' style='color:#D99B42;'>Poly Haven</a> "
                    "and are free for any use (CC0): the HDRIs Aft Lounge (Greg Zaal) "
                    "and Country Club (Sergej Majboroda), the textures Wood Table "
                    "Large and Wood Floor Worn (Dimitrios Savva). The rope lies down "
                    "by position based dynamics (XPBD: M. Macklin, M. Müller and N. "
                    "Chentanez, 2016)."))));

    layout->addWidget(text, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    QPushButton *aboutQt = buttons->addButton(tr("About Qt"), QDialogButtonBox::HelpRole);

    connect(aboutQt, &QPushButton::clicked, this, [this]() { QMessageBox::aboutQt(this); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(buttons);
}
