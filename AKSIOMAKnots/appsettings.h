#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include "lighting.h"

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

// Folder of the last knot file opened, saved or exported (Documents at
// first).
QString lastFolder();
void setLastFolder(
    const QString &folder);

// Lights and shadows of the 3D view (Settings, Lighting).
Lighting::Options lighting();
void setLighting(
    const Lighting::Options &options);

// the level Photo mode's automatic quality last settled on (1 high, 2
// medium, 3 low): it starts there the next time
int photoAutoLevel();

void setPhotoAutoLevel(
    int level);

// Removes every stored setting.
void restoreDefaults();

// Installs the translator for the language (English needs none). Widgets
// keep their texts: the main window is rebuilt after a change.
void installLanguage(
    const QString &code);

// Language, theme and text size from the settings.
void applyAll();

// Windows: makes .aknot files show the program's icon in Explorer and open
// in this program on a double click (for the current user, in
// HKEY_CURRENT_USER\Software\Classes). The copy started last owns them.
// Elsewhere it does nothing.
void registerFileType();

} // namespace AppSettings

#endif // APPSETTINGS_H
