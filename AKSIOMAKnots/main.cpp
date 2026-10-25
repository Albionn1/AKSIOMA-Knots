#include "appsettings.h"
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>

#ifdef Q_OS_WIN
#include <QtGui/private/qguiapplication_p.h>
#endif

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

    app.setApplicationVersion(
        QStringLiteral(
            "1.0.0"));

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
