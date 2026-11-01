#include "appsettings.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>

#ifdef Q_OS_WIN
#include <QtGui/private/qguiapplication_p.h>
#endif

namespace
{

// Warnings go to a small file: there is no console in the installed program.
void logMessage(
    QtMsgType type,
    const QMessageLogContext &,
    const QString &message)
{
    if (type == QtDebugMsg || type == QtInfoMsg)
        return;

    QFile file(AppSettings::logPath());

    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;

    file.write(
        QDateTime::currentDateTime()
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss  "))
            .toUtf8() +
        message.toUtf8() + '\n');
}

} // namespace

int main(
    int argc,
    char *argv[])
{
    /*
     * The default surface format must be set BEFORE the QApplication is
     * created: it gives every OpenGL widget an OpenGL 3.3 core context
     * with 4x multisampling.
     */
    QSurfaceFormat surfaceFormat;
    surfaceFormat.setDepthBufferSize(24);
    surfaceFormat.setStencilBufferSize(8);
    surfaceFormat.setSamples(4);
    surfaceFormat.setVersion(3, 3);
    surfaceFormat.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(surfaceFormat);

    // (the names first: the settings are found by them. The choice of OpenGL
    // has to be made before the QApplication exists.)
    QCoreApplication::setOrganizationName(QStringLiteral("AKSIOMA"));
    QCoreApplication::setApplicationName(QStringLiteral("AKSIOMA Knots"));

    if (AppSettings::softwareGraphics())
        QCoreApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);

    QApplication app(
        argc,
        argv);

    /*
     * =========================================================
     * APPLICATION INFORMATION
     * =========================================================
     */

    app.setApplicationName(
        QStringLiteral(
            "AKSIOMA Knots"));

    app.setApplicationDisplayName(
        QStringLiteral(
            "AKSIOMA: Knots"));

    // from project() in CMakeLists.txt
    app.setApplicationVersion(
        QStringLiteral(
            AKSIOMA_VERSION));

    app.setOrganizationName(
        QStringLiteral(
            "AKSIOMA"));

    // title bar, taskbar and every dialog
    app.setWindowIcon(
        QIcon(
            QStringLiteral(
                ":/ui/icons/app_icon.png")));

#ifdef Q_OS_WIN
    /*
     * A full screen OpenGL window that covers the screen exactly is put
     * into a direct mode by Windows, and pop-ups (combo box lists, menus,
     * tooltips) then open behind it. A 1 px border avoids this (see "Full
     * screen OpenGL based windows" in Qt's Windows platform notes).
     */
    if (auto *windowsApp =
            app.nativeInterface<QNativeInterface::Private::QWindowsApplication>())
    {
        windowsApp->setHasBorderInFullScreenDefault(
            true);
    }
#endif

    /*
     * =========================================================
     * LANGUAGE AND THEME
     * =========================================================
     *
     * From the saved settings (Settings tab). The translator must be
     * installed BEFORE MainWindow is created, because MainWindow
     * translates its visible strings during construction.
     */

    AppSettings::applyAll();

    // a log that grew too long starts again
    if (QFile(AppSettings::logPath()).size() > 1024 * 1024)
        QFile::remove(AppSettings::logPath());

    qInstallMessageHandler(logMessage);

    // .aknot files: the program's icon, and a double click opens them here
    AppSettings::registerFileType();

    /*
     * =========================================================
     * MAIN WINDOW
     * =========================================================
     *
     * On the heap: a language change replaces the window with a new,
     * translated one (MainWindow::rebuildWindow).
     */

    MainWindow *window = new MainWindow();

    window->setAttribute(
        Qt::WA_DeleteOnClose);

    window->showInitially();

    // a knot file to open (double click in Explorer), once the window is up
    const QStringList arguments =
        app.arguments();

    if (arguments.size() > 1)
    {
        const QString path =
            arguments.at(1);

        QTimer::singleShot(
            0,
            window,
            [window, path]()
            {
                window->openKnotFilePath(
                    path);
            });
    }

    return app.exec();
}
