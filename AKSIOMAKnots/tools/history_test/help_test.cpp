// Headless check of the command palette's matching and of typed slider
// values (commandpalette.cpp, controlhelp.cpp).

#include "commandpalette.h"
#include "controlhelp.h"

#include <QApplication>

#include <cstdio>

namespace
{

int failures = 0;

void check(bool passed, const char *what)
{
    std::printf("%s  %s\n", passed ? "ok  " : "FAIL", what);

    if (!passed)
        ++failures;
}

int scored(const QString &query, const QString &text)
{
    return Palette::score(Palette::folded(query), Palette::folded(text));
}

int typed(const QString &text, double unit, int low, int high)
{
    int value = -1;

    return ControlHelp::parseValue(text, unit, low, high, value) ? value : -999;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    check(Palette::folded(QStringLiteral("Cilësimet")) == QStringLiteral("cilesimet"), "accents go");
    check(Palette::folded(QStringLiteral("Çelësi")) == QStringLiteral("celesi"), "a cedilla goes");
    check(Palette::folded(QStringLiteral("8₁₉")) == QStringLiteral("819"), "subscripts are digits");
    check(Palette::folded(QStringLiteral("8_19")) == QStringLiteral("8 19"), "an underscore is a space");

    check(scored(QStringLiteral("cilesi"), QStringLiteral("Cilësimet")) > 0, "typed without the accent, it is found");
    check(scored(QStringLiteral("zz"), QStringLiteral("Cilësimet")) == 0, "letters that are not there: nothing");
    check(scored(QStringLiteral("ruaj"), QStringLiteral("Ruaj nyjën")) >
              scored(QStringLiteral("ruaj"), QStringLiteral("Mos e ruaj")),
          "the start of the text beats a later word");
    check(scored(QStringLiteral("nyj"), QStringLiteral("Hap nyjën")) >
              scored(QStringLiteral("yje"), QStringLiteral("Hap nyjën")),
          "the start of a word beats the middle of one");
    check(scored(QStringLiteral("hn"), QStringLiteral("Hap nyjën")) > 0, "letters in order, with gaps");
    check(scored(QStringLiteral("nh"), QStringLiteral("Hap nyjën")) == 0, "but not out of order");
    check(scored(QString(), QStringLiteral("Hap")) > 0, "nothing typed: everything shows");

    check(typed(QStringLiteral("0.30"), 0.01, 2, 60) == 30, "a decimal number");
    check(typed(QStringLiteral("0,30"), 0.01, 2, 60) == 30, "with a decimal comma");
    check(typed(QStringLiteral("120%"), 1.0, 50, 150) == 120, "a percent sign");
    check(typed(QStringLiteral(" 200 "), 1.0, 48, 320) == 200, "spaces");
    check(typed(QStringLiteral("5"), 0.01, 2, 60) == 60, "too large: the end of the range");
    check(typed(QStringLiteral("-3"), 1.0, 50, 150) == 50, "too small: the other end");
    check(typed(QStringLiteral("abc"), 1.0, 0, 100) == -999, "not a number: refused");
    check(typed(QString(), 1.0, 0, 100) == -999, "nothing typed: refused");
    check(typed(QStringLiteral("nan"), 1.0, 0, 100) == -999, "not-a-number: refused");

    std::printf("\n%d failures\n", failures);

    return failures == 0 ? 0 : 1;
}
