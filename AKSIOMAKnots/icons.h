#ifndef ICONS_H
#define ICONS_H

#include <QString>

class QAbstractButton;

/*
 * The small pictures on the buttons (play, pause, stop, the hand that
 * grabs the rope, scissors, ...), drawn as SVG (icons/svg/<name>.svg, in
 * currentColor) instead of text symbols, so they look the same with every
 * font. Each is drawn in the colour the button's text has at that moment:
 * normal, under the mouse, pressed, checked, disabled, in the dark and in
 * the light theme, and as large as its text.
 */
namespace Icons
{

// Puts the icon `name` before the button's text (after it with `after`);
// an empty name takes it away. Called again, it changes the icon.
void set(
    QAbstractButton *button,
    const QString &name,
    bool after = false);

} // namespace Icons

#endif // ICONS_H
