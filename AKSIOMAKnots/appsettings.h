#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QByteArray>
#include <QString>

/*
 * User settings, stored with QSettings (on Windows in the registry under
 * HKEY_CURRENT_USER\Software\AKSIOMA\AKSIOMA Knots).
 */
namespace AppSettings
{

// "en" or "sq"; the first start follows the system language.
QString language();
void setLanguage(
    const QString &code);

bool darkTheme();
void setDarkTheme(
    bool dark);

// percent of the normal text size
int textScale();
void setTextScale(
    int percent);

bool startFullScreen();
void setStartFullScreen(
    bool enabled);

bool rememberWindow();
void setRememberWindow(
    bool enabled);

QByteArray windowGeometry();
void setWindowGeometry(
    const QByteArray &geometry);

// Removes every stored setting.
void restoreDefaults();

// Installs the translator for the language (English needs none). Widgets
// keep their texts: the main window is rebuilt after a change.
void installLanguage(
    const QString &code);

// Language, theme and text size from the settings.
void applyAll();

} // namespace AppSettings

#endif // APPSETTINGS_H
