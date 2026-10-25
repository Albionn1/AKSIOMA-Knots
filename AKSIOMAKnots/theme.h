#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QString>

/*
 * Dark (the original look) and light colour themes.
 *
 * The dark colours are the reference: the style sheet and the HTML texts
 * are written with them, and html() / the style sheet map them to the
 * current theme.
 *
 *   #060606  window, viewport, fields   #0D0D0D  panels
 *   #BAB9B4  text                       #53514C  borders, quiet text
 *   #8A8780  secondary text             #D99B42  brand, a few headings
 */
namespace Theme
{

struct Colors
{
    QColor window;
    QColor panel;
    QColor text;
    QColor muted;
    QColor quiet;
    QColor accent;
};

bool isDark();

const Colors &colors();

// Text size in percent of the normal size (100).
int textScale();

// Installs the palette and the style sheet of the application.
void apply(
    bool dark,
    int textScale);

// Maps the dark reference colours in an HTML text to the current theme
// and scales its point sizes with the text size.
QString html(
    const QString &source);

} // namespace Theme

#endif // THEME_H
