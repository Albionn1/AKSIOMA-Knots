#include "appsettings.h"

#include "theme.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

#include <algorithm>

#ifdef Q_OS_WIN
#include <QDir>

#include <shlobj.h>
#endif

namespace
{

const QString LanguageKey = QStringLiteral("language");
const QString ThemeKey = QStringLiteral("theme");
const QString TextScaleKey = QStringLiteral("textScale");
const QString StartFullScreenKey = QStringLiteral("startFullScreen");
const QString RememberWindowKey = QStringLiteral("rememberWindow");
const QString WindowGeometryKey = QStringLiteral("windowGeometry");
const QString LastFolderKey = QStringLiteral("lastFolder");
const QString LightingGroup = QStringLiteral("lighting");

QTranslator *installedTranslator = nullptr;

// Texts of Qt's own buttons and colour dialog. Qt has no Albanian
// translation, so they are listed here and lupdate adds them to our .ts.
[[maybe_unused]] const char *const QtTexts[] =
{
    QT_TRANSLATE_NOOP("QPlatformTheme", "OK"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Open"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Save"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Cancel"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Close"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Discard"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Apply"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Reset"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Restore Defaults"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "Help"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&Yes"),
    QT_TRANSLATE_NOOP("QPlatformTheme", "&No"),

    QT_TRANSLATE_NOOP("QColorDialog", "Select Color"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Basic colors"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Custom colors"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Add to Custom Colors"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Pick Screen Color"),
    QT_TRANSLATE_NOOP("QColorDialog", "Hu&e:"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Sat:"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Val:"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Red:"),
    QT_TRANSLATE_NOOP("QColorDialog", "&Green:"),
    QT_TRANSLATE_NOOP("QColorDialog", "Bl&ue:"),
    QT_TRANSLATE_NOOP("QColorDialog", "A&lpha channel:"),
    QT_TRANSLATE_NOOP("QColorDialog", "&HTML:")
};

} // namespace

namespace AppSettings
{

QString language()
{
    const QString fallback =
        QLocale::system().language() == QLocale::Albanian
            ? QStringLiteral("sq")
            : QStringLiteral("en");

    const QString code =
        QSettings().value(LanguageKey, fallback).toString();

    return code == QStringLiteral("sq")
               ? code
               : QStringLiteral("en");
}

void setLanguage(
    const QString &code)
{
    QSettings().setValue(LanguageKey, code);
}

bool darkTheme()
{
    return QSettings().value(ThemeKey, QStringLiteral("dark")).toString() !=
           QStringLiteral("light");
}

void setDarkTheme(
    bool dark)
{
    QSettings().setValue(
        ThemeKey,
        dark ? QStringLiteral("dark") : QStringLiteral("light"));
}

int textScale()
{
    return QSettings().value(TextScaleKey, 100).toInt();
}

void setTextScale(
    int percent)
{
    QSettings().setValue(TextScaleKey, percent);
}

bool startFullScreen()
{
    return QSettings().value(StartFullScreenKey, false).toBool();
}

void setStartFullScreen(
    bool enabled)
{
    QSettings().setValue(StartFullScreenKey, enabled);
}

bool rememberWindow()
{
    return QSettings().value(RememberWindowKey, true).toBool();
}

void setRememberWindow(
    bool enabled)
{
    QSettings().setValue(RememberWindowKey, enabled);
}

QString lastFolder()
{
    return QSettings()
        .value(LastFolderKey,
               QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .toString();
}

void setLastFolder(
    const QString &folder)
{
    QSettings().setValue(LastFolderKey, folder);
}

QByteArray windowGeometry()
{
    return QSettings().value(WindowGeometryKey).toByteArray();
}

void setWindowGeometry(
    const QByteArray &geometry)
{
    QSettings().setValue(WindowGeometryKey, geometry);
}

Lighting::Options lighting()
{
    QSettings settings;

    settings.beginGroup(LightingGroup);

    const Lighting::Options defaults;
    Lighting::Options options;

    auto flag = [&settings](const char *key, bool fallback)
    {
        return settings.value(QLatin1String(key), fallback).toBool();
    };

    auto number = [&settings](const char *key, int fallback, int minimum, int maximum)
    {
        return std::clamp(settings.value(QLatin1String(key), fallback).toInt(), minimum, maximum);
    };

    options.keyLight = flag("key", defaults.keyLight);
    options.fillLight = flag("fill", defaults.fillLight);
    options.rimLight = flag("rim", defaults.rimLight);
    options.reflections = flag("reflections", defaults.reflections);
    options.followCamera = flag("followCamera", defaults.followCamera);
    options.knotShadows = flag("knotShadows", defaults.knotShadows);
    options.floorShadow = flag("floorShadow", defaults.floorShadow);
    options.contactShadow = flag("contactShadow", defaults.contactShadow);
    options.occlusion = flag("occlusion", defaults.occlusion);
    options.floorGrid = flag("floorGrid", defaults.floorGrid);
    options.brightness = number("brightness", defaults.brightness, 50, 150);
    options.shadowSoftness = number("shadowSoftness", defaults.shadowSoftness, 25, 400);
    options.shadowQuality = number("shadowQuality", defaults.shadowQuality, 0, 2);
    options.photoQuality = number("photoQuality", defaults.photoQuality, 0, 3);

    return options;
}

int photoAutoLevel()
{
    return std::clamp(QSettings().value(QStringLiteral("photoAutoLevel"), 1).toInt(), 1, 3);
}

void setPhotoAutoLevel(
    int level)
{
    QSettings().setValue(QStringLiteral("photoAutoLevel"), level);
}

void setLighting(
    const Lighting::Options &options)
{
    QSettings settings;

    settings.beginGroup(LightingGroup);

    settings.setValue(QStringLiteral("key"), options.keyLight);
    settings.setValue(QStringLiteral("fill"), options.fillLight);
    settings.setValue(QStringLiteral("rim"), options.rimLight);
    settings.setValue(QStringLiteral("reflections"), options.reflections);
    settings.setValue(QStringLiteral("followCamera"), options.followCamera);
    settings.setValue(QStringLiteral("knotShadows"), options.knotShadows);
    settings.setValue(QStringLiteral("floorShadow"), options.floorShadow);
    settings.setValue(QStringLiteral("contactShadow"), options.contactShadow);
    settings.setValue(QStringLiteral("occlusion"), options.occlusion);
    settings.setValue(QStringLiteral("floorGrid"), options.floorGrid);
    settings.setValue(QStringLiteral("brightness"), options.brightness);
    settings.setValue(QStringLiteral("shadowSoftness"), options.shadowSoftness);
    settings.setValue(QStringLiteral("shadowQuality"), options.shadowQuality);
    settings.setValue(QStringLiteral("photoQuality"), options.photoQuality);
}

void restoreDefaults()
{
    QSettings().clear();
}

void installLanguage(
    const QString &code)
{
    if (installedTranslator != nullptr)
    {
        QCoreApplication::removeTranslator(installedTranslator);

        delete installedTranslator;

        installedTranslator = nullptr;
    }

    if (code != QStringLiteral("sq"))
        return;

    // compiled into the program from KnotTheory_sq_AL.ts
    QTranslator *translator = new QTranslator();

    if (translator->load(QStringLiteral(":/i18n/KnotTheory_sq_AL.qm")))
    {
        QCoreApplication::installTranslator(translator);

        installedTranslator = translator;
    }
    else
    {
        delete translator;
    }
}

void applyAll()
{
    installLanguage(language());

    Theme::apply(darkTheme(), textScale());
}

void registerFileType()
{
#ifdef Q_OS_WIN
    const QString classes = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\");
    const QString progId = QStringLiteral("AKSIOMA.Knot");
    const QString program = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());

    // registry key -> its default value
    const QList<QPair<QString, QString>> entries =
    {
        {QStringLiteral(".aknot"), progId},
        // the "Type" column in Explorer
        {progId, QCoreApplication::translate("AppSettings", "AKSIOMA knot file")},
        // the first icon of the .exe (app.rc), the program's own icon
        {progId + QStringLiteral("\\DefaultIcon"), QStringLiteral("\"%1\",0").arg(program)},
        {progId + QStringLiteral("\\shell\\open\\command"), QStringLiteral("\"%1\" \"%2\"").arg(program, QStringLiteral("%1"))}
    };

    bool changed = false;

    for (const auto &entry : entries)
    {
        QSettings key(classes + entry.first, QSettings::NativeFormat);

        if (key.value(QStringLiteral("Default")).toString() != entry.second)
        {
            key.setValue(QStringLiteral("Default"), entry.second);
            changed = true;
        }
    }

    // Explorer shows the new icon straight away
    if (changed)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}

} // namespace AppSettings
