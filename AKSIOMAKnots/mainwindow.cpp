#include "mainwindow.h"

#include "aboutdialog.h"
#include "appsettings.h"
#include "commandpalette.h"
#include "controlhelp.h"
#include "diagrameditor.h"
#include "gpumemory.h"
#include "icons.h"
#include "knotbrowser.h"
#include "knotdiagram.h"
#include "knotexport.h"
#include "knottable.h"
#include "practicedialog.h"
#include "theme.h"
#include "timelineslider.h"
#include "unknotsummarydialog.h"

#include <QStandardItemModel>

#include <QApplication>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QUrl>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QColorDialog>
#include <QDialog>
#include <QEvent>
#include <QKeySequence>
#include <QShortcut>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QSplitter>
#include <QSpinBox>
#include <QSlider>
#include <QTextBrowser>
#include <QTextEdit>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace
{

// the example offered in an empty Custom Curve box: nothing worth keeping
const char *const ExampleCurve =
    "x=(2+cos(2*t))*cos(3*t);\n"
    "y=(2+cos(2*t))*sin(3*t);\n"
    "z=sin(4*t);";

// where the knot being worked on is kept, against a crash
QString autosavePath()
{
    const QString folder =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    QDir().mkpath(folder);

    return folder + QStringLiteral("/autosave.aknot");
}

// what a crash left behind, out of the way of the next autosave until the
// user has decided about it
QString recoveredPath()
{
    return QFileInfo(autosavePath()).absolutePath() + QStringLiteral("/recovered.aknot");
}

// A hint that makes way: all of its text or, in too little room, nothing.
class FittingLabel final
    : public QLabel
{
public:
    using QLabel::QLabel;

    QSize minimumSizeHint() const override
    {
        return QSize(0, 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (sizeHint().width() <= width())
            QLabel::paintEvent(event);
    }
};

} // namespace

MainWindow::MainWindow(
    QWidget *parent)
    : QMainWindow(parent)
{
    // Qt adds the display name to every window title unless the title is
    // the display name itself, so both are set: the main window shows only
    // "AKSIOMA: Knots" and dialogs "Settings - AKSIOMA: Knots". Set here and
    // not in main.cpp because a new language builds a new window.
    QGuiApplication::setApplicationDisplayName(
        tr("AKSIOMA: Knots"));

    setWindowTitle(
        QGuiApplication::applicationDisplayName());

    setMinimumSize(
        QSize(
            1000,
            650));

    QWidget *central = new QWidget(this);

    QVBoxLayout *rootLayout = new QVBoxLayout(central);

    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    /*
     * TOP BAR: brand | which knot is shown | Settings, About
     */

    QWidget *topBar = new QWidget(central);

    topBar->setObjectName(QStringLiteral("TopBar"));
    topBar->setAttribute(Qt::WA_StyledBackground, true);
    topBar->setFixedHeight(60);

    QHBoxLayout *topLayout = new QHBoxLayout(topBar);

    topLayout->setContentsMargins(24, 0, 20, 0);
    topLayout->setSpacing(14);

    QLabel *brandLabel = new QLabel(tr("AKSIOMA"), topBar);

    brandLabel->setObjectName(QStringLiteral("Brand"));

    QLabel *brandSub = new QLabel(tr("KNOTS"), topBar);

    brandSub->setObjectName(QStringLiteral("BrandSub"));

    QFrame *brandDivider = new QFrame(topBar);

    brandDivider->setObjectName(QStringLiteral("VRule"));
    brandDivider->setFixedSize(1, 28);

    QPushButton *settingsButton = new QPushButton(tr("Settings"), topBar);

    settingsButton->setObjectName(QStringLiteral("TopBarButton"));
    settingsButton->setCursor(Qt::PointingHandCursor);
    settingsButton->setToolTip(tr("Language, theme, text size, window  (Ctrl+,)"));

    QPushButton *fileButton = new QPushButton(tr("File"), topBar);

    fileButton->setObjectName(QStringLiteral("TopBarButton"));
    fileButton->setCursor(Qt::PointingHandCursor);
    fileButton->setToolTip(tr("Open and save knots, export pictures and data"));

    {
        QMenu *fileMenu = new QMenu(fileButton);

        fileMenu_ = fileMenu;

        auto addFileAction = [this, fileMenu](const QString &text,
                                              const QKeySequence &shortcut,
                                              void (MainWindow::*slot)())
        {
            QAction *action = fileMenu->addAction(text);

            action->setShortcut(shortcut);

            // the shortcut works while the menu is closed too
            addAction(action);

            connect(action, &QAction::triggered, this, slot);

            return action;
        };

        undoAction_ = addFileAction(tr("Undo"), QKeySequence::Undo, &MainWindow::undo);
        redoAction_ = addFileAction(tr("Redo"), QKeySequence::Redo, &MainWindow::redo);

        fileMenu->addSeparator();

        addFileAction(tr("Find anything..."), QKeySequence(Qt::CTRL | Qt::Key_K), &MainWindow::openCommandPalette);

        fileMenu->addSeparator();

        addFileAction(tr("Open knot..."), QKeySequence::Open, &MainWindow::openKnotFile);

        recentMenu_ = fileMenu->addMenu(tr("Open recent"));

        connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::fillRecentMenu);

        addFileAction(tr("Save knot..."), QKeySequence::Save, &MainWindow::saveKnotFile);

        fileMenu->addSeparator();

        addFileAction(tr("Export 3D picture..."),
                      QKeySequence(Qt::CTRL | Qt::Key_E),
                      &MainWindow::exportViewImage);
        addFileAction(tr("Export knot diagram..."),
                      QKeySequence(Qt::CTRL | Qt::Key_D),
                      &MainWindow::exportDiagram);
        addFileAction(tr("Export coordinates..."), QKeySequence(), &MainWindow::exportCoordinates);
        addFileAction(tr("Export analysis report..."), QKeySequence(), &MainWindow::exportReport);

        fileMenu->addSeparator();

        addFileAction(tr("Keyboard shortcuts"), QKeySequence(Qt::Key_F1), &MainWindow::showShortcuts);
        addFileAction(tr("Show the tour"), QKeySequence(), &MainWindow::startTour);

        // Opened by hand, not with setMenu(): a button with a menu keeps
        // room for a menu arrow and pushed its text out on the left.
        connect(fileButton,
                &QPushButton::clicked,
                fileMenu,
                [fileButton, fileMenu]()
                {
                    fileMenu->popup(fileButton->mapToGlobal(QPoint(0, fileButton->height())));
                });
    }

    undoButton_ = new QPushButton(topBar);

    undoButton_->setObjectName(QStringLiteral("TopBarButton"));
    undoButton_->setCursor(Qt::PointingHandCursor);
    ControlHelp::explainDisabled(undoButton_, tr("Undo  (Ctrl+Z)"), tr("Nothing to undo yet."));
    undoButton_->setAccessibleName(tr("Undo"));

    Icons::set(undoButton_, QStringLiteral("undo"));

    redoButton_ = new QPushButton(topBar);

    redoButton_->setObjectName(QStringLiteral("TopBarButton"));
    redoButton_->setCursor(Qt::PointingHandCursor);
    ControlHelp::explainDisabled(redoButton_, tr("Redo  (Ctrl+Y)"), tr("Nothing to redo."));
    redoButton_->setAccessibleName(tr("Redo"));

    Icons::set(redoButton_, QStringLiteral("redo"));

    connect(undoButton_, &QPushButton::clicked, this, &MainWindow::undo);
    connect(redoButton_, &QPushButton::clicked, this, &MainWindow::redo);

    // something to do, not a window setting: a button among the text links
    QPushButton *practiceButton = new QPushButton(tr("Practice"), topBar);

    practiceButton->setMinimumHeight(36);
    practiceButton->setCursor(Qt::PointingHandCursor);
    practiceButton->setToolTip(
        tr("Exercises on the knot diagram: tricolouring, crossing signs"));

    QPushButton *aboutButton = new QPushButton(tr("About"), topBar);

    aboutButton->setObjectName(QStringLiteral("TopBarButton"));
    aboutButton->setCursor(Qt::PointingHandCursor);

    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::showSettings);
    connect(aboutButton, &QPushButton::clicked, this, &MainWindow::showAbout);
    connect(practiceButton, &QPushButton::clicked, this, &MainWindow::openPractice);

    topLayout->addWidget(brandLabel);
    topLayout->addWidget(brandSub);
    topLayout->addSpacing(6);
    topLayout->addWidget(brandDivider);
    topLayout->addSpacing(6);
    topLayout->addWidget(createKnotBar(topBar), 1);
    topLayout->addWidget(undoButton_);
    topLayout->addWidget(redoButton_);
    topLayout->addSpacing(12);
    topLayout->addWidget(practiceButton);
    topLayout->addSpacing(8);
    topLayout->addWidget(fileButton);
    topLayout->addSpacing(4);
    topLayout->addWidget(settingsButton);
    topLayout->addSpacing(4);
    topLayout->addWidget(aboutButton);

    rootLayout->addWidget(topBar);

    /*
     * MAIN AREA: tools | tool panel | 3D view | analysis
     */

    QHBoxLayout *mainLayout = new QHBoxLayout();

    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainSplitter_ = new QSplitter(Qt::Horizontal, central);

    mainSplitter_->setChildrenCollapsible(false);
    mainSplitter_->setHandleWidth(1);

    QWidget *leftPanel = createLeftPanel();

    QWidget *rightPanel = createRightPanel();

    mainLayout->addWidget(createRail(central));
    mainLayout->addWidget(mainSplitter_, 1);

    // 3D view with a slim bar underneath
    QWidget *viewport = new QWidget(central);

    QVBoxLayout *viewportLayout = new QVBoxLayout(viewport);

    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(0);

    glWidget_ = new KnotGLWidget(viewport);

    glWidget_->setLighting(AppSettings::lighting());
    glWidget_->setPhotoAutoLevel(AppSettings::photoAutoLevel());

    viewportLayout->addWidget(glWidget_, 1);

    noticeBar_ = new QFrame(glWidget_);

    noticeBar_->setObjectName(QStringLiteral("Notice"));
    noticeBar_->hide();

    {
        QHBoxLayout *noticeLayout = new QHBoxLayout(noticeBar_);

        noticeLayout->setContentsMargins(14, 8, 8, 8);
        noticeLayout->setSpacing(12);

        noticeText_ = new QLabel(noticeBar_);

        noticeText_->setWordWrap(true);

        noticeProgress_ = new QProgressBar(noticeBar_);

        noticeProgress_->setRange(0, 100);
        noticeProgress_->setTextVisible(false);
        noticeProgress_->setFixedSize(110, 6);

        noticeAction_ = new QPushButton(noticeBar_);
        noticeAction_->setCursor(Qt::PointingHandCursor);

        noticeClose_ = new QPushButton(QStringLiteral("\u2715"), noticeBar_);

        noticeClose_->setObjectName(QStringLiteral("NoticeClose"));
        noticeClose_->setCursor(Qt::PointingHandCursor);
        noticeClose_->setToolTip(tr("Close"));
        noticeClose_->setAccessibleName(tr("Close"));

        noticeLayout->addWidget(noticeText_, 1);
        noticeLayout->addWidget(noticeProgress_);
        noticeLayout->addWidget(noticeAction_);
        noticeLayout->addWidget(noticeClose_);
    }

    noticeTimer_ = new QTimer(this);

    noticeTimer_->setSingleShot(true);

    connect(noticeTimer_, &QTimer::timeout, this, &MainWindow::hideNotice);

    connect(noticeClose_, &QPushButton::clicked, this, [this]()
    {
        const std::function<void()> dismissed = noticeDismissed_;

        hideNotice();

        if (dismissed)
            dismissed();
    });

    connect(noticeAction_, &QPushButton::clicked, this, [this]()
    {
        const std::function<void()> action = noticeDo_;

        hideNotice();

        if (action)
            action();
    });

    glWidget_->installEventFilter(this);

    layingTimer_ = new QTimer(this);

    layingTimer_->setInterval(100);

    connect(layingTimer_, &QTimer::timeout, this, &MainWindow::updateLayingProgress);

    // The unknotting plays like a film: its controls under the picture, so
    // that they stay at hand with the panels folded away.
    transportBar_ = new QWidget(viewport);

    transportBar_->setObjectName(QStringLiteral("ViewBar"));
    transportBar_->setAttribute(Qt::WA_StyledBackground, true);
    transportBar_->setFixedHeight(60);

    QHBoxLayout *transportLayout = new QHBoxLayout(transportBar_);

    transportLayout->setContentsMargins(16, 0, 16, 0);
    transportLayout->setSpacing(10);

    // "0.5×" says what it is; no label, the bar has to fit a small window
    unknotSpeedCombo_->setToolTip(tr("Speed"));

    transportLayout->addWidget(unknotPlayButton_);
    transportLayout->addWidget(unknotScrubSlider_, 1);
    transportLayout->addWidget(unknotSpeedCombo_);
    transportLayout->addWidget(unknotSummaryButton_);
    transportLayout->addWidget(unknotResetButton_);

    transportBar_->hide();

    viewportLayout->addWidget(transportBar_);

    QWidget *viewBar = new QWidget(viewport);

    viewBar->setObjectName(QStringLiteral("ViewBar"));
    viewBar->setAttribute(Qt::WA_StyledBackground, true);
    viewBar->setFixedHeight(48);

    QHBoxLayout *viewBarLayout = new QHBoxLayout(viewBar);

    viewBarLayout->setContentsMargins(24, 0, 24, 0);
    viewBarLayout->setSpacing(10);

    QLabel *navigationHint =
        new FittingLabel(
            tr("DRAG  ORBIT      RIGHT-DRAG  PAN      SCROLL  ZOOM"),
            viewBar);

    navigationHint->setObjectName(QStringLiteral("Caption"));
    navigationHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    resetCameraButton_ = new QPushButton(tr("Reset view"), viewBar);

    resetCameraButton_->setToolTip(
        tr("Turn to the clearest view of the knot and fit it in the window."));

    // Photo mode: the knot as a real rope lying on a real surface
    photoButton_ = new QPushButton(tr("Photo"), viewBar);

    // the same look as a chosen brush in Practice when it is on
    photoButton_->setObjectName(QStringLiteral("BrushButton"));
    photoButton_->setCheckable(true);

    photoButton_->setToolTip(
        tr("Show the knot as a real rope: it falls onto a table under its "
           "own weight and comes to rest, in a sunny place photographed "
           "with its real light."));

    photoSceneCombo_ = new QComboBox(viewBar);

    photoSceneCombo_->addItem(
        tr("Hemp rope on a garden table, in the sun"),
        static_cast<int>(Photo::Scene::LoungeTable));

    photoSceneCombo_->addItem(
        tr("Shoelace on boards in a meadow, in the sun"),
        static_cast<int>(Photo::Scene::BallroomFloor));

    photoSceneCombo_->addItem(
        tr("Tied on a sneaker"),
        static_cast<int>(Photo::Scene::Sneaker));

    photoSceneCombo_->setToolTip(tr("Where the knot is photographed."));
    photoSceneCombo_->setVisible(false);

    connect(
        photoButton_,
        &QPushButton::toggled,
        this,
        [this]() { applyPhotoMode(); });

    connect(
        photoSceneCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this]() { applyPhotoMode(); });

    viewBarLayout->addWidget(navigationHint, 1);
    viewBarLayout->addWidget(photoSceneCombo_);
    viewBarLayout->addWidget(photoButton_);
    viewBarLayout->addWidget(resetCameraButton_);

    // the analysis folded away, for more room around the knot
    analysisButton_ = new QPushButton(tr("Analysis", "view bar"), viewBar);

    analysisButton_->setCheckable(true);
    analysisButton_->setChecked(true);
    analysisButton_->setToolTip(tr("Show or hide the analysis panel."));

    connect(analysisButton_, &QPushButton::toggled, rightPanel, &QWidget::setVisible);

    viewBarLayout->addWidget(analysisButton_);

    viewportLayout->addWidget(viewBar);

    mainSplitter_->addWidget(leftPanel);
    mainSplitter_->addWidget(viewport);
    mainSplitter_->addWidget(rightPanel);

    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setStretchFactor(2, 0);

    // panel widths and first sizes for the text size
    fitPanelsToText();

    rootLayout->addLayout(mainLayout, 1);

    setCentralWidget(central);

    // Settings live in their own window, opened from the top bar.
    settingsDialog_ = new QDialog(this);

    settingsDialog_->setWindowTitle(tr("Settings"));
    settingsDialog_->resize(420, 640);

    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsDialog_);

    settingsLayout->setContentsMargins(0, 0, 0, 0);

    QTabWidget *settingsTabs = new QTabWidget(settingsDialog_);

    settingsTabs->setDocumentMode(true);
    settingsTabs->addTab(createSettingsPanel(), tr("GENERAL"));
    settingsTabs->addTab(createLightingPanel(), tr("LIGHTING"));

    settingsLayout->addWidget(settingsTabs);

    QShortcut *settingsShortcut =
        new QShortcut(
            QKeySequence(Qt::CTRL | Qt::Key_Comma),
            this);

    connect(
        settingsShortcut,
        &QShortcut::activated,
        this,
        &MainWindow::showSettings);

    /*
     * =========================================================
     * FULL SCREEN
     * =========================================================
     */

    QShortcut *fullScreenShortcut =
        new QShortcut(
            QKeySequence(Qt::Key_F11),
            this);

    connect(
        fullScreenShortcut,
        &QShortcut::activated,
        this,
        &MainWindow::toggleFullScreen);

    // Esc only means something while the window is full screen.
    leaveFullScreenShortcut_ =
        new QShortcut(
            QKeySequence(Qt::Key_Escape),
            this);

    leaveFullScreenShortcut_->setEnabled(
        false);

    connect(
        leaveFullScreenShortcut_,
        &QShortcut::activated,
        this,
        [this]()
        {
            if (isFullScreen())
                toggleFullScreen();
        });

    /*
     * =========================================================
     * CONNECTIONS
     * =========================================================
     */

    connect(
        generateButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::generateMesh);

    connect(
        torusPSpin_,
        &QSpinBox::valueChanged,
        this,
        [this](int)
        {
            scheduleUpdate(true);
        });

    connect(
        torusQSpin_,
        &QSpinBox::valueChanged,
        this,
        [this](int)
        {
            scheduleUpdate(true);
        });

    connect(
        resetCameraButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::resetCamera);

    connect(
        colorButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::chooseColor);

    connect(
        presetCombo_,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::presetChanged);

    connect(
        colorModeCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            updateLearnSection();
            rebuildMesh();
            noteChange();
        });

    connect(
        showCrossingNumbersCheck_,
        &QCheckBox::toggled,
        this,
        [this](bool enabled)
        {
            glWidget_->setShowCrossingNumbers(
                enabled);

            updateLearnSection();
        });

    connect(
        autoRotationCheck_,
        &QCheckBox::toggled,
        this,
        [this](bool enabled)
        {
            glWidget_->setAutoRotation(
                enabled);
        });

    connect(
        thicknessSlider_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            thicknessValueLabel_->setText(
                QStringLiteral(
                    "%1")
                    .arg(
                        static_cast<double>(
                            value) /
                            100.0,
                        0,
                        'f',
                        2));

            scheduleUpdate(false);
            noteChange();
        });

    connect(
        meshDensitySlider_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            meshDensityValueLabel_->setText(
                QString::number(
                    value));

            scheduleUpdate(true);
        });

    connect(
        metallicSlider_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            metallicValueLabel_->setText(
                QStringLiteral(
                    "%1")
                    .arg(
                        static_cast<double>(
                            value) /
                            100.0,
                        0,
                        'f',
                        2));

            applySurface();
            noteChange();
        });

    connect(
        browseKnotsButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::openKnotBrowser);

    connect(
        unknotStepCheck_,
        &QCheckBox::toggled,
        this,
        [this](bool)
        {
            setUnknotControlsEnabled(
                unknotPlaying_);
        });

    connect(
        materialCombo_,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        [this](int material)
        {
            // every material starts with a fitting shine
            static const int defaultShine[] = {70, 40, 65, 10};

            metallicSlider_->setValue(
                defaultShine[std::clamp(material, 0, 3)]);

            applySurface();
            noteChange();
        });

    /*
     * =========================================================
     * INITIAL STATE
     * =========================================================
     */

    // Slider changes are applied after a short pause so that dragging a
    // slider does not rebuild the geometry on every pixel.
    rebuildTimer_ = new QTimer(this);
    rebuildTimer_->setSingleShot(true);
    rebuildTimer_->setInterval(150);

    historyTimer_ = new QTimer(this);
    historyTimer_->setSingleShot(true);
    historyTimer_->setInterval(350);

    connect(historyTimer_, &QTimer::timeout, this, &MainWindow::recordState);

    connect(
        rebuildTimer_,
        &QTimer::timeout,
        this,
        [this]()
        {
            const bool full = pendingFullRegenerate_;
            pendingFullRegenerate_ = false;

            if (full)
                generateMesh();
            else
                rebuildMesh();
        });

    // The tools start and stop in many places; a look a few times a second
    // keeps the left edge true without touching all of them.
    QTimer *railTimer = new QTimer(this);

    railTimer->setInterval(200);

    connect(railTimer, &QTimer::timeout, this, &MainWindow::updateRailActivity);

    railTimer->start();

    unknotTimer_ =
        new QTimer(
            this);

    unknotTimer_->setInterval(
        16);

    // The default coarse timer ticks unevenly on Windows (about 15.6 ms
    // steps), which makes the motion stutter.
    unknotTimer_->setTimerType(
        Qt::PreciseTimer);

    connect(
        unknotTimer_,
        &QTimer::timeout,
        this,
        &MainWindow::unknotTick);

    relaxTimer_ =
        new QTimer(
            this);

    relaxTimer_->setInterval(
        16);

    relaxTimer_->setTimerType(
        Qt::PreciseTimer);

    connect(
        relaxTimer_,
        &QTimer::timeout,
        this,
        &MainWindow::relaxTick);

    photoRopeTimer_ = new QTimer(this);

    photoRopeTimer_->setInterval(16);
    photoRopeTimer_->setTimerType(Qt::PreciseTimer);

    connect(photoRopeTimer_, &QTimer::timeout, this, &MainWindow::photoRopeTick);

    // A moving rope keeps pace with the screen: as soon as a frame is
    // shown, the next shape (worked out meanwhile) is taken and the one
    // after started. The timer only picks up a shape that was late.
    connect(
        glWidget_,
        &QOpenGLWidget::frameSwapped,
        this,
        [this]()
        {
            if (photoRopeTimer_->isActive())
                photoRopeTick();
        });

    connect(cutRopeButton_, &QPushButton::clicked, this, &MainWindow::cutPhotoRope);
    connect(tieEndsButton_, &QPushButton::clicked, this, &MainWindow::tiePhotoRopeEnds);

    connect(
        relaxPlayButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::relaxPlayPause);

    connect(
        scrambleButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::relaxScramble);

    connect(
        relaxResetButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::relaxReset);

    connect(
        grabButton_,
        &QPushButton::toggled,
        this,
        &MainWindow::grabToggled);

    connect(
        glWidget_,
        &KnotGLWidget::ropeGrabbed,
        this,
        &MainWindow::ropeGrabbed);

    connect(
        glWidget_,
        &KnotGLWidget::ropeDragged,
        this,
        &MainWindow::ropeDragged);

    connect(
        glWidget_,
        &KnotGLWidget::ropeReleased,
        this,
        &MainWindow::ropeReleased);

    connect(
        unknotPlayButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::unknotPlayPause);

    connect(
        unknotResetButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::unknotReset);

    connect(
        unknotSummaryButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::showUnknotSummary);

    connect(
        unknotScrubSlider_,
        &QSlider::valueChanged,
        this,
        &MainWindow::unknotScrubbed);

    glWidget_->setMeshColor(
        meshColor_);

    updateColorSwatch();

    applySurface();

    presetCombo_->setCurrentIndex(
        presetCombo_->findData(
            static_cast<int>(
                KnotMath::Preset::Trefoil)));

    updateCustomInputVisibility();

    updateLearnSection();

    generateMesh();

    // what cannot be used yet says why
    ControlHelp::explainDisabled(
        relaxResetButton_,
        QString(),
        tr("Nothing to go back to: the knot has not been moved."));

    ControlHelp::explainDisabled(
        unknotResetButton_,
        QString(),
        tr("Start the unknotting first."));

    ControlHelp::explainDisabled(
        unknotSummaryButton_,
        unknotSummaryButton_->toolTip(),
        tr("Start the unknotting first: its steps are listed here."));

    ControlHelp::explainDisabled(
        unknotScrubSlider_,
        QString(),
        tr("Start the unknotting to move along it."));

    ControlHelp::explainDisabled(
        seifertStepSlider_,
        seifertStepSlider_->toolTip(),
        tr("Build the surface first."));

    ControlHelp::explainDisabled(
        cutRopeButton_,
        cutRopeButton_->toolTip(),
        tr("Let go of the rope first. A rope already cut has its two ends."));

    ControlHelp::explainDisabled(
        tieEndsButton_,
        tieEndsButton_->toolTip(),
        tr("Cut the rope first, and let go of it."));

    setAcceptDrops(true);

    /*
     * The first-run tour: a card that stands next to what it talks about.
     */
    tourCard_ = new QFrame(central);

    tourCard_->setObjectName(QStringLiteral("Tour"));
    tourCard_->setFixedWidth(330);
    tourCard_->hide();

    {
        QVBoxLayout *tourLayout = new QVBoxLayout(tourCard_);

        tourLayout->setContentsMargins(16, 14, 16, 14);
        tourLayout->setSpacing(8);

        tourCount_ = new QLabel(tourCard_);
        tourCount_->setObjectName(QStringLiteral("Caption"));

        tourTitle_ = new QLabel(tourCard_);
        tourTitle_->setObjectName(QStringLiteral("Status"));

        QFont bold = tourTitle_->font();

        bold.setBold(true);

        tourTitle_->setFont(bold);

        tourText_ = new QLabel(tourCard_);
        tourText_->setWordWrap(true);
        tourText_->setObjectName(QStringLiteral("Subtitle"));

        tourNext_ = new QPushButton(tourCard_);
        tourNext_->setObjectName(QStringLiteral("GenerateButton"));
        tourNext_->setMinimumHeight(34);

        tourSkip_ = new QPushButton(tr("Skip"), tourCard_);
        tourSkip_->setMinimumHeight(34);

        QHBoxLayout *tourButtons = new QHBoxLayout();

        tourButtons->setSpacing(8);
        tourButtons->addWidget(tourSkip_);
        tourButtons->addStretch(1);
        tourButtons->addWidget(tourNext_);

        tourLayout->addWidget(tourCount_);
        tourLayout->addWidget(tourTitle_);
        tourLayout->addWidget(tourText_);
        tourLayout->addLayout(tourButtons);

        connect(tourSkip_, &QPushButton::clicked, this, &MainWindow::endTour);

        connect(tourNext_, &QPushButton::clicked, this, [this]()
        {
            ++tourStep_;

            showTourStep();
        });
    }

    central->installEventFilter(this);

    // for Space, the arrows and ? wherever the keyboard is
    qApp->installEventFilter(this);

    // the keyboard starts on the view, not on the first button
    glWidget_->setFocus();

    // The state everything else can be undone back to. (Not by recordState:
    // an autosave of the last session is still on the disk, to be offered.)
    historyTimer_->stop();

    history_.record(currentState());

    history_.markSaved();

    updateUndoControls();

    restoreWorkspace();
}

namespace
{

// "01  KNOT ------------" section header
void addSectionHeader(
    QVBoxLayout *layout,
    QWidget *parent,
    const QString &index,
    const QString &title)
{
    QWidget *row = new QWidget(parent);

    QHBoxLayout *rowLayout = new QHBoxLayout(row);

    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);

    // no index: just the title and the rule
    if (!index.isEmpty())
    {
        QLabel *indexLabel = new QLabel(index, row);

        indexLabel->setObjectName(QStringLiteral("SectionIndex"));

        rowLayout->addWidget(indexLabel);
    }

    QLabel *titleLabel = new QLabel(title, row);

    titleLabel->setObjectName(QStringLiteral("Section"));

    QFrame *rule = new QFrame(row);

    rule->setObjectName(QStringLiteral("Rule"));
    rule->setFixedHeight(1);
    rule->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    rowLayout->addWidget(titleLabel);
    rowLayout->addWidget(rule, 1, Qt::AlignVCenter);

    layout->addWidget(row);
}

// "Caption .......... value" over a slider
void addSliderRow(
    QVBoxLayout *layout,
    QWidget *parent,
    const QString &caption,
    QSlider *&slider,
    QLabel *&valueLabel,
    int minimum,
    int maximum,
    int value,
    const QString &valueText,
    double unit)
{
    QWidget *block = new QWidget(parent);

    QVBoxLayout *blockLayout = new QVBoxLayout(block);

    blockLayout->setContentsMargins(0, 0, 0, 0);
    blockLayout->setSpacing(6);

    QHBoxLayout *captionRow = new QHBoxLayout();

    captionRow->setContentsMargins(0, 0, 0, 0);

    QLabel *captionLabel = new QLabel(caption, block);

    captionLabel->setObjectName(QStringLiteral("Field"));

    slider = new QSlider(Qt::Horizontal, block);

    slider->setRange(minimum, maximum);
    slider->setValue(value);

    ControlHelp::resetOnDoubleClick(slider, value);

    valueLabel = new SliderValue(valueText, slider, unit, block);

    valueLabel->setObjectName(QStringLiteral("Value"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    captionRow->addWidget(captionLabel);
    captionRow->addStretch(1);
    captionRow->addWidget(valueLabel);

    blockLayout->addLayout(captionRow);
    blockLayout->addWidget(slider);

    layout->addWidget(block);
}

QLabel *makeFieldLabel(
    const QString &text,
    QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);

    label->setObjectName(QStringLiteral("Field"));

    return label;
}

} // namespace

QWidget *MainWindow::createKnotBar(
    QWidget *parent)
{
    QWidget *bar = new QWidget(parent);

    QHBoxLayout *layout = new QHBoxLayout(bar);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    QLabel *caption = new QLabel(tr("KNOT"), bar);

    caption->setObjectName(QStringLiteral("Caption"));

    layout->addWidget(caption);

    presetCombo_ = new QComboBox(bar);

    presetCombo_->setMinimumHeight(36);
    presetCombo_->setMinimumWidth(230);
    presetCombo_->setMaximumWidth(320);
    presetCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    presetCombo_->addItem(tr("Unknot"), static_cast<int>(KnotMath::Preset::Unknot));
    presetCombo_->addItem(tr("Trefoil Knot (3₁)"), static_cast<int>(KnotMath::Preset::Trefoil));
    presetCombo_->addItem(tr("Figure-Eight Knot (4₁)"), static_cast<int>(KnotMath::Preset::FigureEight));
    presetCombo_->addItem(tr("Cinquefoil (5₁)"), static_cast<int>(KnotMath::Preset::Cinquefoil));
    presetCombo_->addItem(tr("Stevedore Knot (6₁)"), static_cast<int>(KnotMath::Preset::Stevedore));
    presetCombo_->addItem(tr("DNA Double Helix (Multi-strand)"), static_cast<int>(KnotMath::Preset::DNAHelix));
    presetCombo_->addItem(tr("Celtic Armor (Multi-strand)"), static_cast<int>(KnotMath::Preset::CelticKnot));
    presetCombo_->addItem(tr("Custom Torus T(p,q)"), static_cast<int>(KnotMath::Preset::CustomTorus));
    presetCombo_->addItem(tr("Custom Curve"), static_cast<int>(KnotMath::Preset::CustomCoordinates));
    presetCombo_->addItem(tr("Knot Table (12,965 knots)"), static_cast<int>(KnotMath::Preset::KnotTable));

    layout->addWidget(presetCombo_, 1);

    // torus parameters (only for T(p,q)), right next to the choice
    torusRow_ = new QWidget(bar);

    QHBoxLayout *torusLayout = new QHBoxLayout(torusRow_);

    torusLayout->setContentsMargins(0, 0, 0, 0);
    torusLayout->setSpacing(6);

    torusPSpin_ = new QSpinBox(torusRow_);
    torusQSpin_ = new QSpinBox(torusRow_);

    torusPSpin_->setRange(1, 20);
    torusQSpin_->setRange(1, 20);
    torusPSpin_->setValue(3);
    torusQSpin_->setValue(2);

    for (QSpinBox *spin : {torusPSpin_, torusQSpin_})
    {
        spin->setMinimumHeight(36);
        spin->setFixedWidth(68);
    }

    torusLayout->addWidget(makeFieldLabel(tr("p"), torusRow_));
    torusLayout->addWidget(torusPSpin_);
    torusLayout->addSpacing(6);
    torusLayout->addWidget(makeFieldLabel(tr("q"), torusRow_));
    torusLayout->addWidget(torusQSpin_);

    layout->addWidget(torusRow_);

    layout->addSpacing(4);

    browseKnotsButton_ = new QPushButton(tr("Browse..."), bar);

    browseKnotsButton_->setMinimumHeight(36);
    browseKnotsButton_->setToolTip(
        tr("All 12,965 prime knots with up to 13 crossings."));

    QPushButton *drawKnotButton = new QPushButton(tr("Draw..."), bar);

    drawKnotButton->setMinimumHeight(36);
    drawKnotButton->setToolTip(
        tr("Draw a loop with the mouse and choose over/under at each crossing."));

    connect(drawKnotButton, &QPushButton::clicked,
            this, &MainWindow::drawKnotDialog);

    layout->addWidget(browseKnotsButton_);
    layout->addWidget(drawKnotButton);

    layout->addStretch(1);

    return bar;
}

QWidget *MainWindow::createRail(
    QWidget *parent)
{
    QWidget *rail = new QWidget(parent);

    railWidget_ = rail;

    rail->setObjectName(QStringLiteral("Rail"));
    rail->setAttribute(Qt::WA_StyledBackground, true);
    // wider with larger text, through the buttons
    rail->setMinimumWidth(84);
    rail->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    QVBoxLayout *layout = new QVBoxLayout(rail);

    layout->setContentsMargins(0, 8, 1, 8);
    layout->setSpacing(2);

    railGroup_ = new QButtonGroup(this);

    const struct
    {
        QString text;
        QString icon;
    }
    tools[] =
    {
        {tr("Style"), QStringLiteral("rail_style")},
        {tr("Relax"), QStringLiteral("rail_relax")},
        {tr("Unknot", "rail"), QStringLiteral("rail_unknot")},
        {tr("Join"), QStringLiteral("rail_join")},
        {tr("Surface"), QStringLiteral("rail_surface")}
    };

    int page = 0;

    for (const auto &tool : tools)
    {
        QToolButton *button = new QToolButton(rail);

        button->setObjectName(QStringLiteral("RailButton"));
        button->setText(tool.text);
        button->setCheckable(true);
        button->setChecked(page == 0);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(
            QStringLiteral("%1  (Ctrl+%2)\n").arg(tool.text).arg(page + 1) +
            tr("Click the open tool again to fold its panel away."));

        Icons::set(button, tool.icon);

        railGroup_->addButton(button, page);

        layout->addWidget(button);

        QShortcut *shortcut =
            new QShortcut(
                QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + page)),
                this);

        connect(shortcut, &QShortcut::activated, button, &QToolButton::click);

        page++;
    }

    layout->addStretch(1);

    // idClicked, not idToggled: the open tool clicked again folds its panel
    connect(railGroup_, &QButtonGroup::idClicked, this, &MainWindow::railClicked);

    return rail;
}

void MainWindow::railClicked(
    int page)
{
    const bool fold =
        page == leftPages_->currentIndex() && !leftPages_->isHidden();

    leftPages_->setCurrentIndex(page);
    leftPages_->setVisible(!fold);

    updateRailActivity();
}

void MainWindow::updateRailActivity()
{
    const bool running[] =
    {
        false,
        relaxRunning_ || pulling_,
        unknotActive_,
        joinActive_,
        seifertActive_
    };

    for (int page = 0; page < 5; page++)
    {
        QAbstractButton *button = railGroup_->button(page);

        if (button->property("running").toBool() == running[page])
            continue;

        button->setProperty("running", running[page]);

        // the style sheet looks at the property again
        button->style()->unpolish(button);
        button->style()->polish(button);
    }

    transportBar_->setVisible(
        leftPages_->currentIndex() == 2 || unknotActive_);
}

QWidget *MainWindow::createLeftPanel()
{
    QStackedWidget *pages = new QStackedWidget(this);

    leftPages_ = pages;

    pages->setMinimumWidth(280);
    pages->setMaximumWidth(380);

    // one scrolling page per tool
    auto makePage = [this, pages](QVBoxLayout *&layout) -> QScrollArea *
    {
        QScrollArea *scroll = new QScrollArea(pages);

        scroll->setObjectName(QStringLiteral("PanelScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        QWidget *panel = new QWidget(scroll);

        panel->setObjectName(QStringLiteral("Panel"));
        panel->setAttribute(Qt::WA_StyledBackground, true);

        layout = new QVBoxLayout(panel);

        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(14);

        scroll->setWidget(panel);

        return scroll;
    };

    /*
     * STYLE
     */

    QVBoxLayout *layout = nullptr;

    QScrollArea *stylePage = makePage(layout);

    QWidget *panel = stylePage->widget();

    // custom curve (only for Custom Curve)
    customBox_ = new QWidget(panel);

    QVBoxLayout *customLayout = new QVBoxLayout(customBox_);

    customLayout->setContentsMargins(0, 0, 0, 10);
    customLayout->setSpacing(10);

    addSectionHeader(customLayout, customBox_, QString(), tr("CURVE"));

    customInputEdit_ = new QTextEdit(customBox_);

    customInputEdit_->setAcceptRichText(false);
    customInputEdit_->setFixedHeight(110);
    customInputEdit_->setPlaceholderText(
        QStringLiteral(
            "x=(2+cos(2*t))*cos(3*t);\n"
            "y=(2+cos(2*t))*sin(3*t);\n"
            "z=sin(4*t);"));

    customLayout->addWidget(customInputEdit_);

    QLabel *syntaxLabel =
        new QLabel(
            tr("Formulas x=..., y=..., z=... in t, or one 'x, y, z' point per line."),
            customBox_);

    syntaxLabel->setObjectName(QStringLiteral("Subtitle"));
    syntaxLabel->setWordWrap(true);

    customLayout->addWidget(syntaxLabel);

    generateButton_ = new QPushButton(tr("APPLY"), customBox_);

    generateButton_->setObjectName(QStringLiteral("GenerateButton"));
    generateButton_->setMinimumHeight(38);

    customLayout->addWidget(generateButton_);

    layout->addWidget(customBox_);

    addSectionHeader(layout, panel, QString(), tr("TUBE"));

    addSliderRow(layout, panel, tr("Tube thickness"),
                 thicknessSlider_, thicknessValueLabel_,
                 2, 60, 16, QStringLiteral("0.16"), 0.01);

    layout->addWidget(makeFieldLabel(tr("Material"), panel));

    materialCombo_ = new QComboBox(panel);

    materialCombo_->setMinimumHeight(36);
    materialCombo_->addItem(tr("Glossy"));
    materialCombo_->addItem(tr("Satin"));
    materialCombo_->addItem(tr("Metal"));
    materialCombo_->addItem(tr("Rope"));

    layout->addWidget(materialCombo_);

    layout->addWidget(makeFieldLabel(tr("Color mode"), panel));

    QHBoxLayout *colorRow = new QHBoxLayout();

    colorRow->setSpacing(8);

    colorModeCombo_ = new QComboBox(panel);

    colorModeCombo_->setMinimumHeight(36);
    colorModeCombo_->addItem(tr("Single Color"));
    colorModeCombo_->addItem(tr("Crossing Colors"));
    colorModeCombo_->addItem(tr("Strand Colors"));
    colorModeCombo_->setCurrentIndex(
        static_cast<int>(KnotMath::ColorMode::SingleColor));

    // a swatch of the current tube colour; click to change it
    colorButton_ = new QPushButton(panel);

    colorButton_->setFixedSize(36, 36);
    colorButton_->setCursor(Qt::PointingHandCursor);
    colorButton_->setToolTip(tr("Tube color"));

    colorRow->addWidget(colorModeCombo_, 1);
    colorRow->addWidget(colorButton_);

    layout->addLayout(colorRow);

    layout->addSpacing(10);

    addSectionHeader(layout, panel, QString(), tr("VIEW"));

    showCrossingNumbersCheck_ = new QCheckBox(tr("Show crossing numbers"), panel);
    autoRotationCheck_ = new QCheckBox(tr("Auto-rotate"), panel);

    showCrossingNumbersCheck_->setChecked(false);
    autoRotationCheck_->setChecked(false);

    layout->addWidget(showCrossingNumbersCheck_);
    layout->addWidget(autoRotationCheck_);

    layout->addSpacing(6);

    // fine tuning most people never touch, folded away by default
    QPushButton *moreOptionsButton = new QPushButton(panel);

    moreOptionsButton->setObjectName(QStringLiteral("DisclosureButton"));
    moreOptionsButton->setCheckable(true);
    moreOptionsButton->setCursor(Qt::PointingHandCursor);

    QWidget *moreOptions = new QWidget(panel);

    QVBoxLayout *moreLayout = new QVBoxLayout(moreOptions);

    moreLayout->setContentsMargins(0, 0, 0, 0);
    moreLayout->setSpacing(14);

    addSliderRow(moreLayout, moreOptions, tr("Curve density"),
                 meshDensitySlider_, meshDensityValueLabel_,
                 48, 320, 160, QStringLiteral("160"), 1.0);

    addSliderRow(moreLayout, moreOptions, tr("Material shine"),
                 metallicSlider_, metallicValueLabel_,
                 0, 100, 70, QStringLiteral("0.70"), 0.01);

    auto showMoreOptions = [moreOptionsButton, moreOptions](bool shown)
    {
        moreOptions->setVisible(shown);

        moreOptionsButton->setText(
            shown ? tr("Fewer options") : tr("More options"));

        Icons::set(moreOptionsButton, shown ? QStringLiteral("chevron_down") : QStringLiteral("chevron_right"));
    };

    showMoreOptions(false);

    connect(moreOptionsButton, &QPushButton::toggled, this, showMoreOptions);

    layout->addWidget(moreOptionsButton, 0, Qt::AlignLeft);
    layout->addWidget(moreOptions);

    layout->addStretch(1);

    pages->addWidget(stylePage);

    /*
     * RELAX: the same knot in another shape
     */

    QScrollArea *relaxPage = makePage(layout);

    panel = relaxPage->widget();

    addSectionHeader(layout, panel, QString(), tr("RELAX"));

    QWidget *relaxGroup = new QWidget(panel);

    QVBoxLayout *relaxLayout = new QVBoxLayout(relaxGroup);

    relaxLayout->setContentsMargins(0, 0, 0, 0);
    relaxLayout->setSpacing(12);

    QLabel *relaxIntro =
        new QLabel(
            tr("Let the knot settle into its natural shape, or tangle it "
               "up first. Strands never pass through each other, so it "
               "stays the same knot."),
            relaxGroup);

    relaxIntro->setObjectName(QStringLiteral("Subtitle"));
    relaxIntro->setWordWrap(true);

    relaxLayout->addWidget(relaxIntro);

    QHBoxLayout *relaxButtons = new QHBoxLayout();

    relaxButtons->setSpacing(8);

    relaxPlayButton_ = new QPushButton(tr("RELAX", "button"), relaxGroup);

    Icons::set(relaxPlayButton_, QStringLiteral("play"));

    relaxPlayButton_->setObjectName(QStringLiteral("GenerateButton"));
    relaxPlayButton_->setMinimumHeight(38);

    scrambleButton_ = new QPushButton(tr("Scramble"), relaxGroup);

    scrambleButton_->setMinimumHeight(38);

    relaxResetButton_ = new QPushButton(tr("Reset"), relaxGroup);

    relaxResetButton_->setMinimumHeight(38);
    relaxResetButton_->setEnabled(false);

    relaxButtons->addWidget(relaxPlayButton_, 3);
    relaxButtons->addWidget(scrambleButton_, 2);
    relaxButtons->addWidget(relaxResetButton_, 2);

    relaxLayout->addLayout(relaxButtons);

    grabButton_ = new QPushButton(tr("Grab the rope"), relaxGroup);

    Icons::set(grabButton_, QStringLiteral("hand"));

    // the same look as a chosen brush in Practice when it is on
    grabButton_->setObjectName(QStringLiteral("BrushButton"));
    grabButton_->setCheckable(true);
    grabButton_->setMinimumHeight(38);
    grabButton_->setToolTip(
        tr("Pull the knot about with the mouse: drag the rope with the "
           "left button."));

    relaxLayout->addWidget(grabButton_);

    // Photo mode: the rope on the table cut open, and its ends tied
    QHBoxLayout *ropeButtons = new QHBoxLayout();

    ropeButtons->setSpacing(8);

    cutRopeButton_ = new QPushButton(tr("Cut the rope"), relaxGroup);

    Icons::set(cutRopeButton_, QStringLiteral("scissors"));

    cutRopeButton_->setMinimumHeight(38);
    cutRopeButton_->setToolTip(
        tr("Cut the rope on the table open: with two free ends it can be "
           "tied into another knot."));

    tieEndsButton_ = new QPushButton(tr("Tie the ends"), relaxGroup);

    Icons::set(tieEndsButton_, QStringLiteral("tie"));

    tieEndsButton_->setMinimumHeight(38);
    tieEndsButton_->setToolTip(
        tr("Join the two ends of the rope: the knot tied in it becomes "
           "the knot shown and studied."));

    ropeButtons->addWidget(cutRopeButton_, 1);
    ropeButtons->addWidget(tieEndsButton_, 1);

    relaxLayout->addLayout(ropeButtons);

    cutRopeButton_->setVisible(false);
    tieEndsButton_->setVisible(false);

    relaxStatusLabel_ = new QLabel(QString(), relaxGroup);

    relaxStatusLabel_->setObjectName(QStringLiteral("Status"));
    relaxStatusLabel_->setWordWrap(true);
    relaxStatusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    relaxLayout->addWidget(relaxStatusLabel_);

    layout->addWidget(relaxGroup);

    layout->addStretch(1);

    pages->addWidget(relaxPage);

    /*
     * UNKNOT: crossing changes (the playback is under the 3D view)
     */

    QScrollArea *unknotPage = makePage(layout);

    panel = unknotPage->widget();

    addSectionHeader(layout, panel, QString(), tr("UNKNOT"));

    unknotGroup_ = new QWidget(panel);

    QVBoxLayout *unknotLayout = new QVBoxLayout(unknotGroup_);

    unknotLayout->setContentsMargins(0, 0, 0, 0);
    unknotLayout->setSpacing(12);

    QLabel *unknotIntro =
        new QLabel(
            tr("Watch the knot being undone by passing strands "
               "through each other."),
            unknotGroup_);

    unknotIntro->setObjectName(QStringLiteral("Subtitle"));
    unknotIntro->setWordWrap(true);

    unknotLayout->addWidget(unknotIntro);

    unknotPlayButton_ = new QPushButton(tr("UNKNOT", "button"), unknotGroup_);

    Icons::set(unknotPlayButton_, QStringLiteral("play"));

    unknotPlayButton_->setObjectName(QStringLiteral("GenerateButton"));
    unknotPlayButton_->setMinimumHeight(38);
    unknotPlayButton_->setMinimumWidth(120);

    unknotResetButton_ = new QPushButton(tr("Reset"), unknotGroup_);

    unknotResetButton_->setMinimumHeight(38);
    unknotResetButton_->setEnabled(false);

    unknotSummaryButton_ = new QPushButton(tr("Steps"), unknotGroup_);

    unknotSummaryButton_->setMinimumHeight(38);
    unknotSummaryButton_->setEnabled(false);
    unknotSummaryButton_->setToolTip(
        tr("Every crossing change of the animation, one by one."));

    unknotScrubSlider_ = new TimelineSlider(unknotGroup_);

    unknotScrubSlider_->setRange(0, 1000);
    unknotScrubSlider_->setEnabled(false);

    unknotSpeedCombo_ = new QComboBox(unknotGroup_);

    unknotSpeedCombo_->setMinimumHeight(36);
    unknotSpeedCombo_->addItem(QStringLiteral("0.25×"), 0.25);
    unknotSpeedCombo_->addItem(QStringLiteral("0.5×"), 0.5);
    unknotSpeedCombo_->addItem(QStringLiteral("1×"), 1.0);
    unknotSpeedCombo_->addItem(QStringLiteral("2×"), 2.0);
    unknotSpeedCombo_->addItem(QStringLiteral("4×"), 4.0);
    // slowly enough to follow every step
    unknotSpeedCombo_->setCurrentIndex(1);

    unknotStepCheck_ =
        new QCheckBox(tr("Step by step (pause at every stage)"), unknotGroup_);

    unknotStepCheck_->setChecked(false);

    unknotLayout->addWidget(unknotStepCheck_);

    unknotStatusLabel_ = new QLabel(QString(), unknotGroup_);

    unknotStatusLabel_->setObjectName(QStringLiteral("Status"));
    unknotStatusLabel_->setWordWrap(true);
    unknotStatusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    unknotLayout->addWidget(unknotStatusLabel_);

    layout->addWidget(unknotGroup_);

    layout->addStretch(1);

    pages->addWidget(unknotPage);

    /*
     * JOIN: two knots become their connected sum
     */

    QScrollArea *joinPage = makePage(layout);

    panel = joinPage->widget();

    addSectionHeader(layout, panel, QString(), tr("JOIN TWO KNOTS"));

    QLabel *joinIntro =
        new QLabel(
            tr("Cut a small arc out of the current knot and out of a second "
               "knot, and join the four loose ends with a band: the two "
               "become one knot, their connected sum K₁ # K₂."),
            panel);

    joinIntro->setObjectName(QStringLiteral("Subtitle"));
    joinIntro->setWordWrap(true);

    layout->addWidget(joinIntro);

    layout->addWidget(makeFieldLabel(tr("Second knot"), panel));

    joinSecondCombo_ = new QComboBox(panel);

    joinSecondCombo_->setMinimumHeight(36);
    joinSecondCombo_->addItem(tr("Trefoil Knot (3₁)"), QStringLiteral("trefoil"));
    joinSecondCombo_->addItem(tr("Figure-Eight Knot (4₁)"), QStringLiteral("figure8"));
    joinSecondCombo_->addItem(tr("Cinquefoil (5₁)"), QStringLiteral("cinquefoil"));
    joinSecondCombo_->addItem(tr("Stevedore Knot (6₁)"), QStringLiteral("stevedore"));
    joinSecondCombo_->addItem(tr("Same as the current knot"), QStringLiteral("same"));
    joinSecondCombo_->addItem(tr("From the knot table..."), QStringLiteral("table"));

    layout->addWidget(joinSecondCombo_);

    joinMirrorCheck_ = new QCheckBox(tr("Mirror image of the second knot"), panel);

    joinMirrorCheck_->setToolTip(
        tr("Two trefoils of the same handedness give the granny knot; with "
           "the mirror image they give the square knot."));

    layout->addWidget(joinMirrorCheck_);

    joinButton_ = new QPushButton(tr("JOIN"), panel);

    Icons::set(joinButton_, QStringLiteral("play"));

    joinButton_->setObjectName(QStringLiteral("GenerateButton"));
    joinButton_->setMinimumHeight(38);

    layout->addWidget(joinButton_);

    joinStatusLabel_ = new QLabel(QString(), panel);

    joinStatusLabel_->setObjectName(QStringLiteral("Status"));
    joinStatusLabel_->setWordWrap(true);
    joinStatusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    layout->addWidget(joinStatusLabel_);

    joinTimer_ = new QTimer(this);

    joinTimer_->setTimerType(Qt::PreciseTimer);
    joinTimer_->setInterval(16);

    connect(joinButton_, &QPushButton::clicked, this, &MainWindow::joinPlayStop);
    connect(joinTimer_, &QTimer::timeout, this, &MainWindow::joinTick);

    layout->addStretch(1);

    pages->addWidget(joinPage);

    /*
     * SURFACE: the Seifert surface of the knot
     */

    QScrollArea *surfacePage = makePage(layout);

    panel = surfacePage->widget();

    addSectionHeader(layout, panel, QString(), tr("SEIFERT SURFACE"));

    QLabel *surfaceIntro =
        new QLabel(
            tr("Every knot is the edge of a surface with two sides, a "
               "Seifert surface. Seifert's algorithm builds one from the "
               "knot diagram in four steps."),
            panel);

    surfaceIntro->setObjectName(QStringLiteral("Subtitle"));
    surfaceIntro->setWordWrap(true);

    layout->addWidget(surfaceIntro);

    seifertButton_ = new QPushButton(tr("BUILD SURFACE"), panel);

    Icons::set(seifertButton_, QStringLiteral("play"));

    seifertButton_->setObjectName(QStringLiteral("GenerateButton"));
    seifertButton_->setMinimumHeight(38);

    layout->addWidget(seifertButton_);

    seifertStepSlider_ = new QSlider(Qt::Horizontal, panel);

    seifertStepSlider_->setRange(0, 3);
    seifertStepSlider_->setPageStep(1);
    seifertStepSlider_->setTickPosition(QSlider::TicksBelow);
    seifertStepSlider_->setTickInterval(1);
    seifertStepSlider_->setEnabled(false);
    seifertStepSlider_->setToolTip(tr("Go back and forth through the four steps."));

    layout->addWidget(seifertStepSlider_);

    seifertStepTitle_ = new QLabel(panel);

    seifertStepTitle_->setObjectName(QStringLiteral("Section"));

    layout->addWidget(seifertStepTitle_);

    seifertStepText_ = new QLabel(panel);

    seifertStepText_->setObjectName(QStringLiteral("Subtitle"));
    seifertStepText_->setWordWrap(true);
    seifertStepText_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    layout->addWidget(seifertStepText_);

    layout->addSpacing(6);

    seifertInfoLabel_ = new QLabel(panel);

    seifertInfoLabel_->setObjectName(QStringLiteral("Status"));
    seifertInfoLabel_->setWordWrap(true);
    seifertInfoLabel_->setTextFormat(Qt::RichText);
    seifertInfoLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // the entries of the matrix are links (see updateSeifertInfo)
    seifertInfoLabel_->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    seifertInfoLabel_->setOpenExternalLinks(false);

    layout->addWidget(seifertInfoLabel_);

    connect(seifertInfoLabel_,
            &QLabel::linkActivated,
            this,
            &MainWindow::showSeifertLoops);

    layout->addStretch(1);

    seifertTimer_ = new QTimer(this);

    seifertTimer_->setInterval(2200);

    connect(seifertButton_, &QPushButton::clicked, this, &MainWindow::toggleSeifert);

    connect(seifertTimer_,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (seifertStep_ < 3)
                    setSeifertStep(seifertStep_ + 1);

                if (seifertStep_ >= 3)
                    seifertTimer_->stop();
            });

    // dragging the slider stops the automatic steps
    connect(seifertStepSlider_,
            &QSlider::sliderPressed,
            seifertTimer_,
            &QTimer::stop);

    connect(seifertStepSlider_,
            &QSlider::valueChanged,
            this,
            [this](int value)
            {
                if (value != seifertStep_)
                {
                    seifertTimer_->stop();
                    setSeifertStep(value);
                }
            });

    pages->addWidget(surfacePage);

    setSeifertStep(0);

    return pages;
}

QWidget *MainWindow::createRightPanel()
{
    QTabWidget *tabs = new QTabWidget(this);

    rightTabs_ = tabs;

    tabs->setObjectName(QStringLiteral("RightTabs"));
    tabs->setDocumentMode(true);
    tabs->setMinimumWidth(320);
    tabs->setMaximumWidth(460);

    /*
     * ANALYSIS
     */

    QWidget *analysis = new QWidget(tabs);

    analysis->setObjectName(QStringLiteral("Panel"));
    analysis->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *layout = new QVBoxLayout(analysis);

    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    QLabel *classCaption = new QLabel(tr("CLASSIFICATION"), analysis);

    classCaption->setObjectName(QStringLiteral("Caption"));

    classificationValueLabel_ = new QLabel(QStringLiteral("\u2014"), analysis);

    classificationValueLabel_->setObjectName(QStringLiteral("Headline"));
    classificationValueLabel_->setWordWrap(true);

    layout->addWidget(classCaption);
    layout->addWidget(classificationValueLabel_);

    layout->addSpacing(12);

    // two stat tiles
    QHBoxLayout *tiles = new QHBoxLayout();

    tiles->setSpacing(12);

    auto makeTile = [&](const QString &caption, QLabel *&valueLabel, QLabel **note)
    {
        QFrame *tile = new QFrame(analysis);

        tile->setObjectName(QStringLiteral("Tile"));

        QVBoxLayout *tileLayout = new QVBoxLayout(tile);

        tileLayout->setContentsMargins(16, 14, 16, 16);
        tileLayout->setSpacing(6);

        QLabel *captionLabel = new QLabel(caption, tile);

        captionLabel->setObjectName(QStringLiteral("Caption"));

        valueLabel = new QLabel(QStringLiteral("\u2014"), tile);

        valueLabel->setObjectName(QStringLiteral("Stat"));
        valueLabel->setWordWrap(true);

        tileLayout->addWidget(captionLabel);
        tileLayout->addWidget(valueLabel);

        if (note != nullptr)
        {
            *note = new QLabel(tile);

            (*note)->setObjectName(QStringLiteral("Subtitle"));
            (*note)->setWordWrap(true);
            (*note)->hide();

            tileLayout->addWidget(*note);
        }

        tileLayout->addStretch(1);

        tiles->addWidget(tile, 1);
    };

    makeTile(tr("WRITHE"), writheValueLabel_, nullptr);
    makeTile(tr("CROSSINGS"), crossingValueLabel_, &crossingNoteLabel_);

    layout->addLayout(tiles);

    layout->addSpacing(12);

    QFrame *polynomialTile = new QFrame(analysis);

    polynomialTile->setObjectName(QStringLiteral("Tile"));

    QVBoxLayout *polynomialLayout = new QVBoxLayout(polynomialTile);

    polynomialLayout->setContentsMargins(16, 14, 16, 16);
    polynomialLayout->setSpacing(8);

    QLabel *polynomialCaption = new QLabel(tr("ALEXANDER POLYNOMIAL"), polynomialTile);

    polynomialCaption->setObjectName(QStringLiteral("Caption"));

    alexanderValueLabel_ = new QLabel(QStringLiteral("\u2014"), polynomialTile);

    alexanderValueLabel_->setObjectName(QStringLiteral("Polynomial"));
    alexanderValueLabel_->setWordWrap(true);

    QLabel *jonesCaption = new QLabel(tr("JONES POLYNOMIAL"), polynomialTile);

    jonesCaption->setObjectName(QStringLiteral("Caption"));

    jonesValueLabel_ = new QLabel(QStringLiteral("\u2014"), polynomialTile);

    jonesValueLabel_->setObjectName(QStringLiteral("Polynomial"));
    jonesValueLabel_->setWordWrap(true);

    methodologyValueLabel_ = new QLabel(QStringLiteral("\u2014"), polynomialTile);

    methodologyValueLabel_->setObjectName(QStringLiteral("Subtitle"));
    methodologyValueLabel_->setWordWrap(true);

    polynomialLayout->addWidget(polynomialCaption);
    polynomialLayout->addWidget(alexanderValueLabel_);
    polynomialLayout->addSpacing(6);
    polynomialLayout->addWidget(jonesCaption);
    polynomialLayout->addWidget(jonesValueLabel_);
    polynomialLayout->addWidget(methodologyValueLabel_);

    layout->addWidget(polynomialTile);

    layout->addStretch(1);

    tabs->addTab(analysis, tr("ANALYSIS"));

    /*
     * LEARN
     */

    QWidget *learn = new QWidget(tabs);

    learn->setObjectName(QStringLiteral("Panel"));
    learn->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *learnLayout = new QVBoxLayout(learn);

    learnLayout->setContentsMargins(16, 16, 16, 16);

    educationalBrowser_ = new QTextBrowser(learn);

    educationalBrowser_->setOpenLinks(false);
    educationalBrowser_->setHtml(buildEducationalHtml());

    learnLayout->addWidget(educationalBrowser_, 1);

    tabs->addTab(learn, tr("LEARN"));

    return tabs;
}

QWidget *MainWindow::createSettingsPanel()
{
    QScrollArea *scroll = new QScrollArea(this);

    scroll->setObjectName(QStringLiteral("PanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *panel = new QWidget(scroll);

    panel->setObjectName(QStringLiteral("Panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *layout = new QVBoxLayout(panel);

    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto addCaption = [&](const QString &text)
    {
        QLabel *caption = new QLabel(text, panel);

        caption->setObjectName(QStringLiteral("Caption"));

        layout->addWidget(caption);
    };

    /*
     * LANGUAGE
     */

    addCaption(tr("LANGUAGE"));

    languageCombo_ = new QComboBox(panel);

    languageCombo_->setMinimumHeight(36);

    // every language under its own name
    languageCombo_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languageCombo_->addItem(QStringLiteral("Shqip"), QStringLiteral("sq"));

    layout->addWidget(languageCombo_);

    layout->addSpacing(12);

    /*
     * APPEARANCE
     */

    addCaption(tr("APPEARANCE"));

    layout->addWidget(makeFieldLabel(tr("Theme"), panel));

    themeCombo_ = new QComboBox(panel);

    themeCombo_->setMinimumHeight(36);
    themeCombo_->addItem(tr("Dark"), true);
    themeCombo_->addItem(tr("Light"), false);

    layout->addWidget(themeCombo_);

    layout->addWidget(makeFieldLabel(tr("Text size"), panel));

    textSizeCombo_ = new QComboBox(panel);

    textSizeCombo_->setMinimumHeight(36);
    textSizeCombo_->addItem(tr("Small"), 90);
    textSizeCombo_->addItem(tr("Normal"), 100);
    textSizeCombo_->addItem(tr("Large"), 115);
    textSizeCombo_->addItem(tr("Extra large"), 130);

    layout->addWidget(textSizeCombo_);

    layout->addSpacing(12);

    /*
     * WINDOW
     */

    addCaption(tr("WINDOW"));

    fullScreenButton_ = new QPushButton(panel);

    fullScreenButton_->setMinimumHeight(36);

    layout->addWidget(fullScreenButton_);

    startFullScreenCheck_ = new QCheckBox(tr("Start in full screen"), panel);

    rememberWindowCheck_ = new QCheckBox(tr("Remember window size"), panel);

    layout->addWidget(startFullScreenCheck_);
    layout->addWidget(rememberWindowCheck_);

    layout->addSpacing(12);

    QPushButton *restoreButton = new QPushButton(tr("Restore defaults"), panel);

    restoreButton->setMinimumHeight(36);

    layout->addWidget(restoreButton);

    QLabel *note =
        new QLabel(
            tr("Changes are saved automatically. F11 switches full screen "
               "on and off, Esc leaves it."),
            panel);

    note->setObjectName(QStringLiteral("Subtitle"));
    note->setWordWrap(true);

    layout->addWidget(note);

    layout->addStretch(1);

    scroll->setWidget(panel);

    updateSettingsControls();

    connect(
        languageCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            const QString code =
                languageCombo_->currentData().toString();

            if (code == AppSettings::language())
                return;

            AppSettings::setLanguage(code);
            AppSettings::installLanguage(code);

            // after the combo box has finished with its signal
            QTimer::singleShot(0, this, &MainWindow::rebuildWindow);
        });

    connect(
        themeCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            AppSettings::setDarkTheme(themeCombo_->currentData().toBool());

            applyThemeSettings();
        });

    connect(
        textSizeCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            AppSettings::setTextScale(textSizeCombo_->currentData().toInt());

            applyThemeSettings();
        });

    connect(
        startFullScreenCheck_,
        &QCheckBox::toggled,
        this,
        [](bool enabled)
        {
            AppSettings::setStartFullScreen(enabled);
        });

    connect(
        rememberWindowCheck_,
        &QCheckBox::toggled,
        this,
        [](bool enabled)
        {
            AppSettings::setRememberWindow(enabled);
        });

    connect(
        fullScreenButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::toggleFullScreen);

    connect(
        restoreButton,
        &QPushButton::clicked,
        this,
        [this]()
        {
            if (QMessageBox::question(
                    this,
                    tr("Restore defaults"),
                    tr("Reset all settings to their default values?")) !=
                QMessageBox::Yes)
            {
                return;
            }

            const QString previousLanguage =
                AppSettings::language();

            AppSettings::restoreDefaults();

            glWidget_->setLighting(AppSettings::lighting());

            updateLightingControls();

            if (AppSettings::language() != previousLanguage)
            {
                AppSettings::installLanguage(AppSettings::language());

                QTimer::singleShot(0, this, &MainWindow::rebuildWindow);

                return;
            }

            applyThemeSettings();

            updateSettingsControls();
        });

    return scroll;
}

QWidget *MainWindow::createLightingPanel()
{
    QScrollArea *scroll = new QScrollArea(this);

    scroll->setObjectName(QStringLiteral("PanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QWidget *panel = new QWidget(scroll);

    panel->setObjectName(QStringLiteral("Panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *layout = new QVBoxLayout(panel);

    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto addCaption = [&](const QString &text)
    {
        QLabel *caption = new QLabel(text, panel);

        caption->setObjectName(QStringLiteral("Caption"));

        layout->addWidget(caption);
    };

    // a check box that switches one option on and off
    auto addCheck = [&](const QString &text,
                        bool Lighting::Options::*option,
                        const QString &toolTip)
    {
        QCheckBox *check = new QCheckBox(text, panel);

        check->setToolTip(toolTip);

        layout->addWidget(check);

        lightingChecks_.push_back(qMakePair(check, option));

        connect(
            check,
            &QCheckBox::toggled,
            this,
            [this, option](bool enabled)
            {
                Lighting::Options options = glWidget_->lighting();

                options.*option = enabled;

                applyLighting(options);
            });
    };

    /*
     * PRESET
     */

    addCaption(tr("PRESET"));

    lightingPresetCombo_ = new QComboBox(panel);

    lightingPresetCombo_->setMinimumHeight(36);
    lightingPresetCombo_->addItem(tr("Studio"), static_cast<int>(Lighting::Preset::Studio));
    lightingPresetCombo_->addItem(tr("Soft"), static_cast<int>(Lighting::Preset::Soft));
    lightingPresetCombo_->addItem(tr("Dramatic"), static_cast<int>(Lighting::Preset::Dramatic));
    lightingPresetCombo_->addItem(tr("Flat"), static_cast<int>(Lighting::Preset::Flat));
    lightingPresetCombo_->addItem(tr("Custom"), static_cast<int>(Lighting::Preset::Custom));

    layout->addWidget(lightingPresetCombo_);

    lightingPresetText_ = new QLabel(panel);

    lightingPresetText_->setObjectName(QStringLiteral("Subtitle"));
    lightingPresetText_->setWordWrap(true);

    layout->addWidget(lightingPresetText_);

    layout->addSpacing(12);

    /*
     * LIGHTS
     */

    addCaption(tr("LIGHTS"));

    addCheck(tr("Key light"),
             &Lighting::Options::keyLight,
             tr("The main light, high on the left. Without it nothing casts a shadow."));

    addCheck(tr("Fill light"),
             &Lighting::Options::fillLight,
             tr("A weak light low on the right that brightens the shaded side."));

    addCheck(tr("Rim light"),
             &Lighting::Options::rimLight,
             tr("A light behind the knot that outlines its edges."));

    addCheck(tr("Reflections"),
             &Lighting::Options::reflections,
             tr("The studio lights seen in shiny surfaces."));

    addCheck(tr("Lights turn with the camera"),
             &Lighting::Options::followCamera,
             tr("The knot is always lit from the side. Off: the lights stay in "
                "place while you turn the view."));

    addSliderRow(layout, panel, tr("Brightness"),
                 brightnessSlider_, brightnessValue_,
                 50, 150, 100, QStringLiteral("100%"), 1.0);

    brightnessSlider_->setSingleStep(5);
    brightnessSlider_->setPageStep(10);

    layout->addSpacing(12);

    /*
     * SHADOWS
     */

    addCaption(tr("SHADOWS"));

    addCheck(tr("Shadows on the knot"),
             &Lighting::Options::knotShadows,
             tr("Strands cast shadows on each other and on the Seifert surface."));

    addCheck(tr("Shadow on the floor"),
             &Lighting::Options::floorShadow,
             tr("The shadow the key light casts on the floor."));

    addCheck(tr("Contact shadow"),
             &Lighting::Options::contactShadow,
             tr("The soft dark patch right under the knot."));

    addCheck(tr("Ambient occlusion"),
             &Lighting::Options::occlusion,
             tr("Darker where strands come close to each other."));

    addSliderRow(layout, panel, tr("Shadow softness"),
                 shadowSoftnessSlider_, shadowSoftnessValue_,
                 25, 400, 100, QStringLiteral("100%"), 1.0);

    shadowSoftnessSlider_->setSingleStep(5);
    shadowSoftnessSlider_->setPageStep(25);

    layout->addWidget(makeFieldLabel(tr("Shadow quality"), panel));

    shadowQualityCombo_ = new QComboBox(panel);

    shadowQualityCombo_->setMinimumHeight(36);
    shadowQualityCombo_->setToolTip(
        tr("Sharper shadow edges need a larger shadow map in video memory."));

    // memory of the shadow map: 4 bytes per texel (Lighting::shadowMapSize)
    shadowQualityCombo_->addItem(tr("Low (4 MB)"), 0);
    shadowQualityCombo_->addItem(tr("Medium (16 MB)"), 1);
    shadowQualityCombo_->addItem(tr("High (64 MB)"), 2);

    layout->addWidget(shadowQualityCombo_);

    layout->addWidget(makeFieldLabel(tr("Photo quality"), panel));

    photoQualityCombo_ = new QComboBox(panel);

    photoQualityCombo_->setMinimumHeight(36);
    photoQualityCombo_->setToolTip(
        tr("How much work the graphics card does in Photo mode. Automatic "
           "keeps the picture smooth on any computer. A still picture "
           "comes out clean at every level."));

    photoQualityCombo_->addItem(tr("Automatic"), 0);
    photoQualityCombo_->addItem(tr("High"), 1);
    photoQualityCombo_->addItem(tr("Medium"), 2);
    photoQualityCombo_->addItem(tr("Low"), 3);

    layout->addWidget(photoQualityCombo_);

    photoQualityNow_ = new QLabel(panel);

    photoQualityNow_->setObjectName(QStringLiteral("Subtitle"));

    layout->addWidget(photoQualityNow_);

    layout->addSpacing(12);

    /*
     * SCENE
     */

    addCaption(tr("SCENE"));

    addCheck(tr("Floor grid"),
             &Lighting::Options::floorGrid,
             tr("The lines on the floor under the knot."));

    layout->addSpacing(12);

    /*
     * GRAPHICS MEMORY
     */

    addCaption(tr("GRAPHICS MEMORY"));

    memoryAdapterLabel_ = new QLabel(panel);

    memoryAdapterLabel_->setObjectName(QStringLiteral("Field"));
    memoryAdapterLabel_->setWordWrap(true);

    layout->addWidget(memoryAdapterLabel_);

    memoryBar_ = new QProgressBar(panel);

    memoryBar_->setRange(0, 1000);
    memoryBar_->setTextVisible(false);
    memoryBar_->setFixedHeight(6);

    layout->addWidget(memoryBar_);

    memoryUsedLabel_ = new QLabel(panel);

    memoryUsedLabel_->setObjectName(QStringLiteral("Field"));
    memoryUsedLabel_->setWordWrap(true);

    layout->addWidget(memoryUsedLabel_);

    memoryViewLabel_ = new QLabel(panel);

    memoryViewLabel_->setObjectName(QStringLiteral("Subtitle"));
    memoryViewLabel_->setWordWrap(true);

    layout->addWidget(memoryViewLabel_);

    layout->addStretch(1);

    scroll->setWidget(panel);

    updateLightingControls();

    connect(
        lightingPresetCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            const Lighting::Preset preset =
                static_cast<Lighting::Preset>(lightingPresetCombo_->currentData().toInt());

            // Custom is only shown, not chosen
            if (preset == Lighting::Preset::Custom)
            {
                updateLightingControls();
                return;
            }

            Lighting::Options options =
                Lighting::preset(preset, glWidget_->lighting().shadowQuality);

            options.photoQuality = glWidget_->lighting().photoQuality;

            applyLighting(options);
        });

    connect(
        brightnessSlider_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            Lighting::Options options = glWidget_->lighting();

            options.brightness = value;

            applyLighting(options);
        });

    connect(
        shadowSoftnessSlider_,
        &QSlider::valueChanged,
        this,
        [this](int value)
        {
            Lighting::Options options = glWidget_->lighting();

            options.shadowSoftness = value;

            applyLighting(options);
        });

    connect(
        shadowQualityCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            Lighting::Options options = glWidget_->lighting();

            options.shadowQuality = shadowQualityCombo_->currentData().toInt();

            applyLighting(options);
        });

    connect(
        photoQualityCombo_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int)
        {
            Lighting::Options options = glWidget_->lighting();

            options.photoQuality = photoQualityCombo_->currentData().toInt();

            applyLighting(options);
        });

    connect(
        glWidget_,
        &KnotGLWidget::photoLevelChanged,
        this,
        [this](int level, bool automatic)
        {
            // it starts there the next time
            if (automatic)
                AppSettings::setPhotoAutoLevel(level);

            updatePhotoQualityNote();
        });

    memoryTimer_ = new QTimer(this);

    memoryTimer_->setInterval(1000);

    connect(
        memoryTimer_,
        &QTimer::timeout,
        this,
        [this]()
        {
            if (settingsDialog_ != nullptr && settingsDialog_->isVisible())
                updateMemoryMeter();
        });

    memoryTimer_->start();

    return scroll;
}

void MainWindow::applyLighting(
    const Lighting::Options &options)
{
    glWidget_->setLighting(options);

    AppSettings::setLighting(options);

    updateLightingControls();
    updateMemoryMeter();
}

void MainWindow::updateLightingControls()
{
    if (lightingPresetCombo_ == nullptr || glWidget_ == nullptr)
        return;

    const Lighting::Options &options = glWidget_->lighting();
    const Lighting::Preset preset = Lighting::presetOf(options);

    {
        const QSignalBlocker blocker(lightingPresetCombo_);

        lightingPresetCombo_->setCurrentIndex(
            std::max(0, lightingPresetCombo_->findData(static_cast<int>(preset))));
    }

    switch (preset)
    {
    case Lighting::Preset::Studio:
        lightingPresetText_->setText(
            tr("Three lights and soft boxes around the camera, with shadows: "
               "the full look."));
        break;

    case Lighting::Preset::Soft:
        lightingPresetText_->setText(
            tr("Gentle light and wide, soft shadows, without the rim light."));
        break;

    case Lighting::Preset::Dramatic:
        lightingPresetText_->setText(
            tr("No fill light: deep, crisp shadows and strong contrast."));
        break;

    case Lighting::Preset::Flat:
        lightingPresetText_->setText(
            tr("Even light without shadows or reflections: clear on paper "
               "and on projectors."));
        break;

    case Lighting::Preset::Custom:
        lightingPresetText_->setText(
            tr("Your own combination of the settings below."));
        break;
    }

    for (const QPair<QCheckBox *, bool Lighting::Options::*> &entry : lightingChecks_)
    {
        const QSignalBlocker blocker(entry.first);

        entry.first->setChecked(options.*(entry.second));
    }

    // the shadows need the key light
    for (const QPair<QCheckBox *, bool Lighting::Options::*> &entry : lightingChecks_)
    {
        if (entry.second == &Lighting::Options::knotShadows ||
            entry.second == &Lighting::Options::floorShadow)
        {
            entry.first->setEnabled(options.keyLight);
            entry.first->setToolTip(options.keyLight ? QString() : tr("Turn on the key light first."));
        }
    }

    {
        const QSignalBlocker blocker(brightnessSlider_);

        brightnessSlider_->setValue(options.brightness);
    }

    brightnessValue_->setText(QStringLiteral("%1%").arg(options.brightness));

    {
        const QSignalBlocker blocker(shadowSoftnessSlider_);

        shadowSoftnessSlider_->setValue(options.shadowSoftness);
    }

    shadowSoftnessValue_->setText(QStringLiteral("%1%").arg(options.shadowSoftness));

    {
        const QSignalBlocker blocker(shadowQualityCombo_);

        shadowQualityCombo_->setCurrentIndex(
            std::max(0, shadowQualityCombo_->findData(options.shadowQuality)));
    }

    {
        const QSignalBlocker blocker(photoQualityCombo_);

        photoQualityCombo_->setCurrentIndex(
            std::max(0, photoQualityCombo_->findData(options.photoQuality)));
    }

    updatePhotoQualityNote();
}

void MainWindow::updatePhotoQualityNote()
{
    if (photoQualityNow_ == nullptr || photoQualityCombo_ == nullptr)
        return;

    // only automatic quality has something to tell
    const bool automatic = photoQualityCombo_->currentData().toInt() == 0;

    const int found = photoQualityCombo_->findData(glWidget_->photoLevelInUse());

    photoQualityNow_->setVisible(automatic);
    photoQualityNow_->setText(tr("Working at: %1").arg(photoQualityCombo_->itemText(std::max(found, 0))));
}

void MainWindow::updateMemoryMeter()
{
    if (memoryBar_ == nullptr || glWidget_ == nullptr)
        return;

    auto size = [](qint64 bytes)
    {
        const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);

        if (megabytes >= 1024.0)
            return QStringLiteral("%1 GB").arg(QLocale().toString(megabytes / 1024.0, 'f', 1));

        return QStringLiteral("%1 MB").arg(
            QLocale().toString(megabytes, 'f', megabytes < 10.0 ? 1 : 0));
    };

    const GpuMemory::Info info = GpuMemory::query();
    const KnotGLWidget::MemoryUse view = glWidget_->memoryUse();

    if (info.valid)
    {
        memoryAdapterLabel_->setText(info.adapter);

        memoryBar_->setVisible(true);
        memoryBar_->setValue(
            info.budget > 0
                ? static_cast<int>(std::min<qint64>(1000, 1000 * info.used / info.budget))
                : 0);

        memoryUsedLabel_->setText(
            (info.shared
                 ? tr("This program: %1 of %2 (graphics memory shared with the main memory)")
                 : tr("This program: %1 of %2 video memory"))
                .arg(size(info.used), size(info.budget)));
    }
    else
    {
        memoryAdapterLabel_->setText(tr("The video memory cannot be read on this system."));

        memoryBar_->setVisible(false);

        memoryUsedLabel_->setText(
            tr("3D view in total (estimate): %1").arg(size(view.total())));
    }

    memoryViewLabel_->setText(
        tr("3D view (estimate): knot %1, shadow map %2, picture buffers %3, "
           "floor %4. The rest is the window and its panels.")
            .arg(size(view.mesh), size(view.shadowMap), size(view.view), size(view.other)));
}

void MainWindow::updateSettingsControls()
{
    if (languageCombo_ == nullptr)
        return;

    auto select = [](QComboBox *combo, const QVariant &value)
    {
        const QSignalBlocker blocker(combo);

        combo->setCurrentIndex(
            std::max(0, combo->findData(value)));
    };

    select(languageCombo_, AppSettings::language());
    select(themeCombo_, AppSettings::darkTheme());

    // the nearest offered size
    int scaleIndex = 0;

    for (int i = 0; i < textSizeCombo_->count(); ++i)
    {
        if (std::abs(textSizeCombo_->itemData(i).toInt() - AppSettings::textScale()) <
            std::abs(textSizeCombo_->itemData(scaleIndex).toInt() - AppSettings::textScale()))
        {
            scaleIndex = i;
        }
    }

    select(textSizeCombo_, textSizeCombo_->itemData(scaleIndex));

    {
        const QSignalBlocker blocker(startFullScreenCheck_);

        startFullScreenCheck_->setChecked(AppSettings::startFullScreen());
    }

    {
        const QSignalBlocker blocker(rememberWindowCheck_);

        rememberWindowCheck_->setChecked(AppSettings::rememberWindow());
    }

    fullScreenButton_->setText(
        isFullScreen()
            ? tr("Leave full screen  (F11)")
            : tr("Full screen  (F11)"));
}

void MainWindow::applyThemeSettings()
{
    Theme::apply(
        AppSettings::darkTheme(),
        AppSettings::textScale());

    fitPanelsToText();

    // texts with their own colours
    updateLearnSection();

    if (seifertActive_ && !seifertDirty_ && seifert_.valid)
        updateSeifertInfo();

    if (unknotActive_)
        renderUnknotFrame();

    if (glWidget_ != nullptr)
        glWidget_->update();
}

void MainWindow::fitPanelsToText()
{
    const int scale =
        Theme::textScale();

    // widths at the normal text size: least, most, first
    struct Panel
    {
        QWidget *widget;
        int minimum;
        int maximum;
        int initial;
    };

    const Panel panels[] =
    {
        {leftPages_, 280, 380, 310},
        {rightTabs_, 320, 460, 370}
    };

    QVector<int> initial;

    // The 3D view keeps room for its playback bar, and the window for the
    // view next to both panels (never more than the screen has: in less
    // room the panels are folded away).
    const int viewWidth = 460 * scale / 100;

    glWidget_->parentWidget()->setMinimumWidth(viewWidth);

    int windowWidth = viewWidth + 100;

    for (const Panel &panel : panels)
    {
        // the tab bar scrolls when its tabs do not fit (with the style
        // sheet's font, also before the window is first shown)
        panel.widget->ensurePolished();

        const QTabWidget *tabs = qobject_cast<QTabWidget *>(panel.widget);

        const int minimum =
            std::max(
                panel.minimum * scale / 100,
                tabs != nullptr ? tabs->tabBar()->sizeHint().width() : 0);

        panel.widget->setMinimumWidth(minimum);
        panel.widget->setMaximumWidth(std::max(panel.maximum * scale / 100, minimum));

        initial.push_back(std::max(panel.initial * scale / 100, minimum));

        windowWidth += minimum;
    }

    setMinimumWidth(
        std::min(
            windowWidth,
            screen()->availableGeometry().width()));

    // the first sizes again only when the text size changed, so that a
    // dragged divider stays where it is when only the theme changes
    if (scale == panelTextScale_)
        return;

    panelTextScale_ = scale;

    int total = 0;

    for (const int size : mainSplitter_->sizes())
        total += size;

    // before the window is shown the splitter has no size yet
    if (total < initial[0] + initial[1] + 400)
        total = 1700;

    mainSplitter_->setSizes(
        QVector<int>{ initial[0], total - initial[0] - initial[1], initial[1] });
}

void MainWindow::toggleFullScreen()
{
    // keeps the maximized flag, so leaving goes back to the same state
    setWindowState(
        windowState() ^ Qt::WindowFullScreen);
}

void MainWindow::changeEvent(
    QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        if (leaveFullScreenShortcut_ != nullptr)
            leaveFullScreenShortcut_->setEnabled(isFullScreen());

        updateSettingsControls();
    }

    QMainWindow::changeEvent(
        event);
}

void MainWindow::showInitially()
{
    QTimer::singleShot(600, this, &MainWindow::offerAutosave);

    if (!AppSettings::tourSeen())
        QTimer::singleShot(1200, this, &MainWindow::startTour);

    const QByteArray geometry =
        AppSettings::rememberWindow()
            ? AppSettings::windowGeometry()
            : QByteArray();

    if (geometry.isEmpty() ||
        !restoreGeometry(geometry))
    {
        resize(
            1800,
            1000);
    }

    // full screen only when asked for, even if the window was closed so,
    // and never straight away (see showFullScreenLater)
    setWindowState(windowState() & ~Qt::WindowFullScreen);

    show();

    if (AppSettings::startFullScreen())
        showFullScreenLater();
}

void MainWindow::showFullScreenLater()
{
    // A window that is already full screen when it first appears skips the
    // 1 px border of main.cpp: Windows puts the OpenGL window into direct
    // mode and it takes no clicks until F11 is pressed twice. Shown first
    // and switched a moment later, it goes the same way as F11.
    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            setWindowState(windowState() | Qt::WindowFullScreen);
        });
}

void MainWindow::closeEvent(
    QCloseEvent *event)
{
    if (!replaced_)
    {
        pauseRelaxation();

        if (historyTimer_->isActive())
        {
            historyTimer_->stop();

            recordState();
        }

        if (worthKeeping() && !history_.isSaved())
        {
            QMessageBox box(this);

            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(tr("AKSIOMA: Knots"));
            box.setText(tr("Save this knot before closing?"));
            box.setInformativeText(tr("Its changes are not in a file yet."));

            QPushButton *save = box.addButton(tr("Save..."), QMessageBox::AcceptRole);
            QPushButton *discard = box.addButton(tr("Don't save"), QMessageBox::DestructiveRole);
            QPushButton *cancel = box.addButton(tr("Cancel"), QMessageBox::RejectRole);

            box.setDefaultButton(save);
            box.setEscapeButton(cancel);

            box.exec();

            if (box.clickedButton() == cancel || (box.clickedButton() == save && !saveKnot()))
            {
                event->ignore();
                return;
            }

            Q_UNUSED(discard);
        }

        // closed properly: nothing to bring back next time
        QFile::remove(autosavePath());

        saveWorkspace();
    }

    if (layingProgress_)
        layingProgress_->cancel = true;

    if (AppSettings::rememberWindow())
        AppSettings::setWindowGeometry(saveGeometry());

    QMainWindow::closeEvent(
        event);
}

bool MainWindow::eventFilter(
    QObject *object,
    QEvent *event)
{
    if (object == glWidget_ && event->type() == QEvent::Resize)
        placeNotice();

    if (event->type() == QEvent::Resize && tourCard_ != nullptr && object == tourCard_->parent())
        placeTour();

    /*
     * How the keyboard came to a control. Tabbed to with the real key, it
     * answers to Space and the arrows itself; only clicked, they go on being
     * the program's. (The reason Qt gives with the focus is no help: the
     * first control of a new window also gets it "by tab".)
     */
    if (event->type() == QEvent::MouseButtonPress)
        tabFocused_ = false;

    if (event->type() == QEvent::KeyPress)
    {
        const int pressed = static_cast<QKeyEvent *>(event)->key();

        if (pressed == Qt::Key_Tab || pressed == Qt::Key_Backtab)
            tabFocused_ = true;
    }

    if (event->type() == QEvent::KeyPress && QApplication::activeWindow() == this &&
        QApplication::activePopupWidget() == nullptr)
    {
        const QKeyEvent *key = static_cast<QKeyEvent *>(event);

        QWidget *focus = QApplication::focusWidget();

        const bool typing =
            qobject_cast<QLineEdit *>(focus) != nullptr ||
            qobject_cast<QTextEdit *>(focus) != nullptr ||
            qobject_cast<QPlainTextEdit *>(focus) != nullptr ||
            qobject_cast<QAbstractSpinBox *>(focus) != nullptr;

        const bool plain = (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) == 0;

        if (!typing && plain)
        {
            if (key->text() == QStringLiteral("?"))
            {
                showShortcuts();
                return true;
            }

            if (!tabFocused_ && !key->isAutoRepeat())
            {
                if (key->key() == Qt::Key_Space)
                {
                    playPause();
                    return true;
                }

                if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)
                {
                    stepBy(key->key() == Qt::Key_Right ? 1 : -1);
                    return true;
                }
            }
        }
    }

    return QMainWindow::eventFilter(object, event);
}

void MainWindow::dragEnterEvent(
    QDragEnterEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls())
    {
        const QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();

        if (url.isLocalFile() &&
            (suffix == QStringLiteral("aknot") || suffix == QStringLiteral("txt") || suffix == QStringLiteral("csv")))
        {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(
    QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();

    if (urls.isEmpty())
        return;

    event->acceptProposedAction();

    openKnotFilePath(urls.first().toLocalFile());
}

void MainWindow::playPause()
{
    if (unknotActive_)
    {
        unknotPlayPause();
        return;
    }

    switch (leftPages_->currentIndex())
    {
    case 1:
        relaxPlayPause();
        break;

    case 2:
        unknotPlayPause();
        break;

    case 3:
        joinPlayStop();
        break;

    case 4:
        toggleSeifert();
        break;

    default:
        autoRotationCheck_->toggle();
        break;
    }
}

void MainWindow::stepBy(
    int direction)
{
    if (unknotActive_)
    {
        constexpr double Nudge = 0.05;

        // to the next stage or crossing change, if there is one that way
        double target = std::clamp(unknotT_ + direction * Nudge, 0.0, 1.0);

        if (direction > 0)
        {
            for (const double stop : unknotStops_)
            {
                if (stop > unknotT_ + 1.0e-4)
                {
                    target = stop;
                    break;
                }
            }
        }
        else
        {
            for (int i = static_cast<int>(unknotStops_.size()) - 1; i >= 0; --i)
            {
                if (unknotStops_.at(i) < unknotT_ - 1.0e-4)
                {
                    target = unknotStops_.at(i);
                    break;
                }
            }
        }

        showUnknotMoment(target);

        return;
    }

    if (seifertActive_)
    {
        seifertTimer_->stop();

        seifertStepSlider_->setValue(seifertStepSlider_->value() + direction);
    }
}

void MainWindow::showShortcuts()
{
    QDialog dialog(this);

    dialog.setWindowTitle(tr("Keyboard shortcuts"));

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    QGridLayout *grid = new QGridLayout();

    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(7);

    int row = 0;

    auto heading = [&](const QString &text)
    {
        QLabel *label = new QLabel(text, &dialog);

        label->setObjectName(QStringLiteral("Caption"));

        grid->addWidget(label, row, 0, 1, 2);
        grid->setRowMinimumHeight(row, 26);

        ++row;
    };

    auto line = [&](const QString &keys, const QString &what)
    {
        QLabel *left = new QLabel(keys, &dialog);

        left->setObjectName(QStringLiteral("Value"));

        QLabel *right = new QLabel(what, &dialog);

        right->setObjectName(QStringLiteral("Field"));

        grid->addWidget(left, row, 0);
        grid->addWidget(right, row, 1);

        ++row;
    };

    heading(tr("ANYWHERE"));
    line(QStringLiteral("Ctrl+K"), tr("Find anything"));
    line(QStringLiteral("Ctrl+Z  /  Ctrl+Y"), tr("Undo and redo"));
    line(QStringLiteral("Ctrl+O  /  Ctrl+S"), tr("Open and save a knot"));
    line(QStringLiteral("Ctrl+E  /  Ctrl+D"), tr("Export the picture and the diagram"));
    line(QStringLiteral("Ctrl+,"), tr("Settings"));
    line(QStringLiteral("F11  /  Esc"), tr("Full screen on and off"));
    line(QStringLiteral("F1  /  ?"), tr("This list"));

    heading(tr("TOOLS"));
    line(QStringLiteral("Ctrl+1 ... Ctrl+5"), tr("Open a tool"));
    line(tr("Space"), tr("Play or pause what the open tool does"));
    line(QStringLiteral("\u2190  /  \u2192"), tr("Step back and forward: unknotting, surface"));

    heading(tr("MOUSE"));
    line(tr("Drag"), tr("Turn the view"));
    line(tr("Right-drag"), tr("Move the view"));
    line(tr("Scroll"), tr("Zoom"));
    line(tr("Shift+drag"), tr("Push the rope under another strand"));
    line(tr("Double-click a slider"), tr("Back to its usual value"));
    line(tr("Click a slider's number"), tr("Type an exact value"));

    layout->addLayout(grid);

    QPushButton *close = new QPushButton(tr("Close"), &dialog);

    close->setMinimumHeight(36);

    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    layout->addWidget(close, 0, Qt::AlignRight);

    dialog.exec();
}

void MainWindow::fillRecentMenu()
{
    recentMenu_->clear();

    const QStringList files = AppSettings::recentFiles();

    for (const QString &path : files)
    {
        QAction *action = recentMenu_->addAction(QFileInfo(path).fileName());

        action->setToolTip(path);

        connect(action, &QAction::triggered, this, [this, path]()
        {
            openRecentFile(path);
        });
    }

    if (files.isEmpty())
    {
        recentMenu_->addAction(tr("No files yet"))->setEnabled(false);
        return;
    }

    recentMenu_->addSeparator();

    connect(recentMenu_->addAction(tr("Clear the list")), &QAction::triggered, this, []()
    {
        AppSettings::clearRecentFiles();
    });
}

void MainWindow::openRecentFile(
    const QString &path)
{
    if (!QFile::exists(path))
    {
        AppSettings::removeRecentFile(path);

        showNotice(
            tr("%1 is no longer there, so it was taken off the list.").arg(QFileInfo(path).fileName()),
            Notice::Error);

        return;
    }

    openKnotFilePath(path);
}

void MainWindow::openCommandPalette()
{
    QVector<PaletteItem> items;

    // everything in the File menu, under its own name and shortcut
    for (QAction *action : fileMenu_->actions())
    {
        if (action->isSeparator() || action->menu() != nullptr || !action->isEnabled() ||
            action->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_K))
        {
            continue;
        }

        QString title = action->text();

        title.remove(QStringLiteral("..."));

        items.push_back({title, action->shortcut().toString(QKeySequence::NativeText), [action]()
                         {
                             action->trigger();
                         }});
    }

    // the tools
    for (int page = 0; page < leftPages_->count(); ++page)
    {
        QAbstractButton *button = railGroup_->button(page);

        items.push_back({button->text(), tr("Tool"), [this, page, button]()
                         {
                             if (leftPages_->currentIndex() != page || leftPages_->isHidden())
                                 button->click();
                         }});
    }

    // what the tools do: their page opens with them
    auto doing = [this, &items](QAbstractButton *button, const QString &title, int page)
    {
        items.push_back({title, railGroup_->button(page)->text(), [this, button, page]()
                         {
                             if (leftPages_->currentIndex() != page || leftPages_->isHidden())
                                 railGroup_->button(page)->click();

                             if (button->isEnabled())
                                 button->click();
                         }});
    };

    doing(scrambleButton_, tr("Scramble"), 1);
    doing(grabButton_, tr("Grab the rope"), 1);
    doing(joinButton_, tr("JOIN"), 3);
    doing(seifertButton_, tr("BUILD SURFACE"), 4);

    items.push_back({tr("Browse..."), QString(), [this]() { openKnotBrowser(); }});
    items.push_back({tr("Draw..."), QString(), [this]() { drawKnotDialog(); }});
    items.push_back({tr("Practice"), QString(), [this]() { openPractice(); }});
    items.push_back({tr("Settings"), QStringLiteral("Ctrl+,"), [this]() { showSettings(); }});
    items.push_back({tr("About"), QString(), [this]() { showAbout(); }});
    items.push_back({tr("Reset view"), QString(), [this]() { resetCamera(); }});
    items.push_back({tr("Photo"), QString(), [this]() { photoButton_->toggle(); }});
    items.push_back({tr("Analysis", "view bar"), QString(), [this]() { analysisButton_->toggle(); }});

    // the knots in the list, and the scenes
    for (int i = 0; i < presetCombo_->count(); ++i)
    {
        items.push_back({presetCombo_->itemText(i), tr("Knot"), [this, i]()
                         {
                             presetCombo_->setCurrentIndex(i);
                         }});
    }

    for (int i = 0; i < photoSceneCombo_->count(); ++i)
    {
        items.push_back({photoSceneCombo_->itemText(i), tr("Photo"), [this, i]()
                         {
                             photoSceneCombo_->setCurrentIndex(i);

                             if (!photoButton_->isChecked())
                                 photoButton_->setChecked(true);
                         }});
    }

    for (const QString &path : AppSettings::recentFiles())
    {
        items.push_back({QFileInfo(path).fileName(), tr("Recent"), [this, path]()
                         {
                             openRecentFile(path);
                         }});
    }

    // the table of knots, by name, with or without its marks
    auto searchKnots = [this](const QString &query)
    {
        struct Named
        {
            QString name;
            QString text;
        };

        static const QVector<Named> names = []()
        {
            const KnotMath::KnotTable &table = KnotMath::KnotTable::instance();

            QVector<Named> all;

            all.reserve(table.size());

            for (int i = 0; i < table.size(); ++i)
            {
                const QString &name = table.at(i).name;

                all.push_back({name,
                               Palette::folded(name + QLatin1Char(' ') +
                                               KnotMath::KnotTable::displayName(name) + QLatin1Char(' ') +
                                               KnotMath::KnotTable::commonName(name))});
            }

            return all;
        }();

        QVector<PaletteItem> found;

        // only what really contains it: letters scattered over 13,000 names
        // would match everything
        for (const Named &entry : names)
        {
            if (!entry.text.contains(query))
                continue;

            const QString common = KnotMath::KnotTable::commonName(entry.name);
            const QString shown = KnotMath::KnotTable::displayName(entry.name);

            const QString name = entry.name;

            found.push_back({common.isEmpty() ? shown : QStringLiteral("%1  %2").arg(shown, common), tr("Knot"),
                             [this, name]()
                             {
                                 showTableKnot(name);
                             }});

            if (found.size() >= 20)
                break;
        }

        return found;
    };

    CommandPalette palette(items, searchKnots, this);

    palette.move(mapToGlobal(QPoint((width() - palette.width()) / 2, 90)));

    // After the palette has gone: closing it hands the keyboard back to this
    // window, which would take it from a window the action opens.
    if (palette.exec() == QDialog::Accepted && palette.chosen().run)
        QTimer::singleShot(0, this, palette.chosen().run);
}

void MainWindow::startTour()
{
    endTour();

    // (seen is seen, however it ends)
    AppSettings::setTourSeen();

    tourStep_ = 0;

    tourCard_->show();
    tourCard_->raise();

    showTourStep();
}

void MainWindow::showTourStep()
{
    constexpr int Steps = 3;

    for (QWidget *widget : {static_cast<QWidget *>(presetCombo_), railWidget_, static_cast<QWidget *>(photoButton_)})
    {
        widget->setProperty("tour", false);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }

    if (tourStep_ >= Steps)
    {
        endTour();
        return;
    }

    const QString titles[Steps] = {
        tr("Pick a knot"),
        tr("Try a tool"),
        tr("See it as a real rope")};

    const QString texts[Steps] = {
        tr("Choose one here, browse all 12,965, or draw your own."),
        tr("Change its style, relax it, undo it, join two knots or build a "
           "surface. Click the open tool again to fold its panel."),
        tr("Photo lays the knot down as real rope in the sun. Ctrl+K finds "
           "anything, and F1 lists every shortcut.")};

    tourCount_->setText(QStringLiteral("%1 / %2").arg(tourStep_ + 1).arg(Steps));
    tourTitle_->setText(titles[tourStep_]);
    tourText_->setText(texts[tourStep_]);
    tourNext_->setText(tourStep_ + 1 < Steps ? tr("Next") : tr("Done"));
    tourSkip_->setVisible(tourStep_ + 1 < Steps);

    QWidget *target =
        tourStep_ == 0 ? static_cast<QWidget *>(presetCombo_)
                       : (tourStep_ == 1 ? railWidget_ : static_cast<QWidget *>(photoButton_));

    target->setProperty("tour", true);
    target->style()->unpolish(target);
    target->style()->polish(target);

    placeTour();
}

void MainWindow::placeTour()
{
    if (tourCard_ == nullptr || tourCard_->isHidden() || tourStep_ < 0)
        return;

    QWidget *parent = tourCard_->parentWidget();

    tourCard_->adjustSize();

    QPoint place;

    if (tourStep_ == 0)
        place = presetCombo_->mapTo(parent, QPoint(0, presetCombo_->height() + 12));
    else if (tourStep_ == 1)
        place = railWidget_->mapTo(parent, QPoint(railWidget_->width() + 12, 24));
    else
        place = photoButton_->mapTo(parent, QPoint(photoButton_->width() - tourCard_->width(), -tourCard_->height() - 12));

    tourCard_->move(
        std::clamp(place.x(), 8, std::max(8, parent->width() - tourCard_->width() - 8)),
        std::clamp(place.y(), 8, std::max(8, parent->height() - tourCard_->height() - 8)));
}

void MainWindow::endTour()
{
    tourStep_ = -1;

    if (tourCard_ != nullptr)
        tourCard_->hide();

    for (QWidget *widget : {static_cast<QWidget *>(presetCombo_), railWidget_, static_cast<QWidget *>(photoButton_)})
    {
        if (widget != nullptr && widget->property("tour").toBool())
        {
            widget->setProperty("tour", false);
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
        }
    }
}

void MainWindow::restoreWorkspace()
{
    const int page =
        std::clamp(AppSettings::workspaceValue(QStringLiteral("tool"), 0), 0, leftPages_->count() - 1);

    leftPages_->setCurrentIndex(page);
    railGroup_->button(page)->setChecked(true);
    leftPages_->setVisible(AppSettings::workspaceValue(QStringLiteral("toolPanel"), 1) != 0);

    analysisButton_->setChecked(AppSettings::workspaceValue(QStringLiteral("analysisPanel"), 1) != 0);

    rightTabs_->setCurrentIndex(AppSettings::workspaceValue(QStringLiteral("rightTab"), 0));

    {
        const QSignalBlocker blocker(photoSceneCombo_);

        const int scene = AppSettings::workspaceValue(QStringLiteral("photoScene"), 0);

        if (scene >= 0 && scene < photoSceneCombo_->count())
            photoSceneCombo_->setCurrentIndex(scene);
    }

    updateRailActivity();
}

void MainWindow::saveWorkspace()
{
    AppSettings::setWorkspaceValue(QStringLiteral("tool"), leftPages_->currentIndex());
    AppSettings::setWorkspaceValue(QStringLiteral("toolPanel"), leftPages_->isHidden() ? 0 : 1);
    AppSettings::setWorkspaceValue(QStringLiteral("analysisPanel"), analysisButton_->isChecked() ? 1 : 0);
    AppSettings::setWorkspaceValue(QStringLiteral("rightTab"), rightTabs_->currentIndex());
    AppSettings::setWorkspaceValue(QStringLiteral("photoScene"), photoSceneCombo_->currentIndex());
}

void MainWindow::noteChange()
{
    if (!restoring_ && historyTimer_ != nullptr)
        historyTimer_->start();
}

void MainWindow::recordState()
{
    // the end of what is moving now will call again
    if (relaxRunning_ || pulling_ || joinActive_ || unknotPreparing_)
    {
        historyTimer_->start();
        return;
    }

    history_.record(currentState());

    updateUndoControls();

    writeAutosave();
}

KnotState MainWindow::currentState() const
{
    KnotState state;

    state.presetIndex = presetCombo_->currentIndex();
    state.torusP = torusPSpin_->value();
    state.torusQ = torusQSpin_->value();
    state.tableKnot = tableKnotName_;
    state.customText = customInputEdit_->toPlainText();

    if (relaxChanged_)
        state.shape = relax_.isValid() ? relax_.curve() : currentData_.points;

    state.thickness = thicknessSlider_->value();
    state.samples = meshDensitySlider_->value();
    state.material = materialCombo_->currentIndex();
    state.shine = metallicSlider_->value();
    state.colorMode = colorModeCombo_->currentIndex();
    state.color = meshColor_;

    return state;
}

void MainWindow::restoreState(
    const KnotState &state)
{
    restoring_ = true;

    // a new material also sets its own shine, so it goes first
    thicknessSlider_->setValue(state.thickness);
    meshDensitySlider_->setValue(state.samples);
    materialCombo_->setCurrentIndex(state.material);
    metallicSlider_->setValue(state.shine);
    colorModeCombo_->setCurrentIndex(state.colorMode);

    torusPSpin_->setValue(state.torusP);
    torusQSpin_->setValue(state.torusQ);

    tableKnotName_ = state.tableKnot;

    customInputEdit_->setPlainText(state.customText);

    {
        const QSignalBlocker blocker(presetCombo_);

        presetCombo_->setCurrentIndex(state.presetIndex);
    }

    lastPresetIndex_ = state.presetIndex;

    meshColor_ = state.color;

    updateColorSwatch();

    glWidget_->setMeshColor(meshColor_);

    updateCustomInputVisibility();

    // one rebuild instead of one for every value set above
    rebuildTimer_->stop();
    pendingFullRegenerate_ = false;

    generateMesh();

    if (!state.shape.isEmpty())
        applyShape(state.shape);

    updateLearnSection();

    restoring_ = false;

    historyTimer_->stop();

    updateUndoControls();

    writeAutosave();
}

void MainWindow::applyShape(
    const QVector<KnotMath::Vec3> &shape)
{
    currentData_.points = shape;

    currentCrossings_ =
        KnotMath::detectProjectionCrossings(
            currentData_.points,
            2,
            700);

    relaxChanged_ = true;

    KnotMath::Invariants invariants =
        KnotMath::computeInvariantsWithCrossings(
            currentPreset(),
            currentData_.points,
            currentCrossings_,
            currentData_.closed,
            torusPSpin_->value(),
            torusQSpin_->value(),
            currentData_.tableIndex);

    applyConnectedSumInvariants(invariants);

    updateInvariantPanel(invariants);

    rebuildMesh();

    updateRelaxControls();
}

void MainWindow::undo()
{
    // the rope in the hand first: back as it was laid down
    if (photoRopeTouched_)
    {
        photoRopeTouched_ = false;

        stopPhotoRope();

        generateMesh();

        showNotice(tr("The rope is back as it was laid down."), Notice::Info);

        updateUndoControls();

        return;
    }

    pauseRelaxation();

    if (historyTimer_->isActive())
    {
        historyTimer_->stop();

        history_.record(currentState());
    }

    if (history_.canUndo())
        restoreState(history_.undo());
}

void MainWindow::redo()
{
    if (history_.canRedo())
        restoreState(history_.redo());
}

void MainWindow::updateUndoControls()
{
    if (undoAction_ == nullptr || undoButton_ == nullptr)
        return;

    const bool back = history_.canUndo() || photoRopeTouched_;
    const bool forward = history_.canRedo() && !photoRopeTouched_;

    undoAction_->setEnabled(back);
    undoButton_->setEnabled(back);
    redoAction_->setEnabled(forward);
    redoButton_->setEnabled(forward);
}

bool MainWindow::worthKeeping() const
{
    if (!hasData_)
        return false;

    const bool written =
        currentPreset() == KnotMath::Preset::CustomCoordinates &&
        customInputEdit_->toPlainText().trimmed() != QString::fromLatin1(ExampleCurve).trimmed();

    return written || relaxChanged_;
}

KnotExport::KnotFile MainWindow::currentKnotFile() const
{
    const KnotMath::Preset preset = currentPreset();

    KnotExport::KnotFile file;

    file.name = currentData_.name;
    file.preset = preset;
    file.torusP = torusPSpin_->value();
    file.torusQ = torusQSpin_->value();

    if (preset == KnotMath::Preset::KnotTable)
        file.tableKnot = tableKnotName_;

    if (preset == KnotMath::Preset::CustomCoordinates)
        file.customText = customInputEdit_->toPlainText();

    file.points = currentData_.points;
    file.shapeChanged = relaxChanged_;

    file.hasAppearance = true;
    file.thickness = thicknessSlider_->value();
    file.samples = meshDensitySlider_->value();
    file.material = materialCombo_->currentIndex();
    file.shine = metallicSlider_->value();
    file.colorMode = colorModeCombo_->currentIndex();
    file.color = meshColor_;

    return file;
}

void MainWindow::writeAutosave()
{
    if (!worthKeeping() || history_.isSaved())
    {
        QFile::remove(autosavePath());
        return;
    }

    QString error;

    // (written whole or not at all; a failure here must not get in the way)
    KnotExport::writeKnotFile(autosavePath(), currentKnotFile(), error);
}

void MainWindow::offerAutosave()
{
    // the newest, if a crash came after an unanswered offer
    if (QFile::exists(autosavePath()))
    {
        QFile::remove(recoveredPath());
        QFile::rename(autosavePath(), recoveredPath());
    }

    if (!QFile::exists(recoveredPath()))
        return;

    showNotice(
        tr("The last knot was not saved when AKSIOMA closed."),
        Notice::Info,
        tr("Restore"),
        [this]()
        {
            loadKnotFile(recoveredPath(), false);

            QFile::remove(recoveredPath());
        });

    // it waits to be answered, and closing it lets the knot go
    noticeTimer_->stop();

    noticeDismissed_ = []()
    {
        QFile::remove(recoveredPath());
    };
}

void MainWindow::showNotice(
    const QString &text,
    Notice kind,
    const QString &actionText,
    const std::function<void()> &action)
{
    noticeTimer_->stop();

    noticeDo_ = action;
    noticeDismissed_ = nullptr;

    noticeText_->setText(text);

    noticeBar_->setProperty("kind", kind == Notice::Error ? QStringLiteral("error") : QStringLiteral("info"));
    noticeBar_->style()->unpolish(noticeBar_);
    noticeBar_->style()->polish(noticeBar_);

    noticeProgress_->setVisible(kind == Notice::Busy);
    noticeClose_->setVisible(kind != Notice::Busy);

    noticeAction_->setText(actionText);
    noticeAction_->setVisible(!actionText.isEmpty());

    noticeBar_->show();
    noticeBar_->raise();

    placeNotice();

    if (kind == Notice::Info)
        noticeTimer_->start(actionText.isEmpty() ? 5000 : 9000);
}

void MainWindow::hideNotice()
{
    noticeTimer_->stop();
    noticeBar_->hide();

    noticeDo_ = nullptr;
    noticeDismissed_ = nullptr;
}

void MainWindow::placeNotice()
{
    if (noticeBar_ == nullptr || noticeBar_->isHidden())
        return;

    const int width = std::min(glWidget_->width() - 32, 620);

    noticeBar_->setFixedWidth(width);
    noticeBar_->adjustSize();
    noticeBar_->move((glWidget_->width() - width) / 2, 16);
}

void MainWindow::showSaved(
    const QString &path)
{
    const QFileInfo info(path);

    const QString folder = info.absolutePath();

    showNotice(
        tr("Saved: %1").arg(info.fileName()),
        Notice::Info,
        tr("Show folder"),
        [folder]()
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        });
}

void MainWindow::updateLayingProgress()
{
    if (!restingLaying_ || !layingProgress_)
    {
        layingTimer_->stop();
        return;
    }

    // short jobs pass without a word
    if (layingClock_.elapsed() < 400)
        return;

    if (noticeBar_->isHidden() || !noticeProgress_->isVisible())
    {
        const std::shared_ptr<Photo::RopeProgress> progress = layingProgress_;

        showNotice(
            tr("Laying the rope on the table"),
            Notice::Busy,
            tr("Cancel"),
            [progress]()
            {
                progress->cancel = true;
            });
    }

    noticeProgress_->setValue(layingProgress_->percent.load());
}

void MainWindow::rebuildWindow()
{
    // a language is chosen in Settings, so they stay open in the new one
    const bool settingsOpen =
        settingsDialog_->isVisible();

    const bool fullScreen =
        isFullScreen();

    MainWindow *replacement = new MainWindow();

    replacement->setAttribute(
        Qt::WA_DeleteOnClose);

    replacement->takeOverFrom(
        *this);

    replacement->setWindowState(
        replacement->windowState() & ~Qt::WindowFullScreen);

    replacement->show();

    if (fullScreen)
        replacement->showFullScreenLater();

    if (settingsOpen)
    {
        replacement->settingsDialog_->move(settingsDialog_->pos());
        replacement->showSettings();
    }

    replaced_ = true;

    close();
}

void MainWindow::showSettings()
{
    settingsDialog_->show();
    settingsDialog_->raise();
    settingsDialog_->activateWindow();
}

void MainWindow::takeOverFrom(
    MainWindow &previous)
{
    previous.stopUnknotting(
        false);

    restoring_ = true;

    restoreGeometry(
        previous.saveGeometry());

    mainSplitter_->setSizes(
        previous.mainSplitter_->sizes());

    leftPages_->setCurrentIndex(
        previous.leftPages_->currentIndex());

    leftPages_->setVisible(
        !previous.leftPages_->isHidden());

    railGroup_->button(leftPages_->currentIndex())->setChecked(true);

    rightTabs_->setCurrentIndex(
        previous.rightTabs_->currentIndex());

    analysisButton_->setChecked(
        previous.analysisButton_->isChecked());

    // the knot
    tableKnotName_ = previous.tableKnotName_;

    customInputEdit_->setPlainText(previous.customInputEdit_->toPlainText());
    torusPSpin_->setValue(previous.torusPSpin_->value());
    torusQSpin_->setValue(previous.torusQSpin_->value());

    // appearance; a new material also sets its own shine, so it goes first
    thicknessSlider_->setValue(previous.thicknessSlider_->value());
    meshDensitySlider_->setValue(previous.meshDensitySlider_->value());
    materialCombo_->setCurrentIndex(previous.materialCombo_->currentIndex());
    metallicSlider_->setValue(previous.metallicSlider_->value());
    colorModeCombo_->setCurrentIndex(previous.colorModeCombo_->currentIndex());
    showCrossingNumbersCheck_->setChecked(previous.showCrossingNumbersCheck_->isChecked());
    autoRotationCheck_->setChecked(previous.autoRotationCheck_->isChecked());

    unknotSpeedCombo_->setCurrentIndex(previous.unknotSpeedCombo_->currentIndex());
    unknotStepCheck_->setChecked(previous.unknotStepCheck_->isChecked());

    meshColor_ = previous.meshColor_;

    updateColorSwatch();

    glWidget_->setMeshColor(meshColor_);

    {
        const QSignalBlocker blocker(presetCombo_);

        presetCombo_->setCurrentIndex(previous.presetCombo_->currentIndex());
    }

    lastPresetIndex_ = presetCombo_->currentIndex();

    updateCustomInputVisibility();

    // one rebuild instead of one for every value set above
    rebuildTimer_->stop();
    pendingFullRegenerate_ = false;

    generateMesh();

    if (previous.relaxChanged_)
        applyShape(previous.currentState().shape);

    updateLearnSection();

    restoring_ = false;

    historyTimer_->stop();

    history_ = previous.history_;

    updateUndoControls();
}

QString MainWindow::buildEducationalHtml() const
{
    QString colorMode =
        tr("Single Color");

    QString colorModeDescription =
        tr(
            "The entire tube uses the selected base color.");

    if (colorModeCombo_ != nullptr)
    {
        const int index =
            colorModeCombo_->currentIndex();

        if (index ==
            static_cast<int>(
                KnotMath::ColorMode::CrossingColors))
        {
            colorMode =
                tr("Crossing Colors");

            colorModeDescription =
                tr(
                    "Each detected projection crossing is assigned "
                    "a different color. The two local branches involved "
                    "in the same crossing share that crossing color.");
        }
        else if (index ==
                 static_cast<int>(
                     KnotMath::ColorMode::StrandColors))
        {
            colorMode =
                tr("Strand Colors");

            colorModeDescription =
                tr(
                    "Each arc between two under-crossings receives its own "
                    "color, making the path of the knot easier to follow.");
        }
    }

    const QString currentPreset =
        presetCombo_ != nullptr
            ? presetCombo_->currentText()
            : tr("Current knot");

    // Only the texts are translated; the markup is added here.
    auto heading = [](const QString &text, const QString &margin)
    {
        return QStringLiteral(
                   "<div style='font-size:10.5pt;font-weight:700;"
                   "color:#BAB9B4;%1'>%2</div>")
            .arg(margin, text);
    };

    auto topic = [](const QString &title, const QString &body)
    {
        return QStringLiteral(
                   "<p><b style='color:#D99B42;'>%1</b><br>%2</p>")
            .arg(title, body);
    };

    QString html =
        QStringLiteral(
            "<html><body style='font-family:Segoe UI,Arial;font-size:9pt;"
            "color:#BAB9B4;line-height:1.38;'>");

    html += heading(tr("CURRENT OBJECT"), QStringLiteral("margin-bottom:6px;"));

    html += QStringLiteral("<p style='margin-top:0;'><b style='color:#D99B42;'>%1</b></p>")
                .arg(currentPreset.toHtmlEscaped());

    html += heading(tr("CORE CONCEPTS"), QStringLiteral("margin-top:8px;"));

    html += topic(
        tr("Writhe"),
        tr("A geometric measure of how the displayed 3D curve coils "
           "through space. Reshaping the same knot can change its writhe."));

    html += topic(
        tr("Projection crossings"),
        tr("AKSIOMA detects intersections in the XY projection. This is "
           "diagram-dependent; the mathematical crossing number is the "
           "minimum crossing count over all regular projections."));

    html += topic(
        tr("Alexander polynomial"),
        tr("A classical knot invariant. It can distinguish many different "
           "knot types, but it is not a complete classifier."));

    html += topic(
        tr("Jones polynomial"),
        tr("A knot invariant found in 1984. Unlike the Alexander polynomial "
           "it usually tells a knot from its mirror image (V(t) becomes "
           "V(1/t)), so it can show whether a trefoil is left- or "
           "right-handed."));

    html += heading(tr("CURRENT VISUALIZATION"), QStringLiteral("margin-top:8px;"));

    html += QStringLiteral("<p><b>%1</b><br>%2</p>")
                .arg(colorMode, colorModeDescription);

    html += QStringLiteral("<p>%1</p>")
                .arg(tr("When crossing numbers are enabled, numbered markers "
                        "identify detected crossings in the current XY "
                        "projection."));

    html += QStringLiteral("</body></html>");

    return Theme::html(html);
}

void MainWindow::updateLearnSection()
{
    if (educationalBrowser_ != nullptr)
    {
        educationalBrowser_->setHtml(
            buildEducationalHtml());
    }
}

void MainWindow::presetChanged(
    int index)
{
    // The knot table needs a knot: let the user pick one first.
    if (currentPreset() == KnotMath::Preset::KnotTable &&
        tableKnotName_.isEmpty() &&
        !chooseTableKnot())
    {
        presetCombo_->blockSignals(
            true);

        presetCombo_->setCurrentIndex(
            lastPresetIndex_);

        presetCombo_->blockSignals(
            false);

        return;
    }

    lastPresetIndex_ =
        index;

    updateCustomInputVisibility();

    applyDefaultCustomText(
        index);

    updateLearnSection();

    // Presets are shown immediately; the custom curve has its own APPLY.
    generateMesh();
}

bool MainWindow::chooseTableKnot()
{
    KnotBrowserDialog dialog(
        tableKnotName_,
        this);

    if (dialog.exec() != QDialog::Accepted ||
        dialog.selectedKnot().isEmpty())
    {
        return false;
    }

    tableKnotName_ =
        dialog.selectedKnot();

    return true;
}

void MainWindow::openKnotBrowser()
{
    if (chooseTableKnot())
        showTableKnot(tableKnotName_);
}

void MainWindow::showTableKnot(
    const QString &name)
{
    tableKnotName_ = name;

    const int tableIndex =
        presetCombo_->findData(
            static_cast<int>(
                KnotMath::Preset::KnotTable));

    if (presetCombo_->currentIndex() != tableIndex)
    {
        // presetChanged() shows the knot
        presetCombo_->setCurrentIndex(
            tableIndex);
    }
    else
    {
        updateLearnSection();
        generateMesh();
    }
}

void MainWindow::openPractice()
{
    // the exercises need a single closed curve
    QVector<KnotMath::Vec3> curve;

    if (hasData_ && !currentData_.multiStrand && currentData_.closed)
        curve = currentData_.points;

    PracticeDialog dialog(
        curve,
        currentData_.name,
        this);

    dialog.exec();
}

void MainWindow::showAbout()
{
    AboutDialog dialog(
        this);

    dialog.exec();
}

void MainWindow::applyDefaultCustomText(
    int /*presetIndex*/)
{
    // Only offer an example formula when the user switches to the custom
    // curve and has not typed anything yet. Selecting a built-in preset
    // never overwrites the text.
    if (currentPreset() ==
            KnotMath::Preset::CustomCoordinates &&
        customInputEdit_->toPlainText()
            .trimmed()
            .isEmpty())
    {
        customInputEdit_->setPlainText(
            QString::fromLatin1(ExampleCurve));
    }
}

KnotMath::Preset MainWindow::currentPreset() const
{
    return static_cast<KnotMath::Preset>(
        presetCombo_->currentData().toInt());
}

void MainWindow::scheduleUpdate(
    bool fullRegenerate)
{
    if (fullRegenerate)
        pendingFullRegenerate_ = true;

    if (rebuildTimer_ != nullptr)
        rebuildTimer_->start();
}

void MainWindow::updateCustomInputVisibility()
{
    const KnotMath::Preset preset = currentPreset();

    if (torusRow_ != nullptr)
    {
        torusRow_->setVisible(
            preset == KnotMath::Preset::CustomTorus);
    }

    if (customBox_ != nullptr)
    {
        customBox_->setVisible(
            preset == KnotMath::Preset::CustomCoordinates);
    }
}

void MainWindow::generateMesh()
{
    stopRelaxation();

    // a new knot (or the same one again) is laid down afresh
    stopPhotoRope();

    // the knot is rebuilt below
    stopJoin(false);

    if (unknotActive_ || unknotPreparing_)
        stopUnknotting(false);

    if (glWidget_ == nullptr ||
        presetCombo_ == nullptr ||
        meshDensitySlider_ == nullptr ||
        thicknessSlider_ == nullptr ||
        torusPSpin_ == nullptr ||
        torusQSpin_ == nullptr ||
        customInputEdit_ == nullptr ||
        colorModeCombo_ == nullptr)
    {
        return;
    }

    try
    {
        const KnotMath::Preset preset =
            currentPreset();

        KnotMath::CurveData data =
            KnotMath::buildPreset(
                preset,
                meshDensitySlider_->value(),
                torusPSpin_->value(),
                torusQSpin_->value(),
                preset == KnotMath::Preset::KnotTable
                    ? tableKnotName_
                    : customInputEdit_->toPlainText());

        if (data.points.size() < 3)
        {
            throw std::runtime_error(
                tr(
                    "Generated curve contains fewer than three points.")
                    .toStdString());
        }

        currentData_ =
            data;

        seifertDirty_ =
            true;

        // the loops belonged to the old surface
        seifertLoop_ =
            -1;

        seifertLifted_ =
            -1;

        // Crossings are computed once and shared by the colouring and the
        // numbered markers. Their positions are stored as arc-length
        // fractions, so they stay valid for any tube resolution.
        currentCrossings_.clear();

        if (!currentData_.multiStrand)
        {
            currentCrossings_ =
                KnotMath::detectProjectionCrossings(
                    currentData_.points,
                    2,
                    700);
        }

        if (currentData_.multiStrand)
        {
            updateInvariantPanel(
                KnotMath::computeMultiStrandInvariants(
                    preset,
                    currentData_));
        }
        else
        {
            KnotMath::Invariants invariants =
                KnotMath::computeInvariantsWithCrossings(
                    preset,
                    currentData_.points,
                    currentCrossings_,
                    currentData_.closed,
                    torusPSpin_->value(),
                    torusQSpin_->value(),
                    currentData_.tableIndex);

            applyConnectedSumInvariants(
                invariants);

            updateInvariantPanel(
                invariants);
        }

        hasData_ =
            true;

        rebuildMesh();

        // a different knot: fitted into the view with room around it
        const QString knot =
            QStringLiteral("%1|%2|%3|%4|%5")
                .arg(static_cast<int>(preset))
                .arg(tableKnotName_)
                .arg(torusPSpin_->value())
                .arg(torusQSpin_->value())
                .arg(customInputEdit_->toPlainText());

        if (knot != framedKnot_)
        {
            framedKnot_ = knot;

            glWidget_->fitNewKnot(seifertActive_ ? 45.0f : 75.0f);
        }

        updateLearnSection();

        noteChange();
    }
    catch (const std::exception &exception)
    {
        showError(
            QString::fromUtf8(
                exception.what()));
    }
    catch (...)
    {
        showError(
            tr(
                "An unknown error occurred while generating the knot."));
    }
}

void MainWindow::applySurface()
{
    if (glWidget_ == nullptr ||
        metallicSlider_ == nullptr)
    {
        return;
    }

    const int material =
        materialCombo_ != nullptr
            ? materialCombo_->currentIndex()
            : 0;

    const float shine =
        static_cast<float>(
            metallicSlider_->value()) /
        100.0f;

    // shine 0 = matte, 1 = almost a mirror
    glWidget_->setSurface(
        material,
        0.92f - 0.85f * shine);

    glWidget_->setMaterialProperties(
        material == 2 ? 1.0f : 0.0f);
}

void MainWindow::rebuildMesh()
{
    // thickness and colour changes redraw the surface
    if (seifertActive_)
    {
        renderSeifert();
        return;
    }

    // the moving curve is drawn every frame (with the current thickness)
    if (relaxRunning_ || pulling_ || joinActive_)
        return;

    if (unknotActive_)
        stopUnknotting(false);

    if (!hasData_ ||
        glWidget_ == nullptr)
    {
        return;
    }

    const double requestedRadius =
        static_cast<double>(
            thicknessSlider_->value()) /
        100.0;

    // Tubes are thinned (never thickened) where strands come so close
    // that tubes of the requested thickness would run into each other.
    const double radius =
        KnotMath::safeTubeRadius(
            currentData_,
            requestedRadius);

    if (thicknessValueLabel_ != nullptr)
    {
        const QString requestedText =
            QString::number(requestedRadius, 'f', 2);

        if (radius < requestedRadius - 0.005)
        {
            thicknessValueLabel_->setText(
                QStringLiteral("%1 → %2")
                    .arg(requestedText)
                    .arg(radius, 0, 'f', 2));

            thicknessValueLabel_->setToolTip(
                tr("Tube thinned from %1 to %2 so that strands "
                   "don't touch.")
                    .arg(requestedText)
                    .arg(radius, 0, 'f', 2));
        }
        else
        {
            thicknessValueLabel_->setText(
                requestedText);

            thicknessValueLabel_->setToolTip(
                QString());
        }
    }

    const KnotMath::ColorMode colorMode =
        static_cast<KnotMath::ColorMode>(
            colorModeCombo_->currentIndex());

    KnotMath::Mesh mesh;

    if (currentData_.multiStrand)
    {
        // Every component is its own tube; closed strands get a distinct
        // colour (the first one is the colour chosen by the user), open
        // pieces such as base-pair rungs are neutral.
        const QVector<QColor> palette =
        {
            meshColor_,
            QColor(217, 155, 66),
            QColor(138, 135, 128),
            QColor(232, 230, 223),
            QColor(110, 86, 48)
        };

        int closedIndex =
            0;

        for (int strand = 0;
             strand < currentData_.separateStrands.size();
             ++strand)
        {
            const bool strandClosed =
                strand < currentData_.strandClosed.size()
                    ? currentData_.strandClosed.at(strand)
                    : true;

            const double radiusScale =
                strand < currentData_.strandRadiusScale.size()
                    ? currentData_.strandRadiusScale.at(strand)
                    : 1.0;

            // drawn along a smooth spline (the analysis keeps the points)
            const QVector<KnotMath::Vec3> &strandPoints =
                currentData_.separateStrands.at(strand);

            KnotMath::Mesh part =
                KnotMath::buildTubeMesh(
                    KnotMath::refineCurve(
                        strandPoints,
                        KnotMath::refineFactorForTube(
                            strandPoints,
                            radius * radiusScale,
                            strandClosed),
                        strandClosed),
                    radius * radiusScale,
                    strandClosed
                        ? 32
                        : 16,
                    strandClosed,
                    KnotMath::ColorMode::SingleColor,
                    2);

            if (!part.isValid())
                continue;

            if (strandClosed)
            {
                part.fillColor(
                    palette.at(
                        closedIndex %
                        palette.size()));

                ++closedIndex;
            }
            else
            {
                part.fillColor(
                    QColor(138, 135, 128));
            }

            mesh.append(
                part);
        }
    }
    else if (showKeptPhotoRope(radius) || showRestingRope(radius))
    {
        // Photo mode: the knot lies on the table as a real rope (as it
        // was left, if it was picked up)
        return;
    }
    else
    {
        // Drawn along a smooth spline through the points, so long curves
        // do not look polygonal. Crossing colours are stored as fractions
        // of the curve and stay in place.
        mesh =
            KnotMath::buildTubeMeshWithCrossings(
                KnotMath::refineCurve(
                    currentData_.points,
                    KnotMath::refineFactorForTube(
                        currentData_.points,
                        radius,
                        currentData_.closed),
                    currentData_.closed),
                radius,
                32,
                currentData_.closed,
                colorMode,
                currentCrossings_);
    }

    if (!mesh.isValid())
    {
        showError(
            tr(
                "Tube mesh generation failed."));

        return;
    }

    // depth cue: strands that come close shade each other
    KnotMath::computeAmbientOcclusion(
        mesh,
        0.7);

    // laid down by its shape in Photo mode (not by the physics)
    glWidget_->clearRestingPose();

    glWidget_->setCrossings(
        currentCrossings_);

    glWidget_->setMesh(
        mesh);

    glWidget_->setMeshColor(
        meshColor_);

    applySurface();

    glWidget_->setShowCrossingNumbers(
        showCrossingNumbersCheck_ != nullptr &&
        showCrossingNumbersCheck_->isChecked());

    updateGrabCurve();
}

void MainWindow::updateInvariantPanel(
    const KnotMath::Invariants &invariants)
{
    currentInvariants_ =
        invariants;

    if (writheValueLabel_ != nullptr)
    {
        // no "-0.00" for a writhe that rounds to zero
        const double writhe =
            std::abs(invariants.writhe) < 0.005 ? 0.0 : invariants.writhe;

        writheValueLabel_->setText(
            QString::number(
                writhe,
                'f',
                2)
                .replace(QLatin1Char('-'), QChar(0x2212)));

        writheValueLabel_->setToolTip(
            QString::number(
                invariants.writhe,
                'f',
                5));
    }

    if (crossingValueLabel_ != nullptr)
    {
        crossingNoteLabel_->hide();

        if (invariants.crossingNumber < 0)
        {
            crossingValueLabel_->setText(
                QStringLiteral("—"));
        }
        else
        {
            crossingValueLabel_->setText(
                QString::number(
                    invariants.crossingNumber));

            // The diagram on screen is not always a minimal diagram.
            if (invariants.knownCrossingNumber >= 0 &&
                invariants.knownCrossingNumber !=
                    invariants.crossingNumber)
            {
                crossingNoteLabel_->setText(
                    tr("in this view; at least %1")
                        .arg(invariants.knownCrossingNumber));

                crossingNoteLabel_->show();
            }
        }
    }

    if (classificationValueLabel_ != nullptr)
    {
        classificationValueLabel_->setText(
            invariants.classification);
    }

    if (alexanderValueLabel_ != nullptr)
    {
        alexanderValueLabel_->setText(
            invariants.alexanderPolynomial);
    }

    if (jonesValueLabel_ != nullptr)
    {
        jonesValueLabel_->setText(
            invariants.jonesPolynomial);
    }

    if (methodologyValueLabel_ != nullptr)
    {
        methodologyValueLabel_->setText(
            invariants.methodology);
    }

    updateSneakerChoice();
}

bool MainWindow::knotFitsSneaker() const
{
    if (!hasData_ || currentData_.multiStrand || !currentData_.closed ||
        currentData_.points.size() < 4)
    {
        return false;
    }

    // the knot's own crossing number when it is known, else this diagram's
    const int crossings =
        currentInvariants_.knownCrossingNumber >= 0
            ? currentInvariants_.knownCrossingNumber
            : currentInvariants_.crossingNumber;

    return crossings >= 0 && crossings <= 7;
}

void MainWindow::updateSneakerChoice()
{
    if (photoSceneCombo_ == nullptr)
        return;

    auto *model = qobject_cast<QStandardItemModel *>(photoSceneCombo_->model());

    const int index = photoSceneCombo_->findData(static_cast<int>(Photo::Scene::Sneaker));

    if (model == nullptr || index < 0)
        return;

    // (before the first knot every scene can be chosen)
    const bool fits = !hasData_ || knotFitsSneaker();

    QStandardItem *item = model->item(index);

    item->setEnabled(fits);
    item->setToolTip(
        fits ? QString()
             : tr("Only knots with up to 7 crossings can be tied on the sneaker: "
                  "larger knots do not fit on a shoe."));

    if (!fits && photoSceneCombo_->currentIndex() == index)
    {
        photoSceneCombo_->setCurrentIndex(
            photoSceneCombo_->findData(static_cast<int>(Photo::Scene::BallroomFloor)));
    }
}

void MainWindow::chooseColor()
{
    const QColor selected =
        QColorDialog::getColor(
            meshColor_,
            this,
            tr("Choose Tube Color"));

    if (!selected.isValid())
        return;

    meshColor_ =
        selected;

    glWidget_->setMeshColor(
        meshColor_);

    applySurface();

    updateColorSwatch();

    rebuildMesh();

    noteChange();
}

void MainWindow::updateColorSwatch()
{
    colorButton_->setStyleSheet(
        QStringLiteral(
            "QPushButton {"
            " background:%1;"
            " border:1px solid #53514C;"
            " padding:0px;"
            "}"
            "QPushButton:hover {"
            " border:1px solid #BAB9B4;"
            "}")
            .arg(
                meshColor_.name()));
}

void MainWindow::resetCamera()
{
    // the Seifert surface lies flat: seen from the side at an angle, its
    // levels show
    if (glWidget_ != nullptr)
        glWidget_->resetCamera(seifertActive_ ? 45.0f : 75.0f);
}

bool MainWindow::showRestingRope(double radius)
{
    if (glWidget_ == nullptr || !glWidget_->photoMode() || !hasData_ ||
        currentData_.multiStrand || currentData_.points.size() < 4 ||
        pulling_ || relaxRunning_ ||
        seifertActive_ || unknotActive_ || joinActive_)
    {
        return false;
    }

    const auto scene =
        static_cast<Photo::Scene>(photoSceneCombo_->currentData().toInt());

    // with Grab on, the lace on the sneaker is lifted off to be pulled;
    // a rope on the table is picked up where it lies
    if (grabEnabled() && Photo::sceneInfo(scene).sneaker)
        return false;

    const Photo::RopeMaterial material = Photo::sceneInfo(scene).rope;

    // tied in the sneaker's lace (only knots that fit), or on the table
    const bool sneaker = Photo::sceneInfo(scene).sneaker;

    if (sneaker && !knotFitsSneaker())
        return false;

    const bool known =
        restingRope_.valid &&
        restingScene_ == static_cast<int>(scene) &&
        std::abs(restingRadius_ - radius) < 1.0e-9 &&
        restingFrom_.size() == currentData_.points.size() &&
        std::equal(
            restingFrom_.cbegin(),
            restingFrom_.cend(),
            currentData_.points.cbegin(),
            [](const KnotMath::Vec3 &a, const KnotMath::Vec3 &b)
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            });

    if (!known)
    {
        /*
         * The rope takes a second or two to fall and come to rest (more
         * for a long one, a knot of a dozen crossings): worked out on a
         * worker thread, so the window never freezes. Meanwhile the knot
         * is shown as before; once the rope lies still it is drawn again.
         */
        const bool asked =
            restingLaying_ &&
            restingJobScene_ == static_cast<int>(scene) &&
            std::abs(restingJobRadius_ - radius) < 1.0e-9 &&
            restingJobFrom_.size() == currentData_.points.size() &&
            std::equal(
                restingJobFrom_.cbegin(),
                restingJobFrom_.cend(),
                currentData_.points.cbegin(),
                [](const KnotMath::Vec3 &a, const KnotMath::Vec3 &b)
                {
                    return a.x == b.x && a.y == b.y && a.z == b.z;
                });

        if (!asked)
            layRestingRope(radius);

        return false;
    }

    if (!restingRope_.valid)
        return false;

    const KnotMath::Mesh mesh =
        Photo::buildRopeMesh(
            restingRope_.points,
            currentData_.closed,
            radius,
            material,
            false,
            restingRope_.frames);

    if (!mesh.isValid())
        return false;

    /*
     * The crossing numbers stay on their crossings: each is at the same
     * place along the rope (a fraction of its length) as before. On the
     * sneaker the knot is a part of the lace: it starts at cutFraction of
     * the knot, and the cut left out gapFraction of it.
     */
    const QVector<KnotMath::Vec3> &rest = restingRope_.points;
    const int n = rest.size();

    QVector<double> arc(n + 1, 0.0);

    for (int i = 0; i < n; ++i)
        arc[i + 1] = arc.at(i) + (rest.at((i + 1) % n) - rest.at(i)).length();

    const Photo::TiedLace &tied = restingTied_;
    const bool inLace = sneaker && tied.knotTo > tied.knotFrom;

    auto pointAt = [&rest, &arc, n, &tied, inLace](double fraction)
    {
        double target = (fraction - std::floor(fraction)) * arc.at(n);

        if (inLace)
        {
            double along = fraction - tied.cutFraction;

            along = std::clamp((along - std::floor(along)) / std::max(1.0 - tied.gapFraction, 1.0e-6), 0.0, 1.0);

            target = arc.at(tied.knotFrom) + along * (arc.at(tied.knotTo) - arc.at(tied.knotFrom));
        }

        const int i = std::clamp(
            static_cast<int>(std::upper_bound(arc.cbegin(), arc.cend(), target) - arc.cbegin()) - 1,
            0,
            n - 1);

        const double piece = arc.at(i + 1) - arc.at(i);
        const double t = piece > 0.0 ? (target - arc.at(i)) / piece : 0.0;

        return rest.at(i) + (rest.at((i + 1) % n) - rest.at(i)) * t;
    };

    QVector<KnotMath::CrossingInfo> crossings = currentCrossings_;

    for (KnotMath::CrossingInfo &crossing : crossings)
    {
        crossing.pointA = pointAt(crossing.fractionA);
        crossing.pointB = pointAt(crossing.fractionB);
        crossing.midpoint = (crossing.pointA + crossing.pointB) * 0.5;
    }

    glWidget_->setCrossings(crossings);
    glWidget_->setRestingRope(mesh, restingRope_.pose);
    glWidget_->setMeshColor(meshColor_);

    applySurface();

    glWidget_->setShowCrossingNumbers(
        showCrossingNumbersCheck_ != nullptr &&
        showCrossingNumbersCheck_->isChecked());

    // Grab on: the rope is ready to be picked up
    if (grabEnabled() && startPhotoRope())
    {
        glWidget_->setRopeGrab(true);
        showPhotoRope(false);

        return true;
    }

    updateGrabCurve();

    return true;
}

void MainWindow::layRestingRope(double radius)
{
    const auto scene =
        static_cast<Photo::Scene>(photoSceneCombo_->currentData().toInt());

    const Photo::RopeMaterial material = Photo::sceneInfo(scene).rope;
    const bool sneaker = Photo::sceneInfo(scene).sneaker;

    const QVector<KnotMath::Vec3> points = currentData_.points;
    const bool closed = currentData_.closed;

    const quint64 job = ++restingJob_;

    restingLaying_ = true;
    restingJobFrom_ = points;
    restingJobRadius_ = radius;
    restingJobScene_ = static_cast<int>(scene);

    auto result = std::make_shared<Photo::TiedLace>();

    // what is still being laid down is for nothing now
    if (layingProgress_)
        layingProgress_->cancel = true;

    auto progress = std::make_shared<Photo::RopeProgress>();

    layingProgress_ = progress;

    layingClock_.start();
    layingTimer_->start();

    QThread *worker = QThread::create(
        [points, closed, radius, material, sneaker, result, progress]()
        {
            Photo::TiedLace tied;

            // tied in the sneaker's lace, or dropped on the table
            if (sneaker)
                tied = Photo::tieOnSneaker(points, radius, material, progress.get());
            else
                tied.rope = Photo::layOnTable(points, closed, radius, material, progress.get());

            /*
             * The rope never passes through itself, so it is the same knot
             * (the lace through the eyelets as well); checked anyway with
             * the Alexander polynomial of both centre lines. A rope that
             * changed is not shown.
             */
            if (tied.rope.valid && closed)
            {
                const KnotMath::AlexanderPolynomial before =
                    KnotMath::computeAlexanderPolynomial(points);

                const KnotMath::AlexanderPolynomial after =
                    KnotMath::computeAlexanderPolynomial(tied.rope.points);

                if (before.valid && (!after.valid || after.text != before.text))
                    tied.rope.valid = false;
            }

            *result = tied;
        });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);

    connect(
        worker,
        &QThread::finished,
        this,
        [this, job, result, points, radius, scene]()
        {
            // a newer rope was asked for meanwhile
            if (job != restingJob_)
                return;

            restingLaying_ = false;

            layingTimer_->stop();

            if (noticeProgress_->isVisible())
                hideNotice();

            // given up: back to the plain knot, nothing half done
            if (result->rope.cancelled)
            {
                restingJobFrom_.clear();

                photoButton_->setChecked(false);

                showNotice(
                    tr("Cancelled. Turn on Photo to lay the rope down again."),
                    Notice::Info);

                return;
            }

            restingRope_ = result->rope;
            restingTied_ = *result;
            restingFrom_ = points;
            restingRadius_ = radius;
            restingScene_ = static_cast<int>(scene);

            glWidget_->setCaption(QString(), QString());

            // the rope lying still, drawn where the knot was
            rebuildMesh();

            /*
             * The camera was framed while the rope was still being laid
             * down, for what was there then. Now it can see the rope, and
             * in the sneaker's scene the shoe around it, to their scale.
             */
            const QString framing = QStringLiteral("%1 %2").arg(static_cast<int>(scene)).arg(framedKnot_);

            if (framing != restingFramed_)
            {
                restingFramed_ = framing;

                resetCamera();
            }
        });

    worker->start();
}

void MainWindow::applyPhotoMode()
{
    if (glWidget_ == nullptr || photoButton_ == nullptr || photoSceneCombo_ == nullptr)
        return;

    const bool on = photoButton_->isChecked();

    photoSceneCombo_->setVisible(on);

    // Grab goes over to the new scene (or out of Photo mode): let go, and
    // take hold again below
    const bool grabbing = grabEnabled();

    if (grabbing)
        grabButton_->setChecked(false);

    stopPhotoRope();

    // (a rope still being laid down says so until it lies still)
    glWidget_->setCaption(QString(), QString());

    restingFramed_.clear();

    const auto scene =
        static_cast<Photo::Scene>(photoSceneCombo_->currentData().toInt());

    // the room is read and prepared on the graphics card (a moment)
    QApplication::setOverrideCursor(Qt::WaitCursor);

    const bool ok = glWidget_->setPhotoMode(on, scene);

    QApplication::restoreOverrideCursor();

    // the knot falls onto the table (or stands up again)
    rebuildMesh();

    if (!ok)
    {
        {
            const QSignalBlocker blocker(photoButton_);

            photoButton_->setChecked(false);
        }

        photoSceneCombo_->setVisible(false);

        showNotice(
            tr("The photographed place could not be loaded. Installing "
               "AKSIOMA again brings it back."),
            Notice::Error);
    }

    if (grabbing)
        grabButton_->setChecked(true);

    resetCamera();
}

void MainWindow::showError(
    const QString &message)
{
    showNotice(
        tr("The curve could not be made: %1 Check the formula, or press "
           "Undo to go back.").arg(message),
        Notice::Error);
}


/*
 * =========================================================
 * UNKNOTTING ANIMATION
 * =========================================================
 */

void MainWindow::setUnknotControlsEnabled(
    bool playing)
{
    // paused in the middle of a step-by-step run: the button goes on
    const bool nextStep =
        unknotActive_ &&
        unknotT_ < 1.0 &&
        unknotStepCheck_ != nullptr &&
        unknotStepCheck_->isChecked();

    unknotPlayButton_->setText(
        unknotPreparing_
            ? tr("Preparing...")
            : playing
                  ? tr("PAUSE")
                  : (nextStep
                         ? tr("NEXT")
                         : (unknotActive_
                                ? tr("PLAY")
                                : tr("UNKNOT", "button"))));

    Icons::set(
        unknotPlayButton_,
        unknotPreparing_ ? QString() : (playing ? QStringLiteral("pause") : QStringLiteral("play")));

    unknotPlayButton_->setEnabled(
        !unknotPreparing_);

    // Reset also cancels an animation that is still being prepared.
    unknotResetButton_->setEnabled(
        unknotActive_ || unknotPreparing_);

    unknotScrubSlider_->setEnabled(
        unknotActive_);

    unknotSummaryButton_->setEnabled(
        unknotActive_);
}

void MainWindow::startUnknotting()
{
    if (!hasData_ || unknotPreparing_)
        return;

    stopJoin();

    if (seifertActive_)
        stopSeifert();

    // unknots the curve as it is now
    if (relaxRunning_)
        pauseRelaxation();

    if (currentData_.multiStrand ||
        !currentData_.closed)
    {
        unknotStatusLabel_->setText(
            tr("Unknotting needs a single closed curve. "
               "Choose a knot preset or a custom curve."));

        return;
    }

    const double requestedRadius =
        static_cast<double>(
            thicknessSlider_->value()) /
        100.0;

    const QVector<KnotMath::Vec3> curve =
        currentData_.points;

    const quint64 buildId =
        ++unknotBuildId_;

    unknotPreparing_ =
        true;

    unknotStatusLabel_->setText(
        tr("Preparing the animation..."));

    setUnknotControlsEnabled(
        false);

    // The plan search can take a moment for complicated curves, so it runs
    // on a worker thread and the window stays responsive. The worker only
    // touches its own copies.
    auto result =
        std::make_shared<KnotMath::UnknottingAnimation>();

    QThread *worker =
        QThread::create(
            [curve, requestedRadius, result]()
            {
                *result =
                    KnotMath::UnknottingAnimation::build(
                        curve,
                        requestedRadius,
                        240);
            });

    connect(
        worker,
        &QThread::finished,
        this,
        [this, worker, result, buildId, requestedRadius]()
        {
            worker->deleteLater();

            finishUnknotting(
                buildId,
                *result,
                requestedRadius);
        });

    worker->start();
}

void MainWindow::finishUnknotting(
    quint64 buildId,
    const KnotMath::UnknottingAnimation &animation,
    double requestedRadius)
{
    // A reset or a new curve made this result obsolete.
    if (buildId != unknotBuildId_ ||
        !unknotPreparing_)
    {
        return;
    }

    unknotPreparing_ =
        false;

    if (!animation.isValid())
    {
        unknotStatusLabel_->setText(
            tr("Could not build an unknotting animation "
               "for this curve."));

        setUnknotControlsEnabled(
            false);

        return;
    }

    unknotAnimation_ =
        animation;

    unknotFrameHint_ =
        KnotMath::TubeFrameHint();

    unknotDiagram_ =
        animation.diagram();

    // timeline: ticks where a stage starts, dots at the crossing changes;
    // the step-by-step mode pauses where a stage or a change starts
    QVector<TimelineSlider::Mark> marks;

    unknotStops_.clear();

    for (const KnotMath::UnknottingAnimation::TimelineMark &mark :
         animation.timeline())
    {
        if (mark.time > 0.0)
            unknotStops_.push_back(mark.time);

        if (mark.change == 0 &&
            mark.time > 0.0 &&
            mark.time < 1.0)
        {
            marks.push_back({mark.time, false});
        }
    }

    for (const KnotMath::UnknottingEvent &event :
         animation.events())
    {
        marks.push_back({event.time, true});
    }

    unknotScrubSlider_->setMarks(
        marks);

    unknotRadius_ =
        std::min(
            requestedRadius,
            unknotAnimation_.suggestedTubeRadius());

    unknotRequestedRadius_ =
        requestedRadius;

    unknotFlattenStart_ =
        1.0;

    for (const KnotMath::UnknottingAnimation::TimelineMark &mark :
         animation.timeline())
    {
        if (mark.stage == KnotMath::UnknottingAnimation::Stage::Flatten)
            unknotFlattenStart_ = mark.time;
    }

    unknotCelebrate_ =
        -1.0;

    // the summary of an earlier animation no longer fits
    if (unknotSummary_ != nullptr)
        unknotSummary_->close();

    const KnotMath::Vec3 lo =
        unknotAnimation_.boundsMin();

    const KnotMath::Vec3 hi =
        unknotAnimation_.boundsMax();

    const float margin =
        static_cast<float>(
            std::max(
                unknotRadius_,
                requestedRadius));

    glWidget_->setFixedBounds(
        QVector3D(
            static_cast<float>(lo.x) - margin,
            static_cast<float>(lo.y) - margin,
            static_cast<float>(lo.z) - margin),
        QVector3D(
            static_cast<float>(hi.x) + margin,
            static_cast<float>(hi.y) + margin,
            static_cast<float>(hi.z) + margin));

    // Numbered crossing markers belong to the original diagram; during the
    // animation the flashes carry the numbers instead.
    glWidget_->setShowCrossingNumbers(
        false);

    unknotActive_ =
        true;

    unknotT_ =
        0.0;

    // Photo mode: the rope of the scene is lifted off the table
    startPhotoUnknot();

    // The user pressed play to get here.
    unknotPlaying_ =
        true;

    unknotClock_.restart();

    unknotTimer_->start();

    setUnknotControlsEnabled(
        true);

    renderUnknotFrame();
}

void MainWindow::stopUnknotting(
    bool restoreMesh)
{
    // An animation that is still being built is no longer wanted.
    ++unknotBuildId_;

    const bool wasPreparing =
        unknotPreparing_;

    unknotPreparing_ =
        false;

    unknotTimer_->stop();

    unknotPlaying_ =
        false;

    unknotCelebrate_ =
        -1.0;

    if (unknotSummary_ != nullptr)
        unknotSummary_->close();

    const bool wasActive =
        unknotActive_;

    unknotActive_ =
        false;

    const bool wasPhoto =
        photoUnknot_;

    stopPhotoUnknot();

    unknotAnimation_ =
        KnotMath::UnknottingAnimation();

    unknotStops_.clear();

    unknotScrubSlider_->clearMarks();

    glWidget_->clearDiagramInset();

    unknotScrubSlider_->blockSignals(
        true);

    unknotScrubSlider_->setValue(
        0);

    unknotScrubSlider_->blockSignals(
        false);

    setUnknotControlsEnabled(
        false);

    if (!wasActive)
    {
        if (wasPreparing)
            unknotStatusLabel_->clear();

        return;
    }

    glWidget_->clearFlashes();

    glWidget_->clearFixedBounds();

    glWidget_->setShowCrossingNumbers(
        showCrossingNumbersCheck_ != nullptr &&
        showCrossingNumbersCheck_->isChecked());

    unknotStatusLabel_->clear();

    if (restoreMesh)
        rebuildMesh();

    // the rope lies on the table again: the usual view of it
    if (wasPhoto && restoreMesh)
        glWidget_->resetCamera();
}

void MainWindow::unknotPlayPause()
{
    if (unknotPreparing_)
        return;

    if (!unknotActive_)
    {
        // Playback starts in finishUnknotting() once the animation is ready.
        startUnknotting();
        return;
    }

    if (unknotPlaying_)
    {
        unknotPlaying_ =
            false;

        unknotTimer_->stop();

        setUnknotControlsEnabled(
            false);

        return;
    }

    if (unknotT_ >= 1.0)
    {
        unknotT_ =
            0.0;

        // the loop that fell onto the table goes back up
        unknotDrop_.reset();
        unknotLanded_.clear();
    }

    unknotCelebrate_ =
        -1.0;

    unknotPlaying_ =
        true;

    unknotClock_.restart();

    unknotTimer_->start();

    setUnknotControlsEnabled(
        true);

    renderUnknotFrame();
}

void MainWindow::unknotReset()
{
    stopUnknotting(
        true);
}

namespace
{

// Photo mode: seconds to lift the rope off the table before unknotting
constexpr double UnknotLiftSeconds = 2.4;

/*
 * A rope keeps its length: the unknotting animation draws its curve
 * shorter as the knot comes undone (it rounds and shrinks it), so in
 * Photo mode every frame is scaled about its middle back to the rope's
 * `length` (closed curve). `middle` and `scale` tell how, for the points
 * that go with the curve (the flashes).
 */
QVector<KnotMath::Vec3> keepLength(
    const QVector<KnotMath::Vec3> &curve,
    double length,
    KnotMath::Vec3 *middle = nullptr,
    double *scale = nullptr)
{
    const int n = curve.size();

    KnotMath::Vec3 centre;
    double now = 0.0;

    for (int i = 0; i < n; ++i)
    {
        centre += curve.at(i);
        now += (curve.at((i + 1) % n) - curve.at(i)).length();
    }

    centre = centre / std::max(1, n);

    const double factor = now > 1.0e-9 && length > 0.0 ? length / now : 1.0;

    if (middle != nullptr)
        *middle = centre;

    if (scale != nullptr)
        *scale = factor;

    QVector<KnotMath::Vec3> scaled;

    scaled.reserve(n);

    for (const KnotMath::Vec3 &p : curve)
        scaled.push_back(centre + (p - centre) * factor);

    return scaled;
}

} // namespace

void MainWindow::unknotTick()
{
    if (!unknotActive_)
        return;

    // a short celebration at the end, then the list of all the changes
    if (unknotCelebrate_ >= 0.0)
    {
        constexpr double celebrateSeconds =
            1.8;

        unknotCelebrate_ +=
            static_cast<double>(
                unknotClock_.restart()) /
            1000.0 /
            celebrateSeconds;

        if (unknotCelebrate_ >= 1.0)
        {
            unknotCelebrate_ =
                -1.0;

            unknotTimer_->stop();

            renderUnknotFrame();

            showUnknotSummary();

            return;
        }

        renderUnknotFrame();

        return;
    }

    // Photo mode: the finished loop falls onto the table, then the
    // celebration
    if (photoUnknot_ && unknotDrop_ != nullptr)
    {
        const double seconds =
            std::min(static_cast<double>(unknotClock_.restart()) / 1000.0, 0.1);

        if (!advanceUnknotDrop(seconds))
        {
            unknotCelebrate_ = 0.0;

            // a closer look at the loop where it lies
            KnotMath::Vec3 low(1.0e30, 1.0e30, 1.0e30);
            KnotMath::Vec3 high(-1.0e30, -1.0e30, -1.0e30);

            for (const KnotMath::Vec3 &p : unknotLanded_)
            {
                low = KnotMath::Vec3(std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z));
                high = KnotMath::Vec3(std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z));
            }

            const KnotMath::Vec3 middle = (low + high) * 0.5;

            glWidget_->flyWithSunBehind(
                QVector3D(static_cast<float>(middle.x), static_cast<float>(middle.y), static_cast<float>(middle.z)),
                static_cast<float>(0.6 * (high - low).length()),
                35.0f,
                45.0f);
        }

        renderUnknotFrame();

        return;
    }

    if (!unknotPlaying_)
        return;

    const double dt =
        static_cast<double>(
            unknotClock_.restart()) /
        1000.0;

    // Photo mode: first the rope is lifted off the table
    if (photoUnknot_ && unknotLift_ < 1.0)
    {
        unknotLift_ =
            std::min(1.0, unknotLift_ + dt / UnknotLiftSeconds);

        renderUnknotFrame();

        return;
    }

    const double speed =
        unknotSpeedCombo_->currentData().toDouble();

    const double total =
        std::max(
            0.5,
            unknotAnimation_.totalSeconds());

    const double previousT =
        unknotT_;

    unknotT_ +=
        dt * speed / total;

    // Step by step: stop at the start of the next stage or crossing change
    // and wait for "Next".
    if (unknotStepCheck_ != nullptr &&
        unknotStepCheck_->isChecked())
    {
        for (const double stop : unknotStops_)
        {
            if (stop > previousT &&
                stop <= unknotT_ &&
                stop < 1.0)
            {
                unknotT_ =
                    stop;

                unknotPlaying_ =
                    false;

                unknotTimer_->stop();

                setUnknotControlsEnabled(
                    false);

                renderUnknotFrame();

                return;
            }
        }
    }

    if (unknotT_ >= 1.0)
    {
        unknotT_ =
            1.0;

        unknotPlaying_ =
            false;

        // the timer keeps running for the celebration (in Photo mode for
        // the loop falling onto the table first)
        if (photoUnknot_)
            startUnknotDrop();

        unknotCelebrate_ =
            photoUnknot_ && unknotDrop_ != nullptr ? -1.0 : 0.0;

        setUnknotControlsEnabled(
            false);
    }

    renderUnknotFrame();
}

void MainWindow::unknotScrubbed(
    int value)
{
    if (!unknotActive_)
        return;

    // Dragging the bar pauses playback and shows exactly that moment.
    if (unknotCelebrate_ >= 0.0)
    {
        unknotCelebrate_ =
            -1.0;

        unknotTimer_->stop();
    }

    if (unknotPlaying_)
    {
        unknotPlaying_ =
            false;

        unknotTimer_->stop();

        setUnknotControlsEnabled(
            false);
    }

    unknotT_ =
        static_cast<double>(value) /
        1000.0;

    // Photo mode: the moment in the air
    unknotLift_ = 1.0;
    unknotDrop_.reset();
    unknotLanded_.clear();

    renderUnknotFrame();
}

void MainWindow::renderUnknotFrame()
{
    if (!unknotActive_)
        return;

    QVector<float> highlight;

    KnotMath::UnknottingAnimation::Stage stage =
        KnotMath::UnknottingAnimation::Stage::Align;

    int changeNumber =
        0;

    QVector<KnotMath::Vec3> curve =
        unknotAnimation_.curveAt(
            unknotT_,
            &highlight,
            &stage,
            &changeNumber);

    using Stage =
        KnotMath::UnknottingAnimation::Stage;

    // While the circle is laid flat its strands are far apart, so the tube
    // grows back to the thickness that was asked for (it may have been
    // thinner to keep tight strands from touching).
    double radius =
        unknotRadius_;

    if (stage == Stage::Flatten ||
        stage == Stage::Done)
    {
        const double e =
            std::clamp(
                (unknotT_ - unknotFlattenStart_) /
                    std::max(1.0e-9, 1.0 - unknotFlattenStart_),
                0.0,
                1.0);

        const double grown =
            std::max(
                unknotRadius_,
                std::min(
                    unknotRequestedRadius_,
                    0.3 * unknotAnimation_.finalRingRadius()));

        radius +=
            (grown - unknotRadius_) *
            e * e * (3.0 - 2.0 * e);
    }

    // centre of the curve (of the final circle for the celebration)
    KnotMath::Vec3 centre;

    for (const KnotMath::Vec3 &p : curve)
        centre += p;

    centre =
        centre /
        std::max(
            1.0,
            static_cast<double>(
                curve.size()));

    // Celebration: the circle "pops" once and glows green.
    double glow =
        0.0;

    if (unknotCelebrate_ >= 0.0)
    {
        const double c =
            std::clamp(
                unknotCelebrate_,
                0.0,
                1.0);

        const double pop =
            1.0 +
            0.06 *
                std::sin(3.14159265358979 * std::min(1.0, 1.6 * c)) *
                (1.0 - c);

        for (KnotMath::Vec3 &p : curve)
            p = centre + (p - centre) * pop;

        glow =
            0.7 *
            std::sin(3.14159265358979 * c);

        highlight.fill(
            static_cast<float>(glow),
            curve.size());
    }

    // Drawn along a smooth spline with twice the points. The factor is
    // fixed so that every frame has the same rings (smooth frame hint).
    constexpr int refine =
        2;

    /*
     * Photo mode: the rope where it is: rising from the table (the rope
     * as it lay, blended into the knot in the air), in the air, or fallen
     * back onto the table at the end.
     */
    KnotMath::Vec3 air;
    KnotMath::Vec3 flashMiddle;
    double flashScale = 1.0;

    if (photoUnknot_)
    {
        air = unknotAir_;

        // as long as the rope (the animation shrinks it as it goes)
        const QVector<KnotMath::Vec3> full =
            keepLength(curve, unknotRopeLength_, &flashMiddle, &flashScale);

        QVector<KnotMath::Vec3> rope;

        if (!unknotLanded_.isEmpty())
        {
            rope = unknotLanded_;
        }
        else if (unknotLift_ < 1.0)
        {
            const QVector<KnotMath::Vec3> from =
                KnotMath::resampleClosedCurve(unknotLiftFrom_, full.size(), true);

            const double e = unknotLift_ * unknotLift_ * (3.0 - 2.0 * unknotLift_);

            for (int i = 0; i < full.size(); ++i)
                rope.push_back(from.value(i, full.at(i)) * (1.0 - e) + (full.at(i) + air) * e);

            air = air * e;
        }
        else
        {
            for (const KnotMath::Vec3 &p : full)
                rope.push_back(p + air);
        }

        if (!unknotLanded_.isEmpty())
        {
            centre = KnotMath::Vec3();

            for (const KnotMath::Vec3 &p : rope)
                centre += p;

            centre = centre / std::max(1.0, static_cast<double>(rope.size()));
            air = KnotMath::Vec3();
        }

        // the second camera shot for the opened loop (and back, when the
        // bar is dragged back)
        if (unknotLanded_.isEmpty() && unknotLift_ >= 1.0)
        {
            const bool wide = unknotT_ >= unknotWideFrom_;

            if (wide != unknotWideShot_)
            {
                unknotWideShot_ = wide;

                glWidget_->flyWithSunBehind(
                    wide ? unknotWideCentre_ : unknotCloseCentre_,
                    wide ? unknotWideRadius_ : unknotCloseRadius_,
                    35.0f,
                    22.0f);
            }
        }

        showPhotoUnknotRope(
            rope,
            highlight,
            curve.size(),
            glow > 0.0 ? QColor(108, 192, 139) : QColor(91, 155, 255),
            QColor(217, 155, 66));
    }

    KnotMath::Mesh mesh =
        photoUnknot_
            ? KnotMath::Mesh()
            : KnotMath::buildTubeMesh(
            KnotMath::refineCurve(
                curve,
                refine,
                true),
            radius,
            32,
            true,
            KnotMath::ColorMode::SingleColor,
            2,
            &unknotFrameHint_);

    if (mesh.isValid() && !photoUnknot_)
    {
        KnotMath::computeAmbientOcclusion(
            mesh,
            0.7);

        // blue: the strand that was on top, amber: the one below
        // (the same colours as in the diagram in the corner); green: the
        // celebration at the end
        KnotMath::colorMeshByStrandHighlight(
            mesh,
            meshColor_,
            glow > 0.0
                ? QColor(108, 192, 139)
                : QColor(91, 155, 255),
            QColor(217, 155, 66),
            KnotMath::refineValues(
                highlight,
                refine,
                true));

        glWidget_->setMesh(
            mesh);
    }

    // Flash rings: each crossing change flashes for about 0.9 s.
    const double total =
        unknotAnimation_.totalSeconds();

    QVector<KnotMath::Vec3> positions;
    QVector<float> progress;
    QVector<int> numbers;

    for (const KnotMath::UnknottingEvent &event :
         unknotAnimation_.events())
    {
        const double p =
            (unknotT_ - event.time) *
            total /
            0.9;

        if (p > 0.0 &&
            p < 1.0)
        {
            positions.push_back(
                flashMiddle + (event.position - flashMiddle) * flashScale + air);

            progress.push_back(
                static_cast<float>(p));

            numbers.push_back(
                event.number);
        }
    }

    // one big flash in the middle of the finished circle
    if (unknotCelebrate_ >= 0.0)
    {
        positions.push_back(
            centre);

        progress.push_back(
            static_cast<float>(
                std::clamp(
                    unknotCelebrate_ / 0.8,
                    0.0,
                    0.999)));

        numbers.push_back(
            0);
    }

    glWidget_->setFlashes(
        positions,
        progress,
        numbers);

    const int changes =
        unknotAnimation_.crossingChanges();

    QString text;

    // A title and a short explanation for every stage (in Photo mode
    // also as a caption on the picture).
    QString captionTitle;
    QString captionBody;

    auto explain = [&captionTitle, &captionBody](const QString &title, const QString &body)
    {
        captionTitle = title;
        captionBody = body;

        return QStringLiteral("<b>%1</b><br>%2").arg(title, body);
    };

    switch (stage)
    {
    case Stage::Align:
        text = explain(
            tr("Choosing a viewpoint"),
            tr("The knot turns until we look at it from the direction "
               "where its picture (the knot diagram, lower left) needs "
               "the fewest crossing changes."));
        break;

    case Stage::Layer:
        text = explain(
            tr("A knot diagram"),
            tr("At every crossing one strand runs over and the other "
               "under. The knot is pressed into two thin layers so "
               "that this is easy to see."));
        break;

    case Stage::Change:
        text = explain(
            tr("Crossing change %1 of %2")
                .arg(std::max(1, changeNumber))
                .arg(changes),
            tr("The <span style='color:#5B9BFF;'>blue</span> strand "
               "(on top) and the <span style='color:#D99B42;'>amber"
               "</span> strand (below) pass through each other. Real "
               "rope cannot do this: it is the only move that can "
               "change the knot. The fewest such moves that undo a knot "
               "is its unknotting number."));
        break;

    case Stage::Settle:
        text = explain(
            tr("Downhill all the way"),
            tr("Walking from the white dot in the direction of the "
               "arrow, every crossing is now reached first on the "
               "upper strand. A diagram like this (\"descending\") is "
               "always an unknot."));
        break;

    case Stage::Untangle:
        text = explain(
            tr("Pulling it open"),
            tr("Because the curve only goes downhill, it can be lifted "
               "apart without any strand passing through another."));
        break;

    case Stage::Flatten:
        text = explain(
            tr("A plain circle"),
            tr("The loop is rounded and laid flat: the unknot."));
        break;

    case Stage::Done:
    default:
        if (changes == 0)
        {
            text = tr("Unknotted without any crossing change: "
                      "this curve is an unknot.");
        }
        else
        {
            text = tr("Unknotted with %1 crossing change(s). "
                      "This is an upper bound for the "
                      "unknotting number.")
                       .arg(changes);

            const int known =
                KnotMath::knownUnknottingNumber(
                    currentPreset(),
                    torusPSpin_->value(),
                    torusQSpin_->value());

            if (known >= 0)
            {
                text += QLatin1Char('\n') +
                        tr("Known unknotting number: %1.")
                            .arg(known);
            }
            else if (currentData_.tableIndex >= 0)
            {
                // from the knot table; a range such as [1,2] where it is
                // not known exactly
                const QString tableValue =
                    KnotMath::KnotTable::instance()
                        .at(currentData_.tableIndex)
                        .unknotting;

                if (!tableValue.isEmpty())
                {
                    text += QLatin1Char('\n') +
                            tr("Known unknotting number: %1.")
                                .arg(tableValue);
                }
            }
        }

        text = explain(
            tr("Unknotted"),
            text.toHtmlEscaped().replace(
                QLatin1Char('\n'),
                QStringLiteral("<br>")) +
                QStringLiteral("<br>") +
                tr("Press <b>Steps</b> to go through every crossing change "
                   "again."));
        break;
    }

    if (!unknotAnimation_.isVerified())
    {
        text += QStringLiteral("<br>") +
                tr("(Warning: this curve is very tight, tubes may "
                   "overlap.)");
    }

    // Photo mode: what the rope does before and after the animation
    if (photoUnknot_)
    {
        if (unknotLift_ < 1.0)
        {
            text = explain(
                tr("Lifting the rope"),
                tr("The knot is lifted off the table and turned towards "
                   "us, so every crossing can be seen. The picture in the "
                   "corner is its diagram: the knot seen from above, a "
                   "gap where a strand passes under another."));
        }
        else if (unknotDrop_ != nullptr)
        {
            text = explain(
                tr("Back on the table"),
                tr("Let go, the loop falls by its own weight. However it "
                   "lands, it can be laid out as a plain ring without "
                   "cutting it: it is the unknot."));
        }
    }

    unknotStatusLabel_->setText(
        Theme::html(text));

    if (photoUnknot_)
        glWidget_->setCaption(captionTitle, captionBody);

    // 2D diagram in the corner, up to the end of the "downhill" stage
    // (after that the curve no longer has this diagram).
    if (stage == Stage::Align ||
        stage == Stage::Layer ||
        stage == Stage::Change ||
        stage == Stage::Settle)
    {
        // past the pass-through moment the other strand is on top
        int changesDone =
            0;

        for (const KnotMath::UnknottingEvent &event :
             unknotAnimation_.events())
        {
            if (unknotT_ >= event.time)
                ++changesDone;
        }

        KnotGLWidget::DiagramInset inset =
            KnotGLWidget::DiagramInset::fromUnknotting(
                unknotDiagram_,
                changesDone,
                stage == Stage::Change ? changeNumber : 0);

        inset.caption = tr("KNOT DIAGRAM");

        glWidget_->setDiagramInset(
            inset);
    }
    else
    {
        glWidget_->clearDiagramInset();
    }

    unknotScrubSlider_->blockSignals(
        true);

    unknotScrubSlider_->setValue(
        static_cast<int>(
            std::lround(unknotT_ * 1000.0)));

    unknotScrubSlider_->blockSignals(
        false);
}


void MainWindow::showUnknotSummary()
{
    if (!unknotActive_)
        return;

    if (unknotSummary_ != nullptr)
    {
        unknotSummary_->raise();
        unknotSummary_->activateWindow();

        return;
    }

    using Stage =
        KnotMath::UnknottingAnimation::Stage;

    UnknotSummaryDialog::Summary summary;

    summary.diagram =
        unknotDiagram_;

    // moments to show in 3D: the layered diagram just before the first
    // change, every pass-through and the finished descending diagram
    for (const KnotMath::UnknottingAnimation::TimelineMark &mark :
         unknotAnimation_.timeline())
    {
        if (mark.stage == Stage::Settle)
            summary.startTime = mark.time;

        if (mark.stage == Stage::Untangle)
            summary.resultTime = std::max(0.0, mark.time - 0.002);
    }

    for (const KnotMath::UnknottingAnimation::TimelineMark &mark :
         unknotAnimation_.timeline())
    {
        if (mark.stage == Stage::Change)
        {
            summary.startTime = mark.time;
            break;
        }
    }

    for (const KnotMath::UnknottingEvent &event :
         unknotAnimation_.events())
    {
        summary.changeTimes.push_back(
            event.time);
    }

    const int known =
        KnotMath::knownUnknottingNumber(
            currentPreset(),
            torusPSpin_->value(),
            torusQSpin_->value());

    if (known >= 0)
    {
        summary.known =
            QString::number(known);
    }
    else if (currentData_.tableIndex >= 0)
    {
        summary.known =
            KnotMath::KnotTable::instance()
                .at(currentData_.tableIndex)
                .unknotting;
    }

    // a curve that was relaxed or scrambled is still the same knot
    switch (currentPreset())
    {
    case KnotMath::Preset::CustomCoordinates:
        break;

    case KnotMath::Preset::KnotTable:
        summary.knotName =
            tableKnotName_;
        break;

    default:
        summary.knotName =
            KnotMath::presetDisplayName(
                currentPreset());
        break;
    }

    UnknotSummaryDialog *dialog =
        new UnknotSummaryDialog(
            summary,
            this);

    dialog->setAttribute(
        Qt::WA_DeleteOnClose);

    connect(
        dialog,
        &UnknotSummaryDialog::showMoment,
        this,
        &MainWindow::showUnknotMoment);

    unknotSummary_ =
        dialog;

    // not modal: the 3D view stays usable next to it
    dialog->show();
}

void MainWindow::showUnknotMoment(
    double t)
{
    if (!unknotActive_)
        return;

    unknotCelebrate_ =
        -1.0;

    unknotPlaying_ =
        false;

    unknotTimer_->stop();

    unknotT_ =
        std::clamp(
            t,
            0.0,
            1.0);

    setUnknotControlsEnabled(
        false);

    renderUnknotFrame();
}

void MainWindow::drawKnotDialog()
{
    DiagramEditorDialog dialog(
        this);

    if (dialog.exec() != QDialog::Accepted ||
        !dialog.diagram().isValid())
    {
        return;
    }

    const QVector<KnotMath::Vec3> curve =
        KnotMath::diagramToCurve(
            dialog.diagram(),
            0.30);

    if (curve.size() < 8)
        return;

    // The drawn knot is handed to the normal "Custom Curve" path as a list
    // of coordinates, so every analysis and the unknotting animation work
    // on it, and the text can be edited or saved.
    const int customIndex =
        presetCombo_->findData(
            static_cast<int>(
                KnotMath::Preset::CustomCoordinates));

    if (customIndex >= 0)
    {
        presetCombo_->setCurrentIndex(
            customIndex);
    }

    customInputEdit_->setPlainText(
        KnotMath::diagramToCoordinateText(
            curve));

    generateMesh();
}

/*
 * =============================================================
 * FILES AND EXPORTS
 * =============================================================
 */

namespace
{

// Asks for a file name to write; adds the extension of the chosen filter
// if the name has none. Empty if cancelled.
QString askSavePath(
    QWidget *parent,
    const QString &caption,
    const QString &baseName,
    const QStringList &filters)
{
    QString selectedFilter =
        filters.first();

    QString path =
        QFileDialog::getSaveFileName(
            parent,
            caption,
            AppSettings::lastFolder() + QLatin1Char('/') +
                KnotExport::safeFileName(baseName),
            filters.join(QStringLiteral(";;")),
            &selectedFilter);

    if (path.isEmpty())
        return path;

    if (QFileInfo(path).suffix().isEmpty())
    {
        // "PNG image (*.png)" -> ".png"
        static const QRegularExpression firstPattern(
            QStringLiteral("\\*(\\.[A-Za-z0-9]+)"));

        const QRegularExpressionMatch match =
            firstPattern.match(selectedFilter);

        if (match.hasMatch())
            path += match.captured(1);
    }

    AppSettings::setLastFolder(
        QFileInfo(path).absolutePath());

    return path;
}

bool writeTextFile(
    const QString &path,
    const QString &text,
    QString &error)
{
    QSaveFile file(path);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        error = file.errorString();
        return false;
    }

    file.write(text.toUtf8());

    if (!file.commit())
    {
        error = file.errorString();
        return false;
    }

    return true;
}

} // namespace

void MainWindow::openKnotFile()
{
    const QString path =
        QFileDialog::getOpenFileName(
            this,
            tr("Open knot"),
            AppSettings::lastFolder(),
            tr("Knots (*.aknot *.txt *.csv);;AKSIOMA knot files (*.aknot);;"
               "Coordinate lists (*.txt *.csv);;All files (*)"));

    if (path.isEmpty())
        return;

    openKnotFilePath(
        path);
}

void MainWindow::openKnotFilePath(
    const QString &path)
{
    AppSettings::setLastFolder(
        QFileInfo(path).absolutePath());

    loadKnotFile(path, true);
}

void MainWindow::loadKnotFile(
    const QString &path,
    bool remembered)
{
    KnotExport::KnotFile file;
    QString error;

    if (!KnotExport::readKnotFile(path, file, error))
    {
        showNotice(
            tr("The knot could not be opened: %1 Choose an .aknot file or a "
               "text file of coordinates.").arg(error),
            Notice::Error);

        return;
    }

    const int presetIndex =
        presetCombo_->findData(
            static_cast<int>(
                file.preset));

    if (presetIndex < 0)
        return;

    // appearance first; a new material also sets its own shine
    if (file.hasAppearance)
    {
        if (file.thickness > 0)
            thicknessSlider_->setValue(file.thickness);

        if (file.samples > 0)
            meshDensitySlider_->setValue(file.samples);

        if (file.material >= 0 && file.material < materialCombo_->count())
            materialCombo_->setCurrentIndex(file.material);

        metallicSlider_->setValue(file.shine);

        if (file.colorMode >= 0 && file.colorMode < colorModeCombo_->count())
            colorModeCombo_->setCurrentIndex(file.colorMode);

        if (file.color.isValid())
        {
            meshColor_ = file.color;

            updateColorSwatch();

            glWidget_->setMeshColor(meshColor_);
        }
    }

    torusPSpin_->setValue(file.torusP);
    torusQSpin_->setValue(file.torusQ);

    if (file.preset == KnotMath::Preset::KnotTable)
        tableKnotName_ = file.tableKnot;

    if (file.preset == KnotMath::Preset::CustomCoordinates)
        customInputEdit_->setPlainText(file.customText);

    {
        const QSignalBlocker blocker(presetCombo_);

        presetCombo_->setCurrentIndex(presetIndex);
    }

    lastPresetIndex_ = presetIndex;

    updateCustomInputVisibility();

    // one rebuild instead of one for every value set above
    rebuildTimer_->stop();
    pendingFullRegenerate_ = false;

    generateMesh();

    updateLearnSection();

    resetCamera();

    // this is what the file holds (or, restored, what is yet to be saved)
    historyTimer_->stop();

    history_.record(currentState());

    if (remembered)
    {
        history_.markSaved();

        AppSettings::addRecentFile(path);
    }

    updateUndoControls();

    writeAutosave();
}

void MainWindow::saveKnotFile()
{
    saveKnot();
}

bool MainWindow::saveKnot()
{
    if (!hasData_)
        return false;

    // a moving curve is kept where it is
    pauseRelaxation();

    const QString path =
        askSavePath(
            this,
            tr("Save knot"),
            currentData_.name,
            {tr("AKSIOMA knot file (*.aknot)")});

    if (path.isEmpty())
        return false;

    QString error;

    if (!KnotExport::writeKnotFile(path, currentKnotFile(), error))
    {
        showNotice(
            tr("The knot could not be saved: %1 Try another folder or file name.").arg(error),
            Notice::Error);

        return false;
    }

    historyTimer_->stop();

    history_.record(currentState());
    history_.markSaved();

    AppSettings::addRecentFile(path);

    updateUndoControls();

    writeAutosave();

    showSaved(path);

    return true;
}

void MainWindow::exportViewImage()
{
    if (glWidget_ == nullptr || !hasData_)
        return;

    // taken now, before the dialog covers the view
    const QImage image =
        glWidget_->grabFramebuffer();

    const QString path =
        askSavePath(
            this,
            tr("Export 3D picture"),
            currentData_.name,
            {tr("PNG image (*.png)"), tr("JPEG image (*.jpg)")});

    if (path.isEmpty())
        return;

    // JPEG has no transparency
    const bool jpeg =
        QFileInfo(path).suffix().startsWith(QStringLiteral("jp"), Qt::CaseInsensitive);

    const QImage output =
        jpeg ? image.convertToFormat(QImage::Format_RGB32) : image;

    if (!output.save(path, nullptr, jpeg ? 92 : -1))
    {
        showNotice(
            tr("The picture could not be saved. Try another folder or file name."),
            Notice::Error);

        return;
    }

    showSaved(path);
}

void MainWindow::exportDiagram()
{
    if (!hasData_)
        return;

    pauseRelaxation();

    if (currentData_.multiStrand || !currentData_.closed)
    {
        showNotice(
            tr("A knot diagram needs a single closed curve. Choose a knot "
               "instead of a structure of several strands."),
            Notice::Error);

        return;
    }

    QString error;

    const KnotMath::KnotDiagram diagram =
        KnotMath::diagramFromCurve(
            currentData_.points,
            2.0,
            error);

    if (!diagram.isValid())
    {
        showNotice(
            tr("No knot diagram could be made: %1 Relaxing the knot first "
               "often clears up a crowded curve.").arg(error),
            Notice::Error);

        return;
    }

    const QString path =
        askSavePath(
            this,
            tr("Export knot diagram"),
            currentData_.name,
            {tr("SVG drawing (*.svg)"), tr("PNG image (*.png)")});

    if (path.isEmpty())
        return;

    if (!KnotExport::saveDiagram(diagram, path, currentData_.name, error))
    {
        showNotice(
            tr("The diagram could not be saved: %1 Try another folder or file name.").arg(error),
            Notice::Error);

        return;
    }

    showSaved(path);
}

void MainWindow::exportCoordinates()
{
    if (!hasData_)
        return;

    pauseRelaxation();

    const QString path =
        askSavePath(
            this,
            tr("Export coordinates"),
            currentData_.name,
            {tr("Text, one point per line (*.txt)"), tr("CSV table (*.csv)")});

    if (path.isEmpty())
        return;

    const bool csv =
        QFileInfo(path).suffix().compare(QStringLiteral("csv"), Qt::CaseInsensitive) == 0;

    QString error;

    if (!writeTextFile(path, KnotExport::coordinatesText(currentData_, csv), error))
    {
        showNotice(
            tr("The coordinates could not be saved: %1 Try another folder or file name.").arg(error),
            Notice::Error);

        return;
    }

    showSaved(path);
}

void MainWindow::exportReport()
{
    if (!hasData_)
        return;

    pauseRelaxation();

    KnotExport::ReportData report;

    report.name = currentData_.name;
    report.invariants = currentInvariants_;
    report.pointCount = currentData_.points.size();
    report.shapeChanged = relaxChanged_;

    if (!currentData_.multiStrand && currentData_.closed)
    {
        QString error;

        const KnotMath::KnotDiagram diagram =
            KnotMath::diagramFromCurve(
                currentData_.points,
                2.0,
                error);

        if (diagram.isValid())
        {
            const KnotMath::DiagramArcs arcs =
                KnotMath::diagramArcs(diagram);

            report.diagramCrossings = diagram.crossings.size();
            report.determinant = KnotMath::diagramDeterminant(arcs);
            report.tricolourable = KnotMath::coloringDimension(arcs) >= 2;
        }
    }

    const QString path =
        askSavePath(
            this,
            tr("Export analysis report"),
            currentData_.name,
            {tr("Text (*.txt)")});

    if (path.isEmpty())
        return;

    QString error;

    if (!writeTextFile(path, KnotExport::reportText(report), error))
    {
        showNotice(
            tr("The report could not be saved: %1 Try another folder or file name.").arg(error),
            Notice::Error);

        return;
    }

    showSaved(path);
}

/*
 * =============================================================
 * SEIFERT SURFACE
 * =============================================================
 */

namespace
{

// the two sides of the surface
const QColor SeifertFront(217, 155, 66);
const QColor SeifertBack(64, 118, 190);

// the two loops of a matrix entry: on the surface and lifted off it (the
// texts use the same colours, #46CD78 and #E150C8)
const QColor SeifertLoopColor(70, 205, 120);
const QColor SeifertLiftedColor(225, 80, 200);

} // namespace

void MainWindow::toggleSeifert()
{
    if (seifertActive_)
    {
        stopSeifert();
        return;
    }

    if (!hasData_)
        return;

    stopJoin();

    if (currentData_.multiStrand || !currentData_.closed)
    {
        seifertInfoLabel_->setText(
            tr("A Seifert surface is built here for a single closed knot. "
               "Choose a knot preset, a table knot or a custom curve."));

        return;
    }

    // the surface of the curve as it is now
    pauseRelaxation();

    if (unknotActive_ || unknotPreparing_)
        stopUnknotting(false);

    seifertActive_ = true;
    seifertDirty_ = true;

    seifertButton_->setText(tr("BACK TO THE KNOT"));
    Icons::set(seifertButton_, QStringLiteral("stop"));
    seifertStepSlider_->setEnabled(true);

    setSeifertStep(0);

    if (!seifertActive_)
        return;

    resetCamera();

    // then the steps one after the other
    seifertTimer_->start();
}

void MainWindow::stopSeifert()
{
    if (!seifertActive_)
        return;

    seifertActive_ = false;

    seifertTimer_->stop();

    seifertButton_->setText(tr("BUILD SURFACE"));
    Icons::set(seifertButton_, QStringLiteral("play"));
    seifertStepSlider_->setEnabled(false);

    // the matrix stays, without its links and loops
    seifertLoop_ = -1;
    seifertLifted_ = -1;

    if (!seifertDirty_ && seifert_.valid)
        updateSeifertInfo();

    // the knot again
    rebuildMesh();
}

void MainWindow::setSeifertStep(
    int step)
{
    seifertStep_ = std::clamp(step, 0, 3);

    // the loops of the matrix are shown on the finished surface only
    if (seifertStep_ < 3 && seifertLoop_ >= 0)
    {
        seifertLoop_ = -1;
        seifertLifted_ = -1;

        if (seifertActive_ && !seifertDirty_)
            updateSeifertInfo();
    }

    {
        const QSignalBlocker blocker(seifertStepSlider_);

        seifertStepSlider_->setValue(seifertStep_);
    }

    switch (seifertStep_)
    {
    case 0:
        seifertStepTitle_->setText(tr("1 / 4   THE KNOT DIAGRAM"));
        seifertStepText_->setText(
            tr("The knot is laid flat: its diagram, from the direction with the "
               "fewest crossings. The knot has a direction, and every crossing is "
               "kept as it is."));
        break;

    case 1:
        seifertStepTitle_->setText(tr("2 / 4   SEIFERT CIRCLES"));
        seifertStepText_->setText(
            tr("Walk along the knot and, at every crossing, turn onto the other "
               "strand instead of crossing it. The diagram falls apart into loops "
               "that never cross: the Seifert circles. A circle inside another "
               "one is lifted higher."));
        break;

    case 2:
        seifertStepTitle_->setText(tr("3 / 4   DISKS"));
        seifertStepText_->setText(
            tr("Every circle is filled with a disk. The two sides get different "
               "colours: amber is the side from which the circle runs "
               "anticlockwise, blue the other side."));
        break;

    default:
        seifertStepTitle_->setText(tr("4 / 4   TWISTED BANDS"));
        seifertStepText_->setText(
            tr("At every crossing the two disks are joined by a band with a half "
               "twist, turned like the crossing. The edge of the surface is the "
               "knot again. Amber always meets amber: the surface has two sides "
               "(it is orientable)."));
        break;
    }

    if (seifertActive_)
        renderSeifert();
}

void MainWindow::renderSeifert()
{
    if (!seifertActive_ || glWidget_ == nullptr || !hasData_)
        return;

    if (seifertDirty_)
    {
        seifertDirty_ = false;

        // a new knot that has no surface here (several components)
        if (currentData_.multiStrand || !currentData_.closed)
        {
            seifert_ = KnotMath::SeifertSurface();

            stopSeifert();

            seifertInfoLabel_->setText(
                tr("A Seifert surface is built here for a single closed knot. "
                   "Choose a knot preset, a table knot or a custom curve."));

            return;
        }

        seifert_ = KnotMath::buildSeifertSurface(currentData_.points, true);

        seifertLoop_ = -1;
        seifertLifted_ = -1;

        updateSeifertInfo();
    }

    if (!seifert_.valid)
    {
        stopSeifert();
        return;
    }

    const double radius =
        KnotMath::seifertTubeRadius(
            seifert_,
            static_cast<double>(thicknessSlider_->value()) / 100.0);

    KnotMath::SeifertHighlight highlight;

    highlight.loop = seifertLoop_;
    highlight.lifted = seifertLifted_;
    highlight.loopColor = SeifertLoopColor;
    highlight.liftedColor = SeifertLiftedColor;

    const KnotMath::Mesh mesh =
        KnotMath::seifertMesh(
            seifert_,
            seifertStep_,
            radius,
            meshColor_,
            SeifertFront,
            SeifertBack,
            highlight);

    if (!mesh.isValid())
        return;

    glWidget_->setShowCrossingNumbers(false);
    glWidget_->setCrossings({});
    glWidget_->setMesh(mesh);
    glWidget_->setMeshColor(meshColor_);

    applySurface();
}

void MainWindow::updateSeifertInfo()
{
    const KnotMath::SeifertSurface &s = seifert_;

    if (!s.valid)
    {
        seifertInfoLabel_->setText(
            tr("No Seifert surface could be built: %1").arg(s.error.toHtmlEscaped()));

        return;
    }

    QString html;

    html += tr("<b>%1</b> crossings and <b>%2</b> Seifert circles give a surface of "
               "genus <b>g = (%1 − %2 + 1) / 2 = %3</b> (the number of "
               "\"handles\").")
                .arg(s.crossings)
                .arg(s.circles)
                .arg(s.genus);

    if (s.genus == 0)
    {
        html += QStringLiteral("<br><br>") +
                tr("The surface is a disk, so this knot is the unknot.");
    }

    // what is known about the genus of the knot itself
    int knotGenus = -1;

    if (currentData_.tableIndex >= 0 &&
        currentData_.tableIndex < KnotMath::KnotTable::instance().size())
    {
        knotGenus = KnotMath::KnotTable::instance().at(currentData_.tableIndex).genus;
    }

    int lowerBound = -1;

    if (s.matrixValid && !s.alexander.empty())
        lowerBound = (s.alexander.rbegin()->first - s.alexander.begin()->first) / 2;

    if (s.genus > 0)
    {
        html += QStringLiteral("<br><br>");

        if (knotGenus >= 0 && knotGenus == s.genus)
        {
            html += tr("The genus of this knot is %1 (KnotInfo), so this surface is "
                       "as simple as possible.")
                        .arg(knotGenus);
        }
        else if (knotGenus >= 0)
        {
            html += tr("The genus of this knot is %1 (KnotInfo): another diagram gives "
                       "a simpler surface.")
                        .arg(knotGenus);
        }
        else if (lowerBound == s.genus)
        {
            html += tr("The Alexander polynomial shows that every surface of this knot "
                       "has genus at least %1, so this one is as simple as possible: "
                       "the knot has genus %1.")
                        .arg(lowerBound);
        }
        else if (lowerBound >= 0)
        {
            html += tr("The Alexander polynomial shows that every surface of this knot "
                       "has genus at least %1.")
                        .arg(lowerBound);
        }
    }

    if (s.genus > 0 && s.matrixValid)
    {
        const int size = s.seifertMatrix.size();

        html += QStringLiteral("<br><br>") +
                tr("<b>Seifert matrix V</b> (%1 × %1): the linking numbers of %1 loops "
                   "on the surface with the loops lifted off its amber side.")
                    .arg(size);

        // while the surface is shown, every entry is a link "i,j" to its
        // two loops
        const bool clickable =
            seifertActive_ &&
            s.loops.size() == size;

        if (size <= 10)
        {
            QString rows;

            for (int i = 0; i < size; ++i)
            {
                QString line;

                for (int j = 0; j < size; ++j)
                {
                    const QString number = QString::number(s.seifertMatrix[i][j]);

                    line += QString(std::max(1, 4 - static_cast<int>(number.size())), QLatin1Char(' '));

                    if (!clickable)
                    {
                        line += number;
                        continue;
                    }

                    const bool selected = i == seifertLoop_ && j == seifertLifted_;

                    line += QStringLiteral("<a href='%1,%2' style='text-decoration:none;'>"
                                           "<span style='color:%3;%4'>%5</span></a>")
                                .arg(i)
                                .arg(j)
                                .arg(selected ? QStringLiteral("#D99B42") : QStringLiteral("#BAB9B4"),
                                     selected ? QStringLiteral("font-weight:bold;text-decoration:underline;")
                                              : QString(),
                                     number);
                }

                rows += line + QLatin1Char('\n');
            }

            html += QStringLiteral("<pre style='font-family:Consolas,monospace;'>%1</pre>")
                        .arg(rows);

            if (clickable && seifertLoop_ < 0)
            {
                html += QStringLiteral("<span style='color:#8A8780;'>%1</span><br><br>")
                            .arg(tr("Click an entry of V to see its two loops on the surface."));
            }
            else if (clickable)
            {
                const int i = seifertLoop_;
                const int j = seifertLifted_;
                const int value = s.seifertMatrix[i][j];

                if (i == j)
                {
                    html += tr("<b>V[%1][%1] = lk(a<sub>%1</sub>, a<sub>%1</sub><sup>+</sup>) = %2</b><br>"
                               "The <span style='color:#46CD78;'>green</span> loop "
                               "a<sub>%1</sub> lies on the surface, the "
                               "<span style='color:#E150C8;'>pink</span> loop is its own copy "
                               "lifted off the amber side. The entry counts how many times "
                               "the copy winds around the loop: how many full twists the "
                               "surface makes along it.")
                                .arg(i + 1)
                                .arg(value);
                }
                else
                {
                    html += tr("<b>V[%1][%2] = lk(a<sub>%1</sub>, a<sub>%2</sub><sup>+</sup>) = %3</b><br>"
                               "The <span style='color:#46CD78;'>green</span> loop "
                               "a<sub>%1</sub> lies on the surface, the "
                               "<span style='color:#E150C8;'>pink</span> loop is "
                               "a<sub>%2</sub> lifted off the amber side. The entry, their "
                               "linking number, counts how many times the two loops wind "
                               "around each other (with a sign for the direction).")
                                .arg(i + 1)
                                .arg(j + 1)
                                .arg(value);
                }

                html += QStringLiteral(" <a href='clear' style='color:#D99B42;'>%1</a><br><br>")
                            .arg(tr("Clear"));
            }
        }
        else
        {
            html += QStringLiteral("<br>");
        }

        html += tr("Signature σ(V + Vᵀ) = <b>%1</b>").arg(s.signature) +
                QStringLiteral("<br>") +
                tr("Determinant |det(V + Vᵀ)| = <b>%1</b>").arg(s.determinant) +
                QStringLiteral("<br>") +
                tr("Alexander polynomial det(V − t Vᵀ) = <b>%1</b>")
                    .arg(s.alexanderText.toHtmlEscaped()) +
                QStringLiteral("<br><br>") +
                tr("The same Alexander polynomial as in the analysis panel, "
                   "found in a completely different way.");
    }
    else if (s.genus > 0)
    {
        html += QStringLiteral("<br><br>") + s.matrixError.toHtmlEscaped();
    }

    // the colours of the links and loops follow the theme
    seifertInfoLabel_->setText(Theme::html(html));
}

void MainWindow::showSeifertLoops(
    const QString &link)
{
    if (link == QLatin1String("clear"))
    {
        clearSeifertLoops();
        return;
    }

    const QStringList parts = link.split(QLatin1Char(','));

    if (parts.size() != 2 || !seifertActive_ || seifertDirty_)
        return;

    const int i = parts[0].toInt();
    const int j = parts[1].toInt();
    const int count = seifert_.loops.size();

    if (i < 0 || i >= count || j < 0 || j >= count)
        return;

    // the same entry again
    if (i == seifertLoop_ && j == seifertLifted_)
    {
        clearSeifertLoops();
        return;
    }

    seifertLoop_ = i;
    seifertLifted_ = j;

    // on the finished surface
    seifertTimer_->stop();

    setSeifertStep(3);

    updateSeifertInfo();
}

void MainWindow::clearSeifertLoops()
{
    if (seifertLoop_ < 0)
        return;

    seifertLoop_ = -1;
    seifertLifted_ = -1;

    updateSeifertInfo();
    renderSeifert();
}

/*
 * =============================================================
 * RELAX AND SCRAMBLE
 * =============================================================
 */

bool MainWindow::prepareRelaxation()
{
    if (!hasData_)
        return false;

    stopJoin();

    if (seifertActive_)
        stopSeifert();

    if (currentData_.multiStrand ||
        !currentData_.closed)
    {
        relaxStatusLabel_->setText(
            tr("Relaxing needs a single closed curve. "
               "Choose a knot preset or a custom curve."));

        return false;
    }

    // back to the curve itself (the animation frame is not the curve)
    if (unknotActive_ || unknotPreparing_)
        stopUnknotting(true);

    if (relax_.isValid())
        return true;

    if (!relax_.start(currentData_.points))
    {
        relaxStatusLabel_->setText(
            relax_.error());

        return false;
    }

    relaxFrameHint_ =
        KnotMath::TubeFrameHint();

    relaxShownRadius_ =
        -1.0;

    return true;
}

void MainWindow::runRelaxation()
{
    relaxRunning_ =
        true;

    // not yet looked for the best view
    relaxTurnLeft_ =
        -1.0;

    // the numbered markers belong to the curve before it moved
    glWidget_->setShowCrossingNumbers(
        false);

    glWidget_->clearFlashes();

    // relax_.curve() always fits in a ball of radius 2.7 (with the rope
    // grabbed the floor stays where pulling keeps it)
    if (grabEnabled())
    {
        fixPullBounds();
    }
    else
    {
        const float extent =
            2.7f +
            static_cast<float>(
                thicknessSlider_->value()) /
                100.0f;

        glWidget_->setFixedBounds(
            QVector3D(-extent, -extent, -extent),
            QVector3D(extent, extent, extent));
    }

    relaxClock_.restart();

    relaxTimer_->start();

    updateRelaxControls();

    renderRelaxFrame();
}

void MainWindow::pauseRelaxation()
{
    if (!relaxRunning_)
        return;

    relaxRunning_ =
        false;

    relaxTimer_->stop();

    // the moved curve becomes the current one: thickness, colours,
    // crossing numbers and the unknotting animation work on it
    currentData_.points =
        relax_.curve();

    currentData_.closed =
        true;

    currentCrossings_ =
        KnotMath::detectProjectionCrossings(
            currentData_.points,
            2,
            700);

    relaxChanged_ =
        true;

    if (!grabEnabled())
        glWidget_->clearFixedBounds();

    rebuildMesh();

    requestRelaxAnalysis();

    updateRelaxControls();

    noteChange();
}

void MainWindow::stopRelaxation()
{
    if (relaxTimer_ != nullptr)
        relaxTimer_->stop();

    const bool wasRunning =
        relaxRunning_ || pulling_;

    relaxRunning_ =
        false;

    // the rope in the hand is gone too
    if (pulling_ && glWidget_ != nullptr)
        glWidget_->cancelRopeGrab();

    pulling_ =
        false;

    relaxChanged_ =
        false;

    relax_ =
        KnotMath::KnotRelaxation();

    // analyses still running are for a curve that is gone
    ++relaxSession_;

    relaxAnalysisWanted_ =
        false;

    if (wasRunning && glWidget_ != nullptr && !grabEnabled())
        glWidget_->clearFixedBounds();

    if (relaxStatusLabel_ != nullptr)
        relaxStatusLabel_->clear();

    updateRelaxControls();
}

void MainWindow::relaxPlayPause()
{
    if (pulling_)
        return;

    if (relaxRunning_)
    {
        pauseRelaxation();

        relaxStatusLabel_->setText(
            tr("Paused. Press Relax to go on."));

        return;
    }

    if (!prepareRelaxation())
        return;

    if (relax_.settled())
    {
        relaxStatusLabel_->setText(
            tr("This shape is already relaxed. Scramble it first, "
               "then watch it find its way back."));

        return;
    }

    runRelaxation();
}

void MainWindow::relaxScramble()
{
    if (pulling_ || !prepareRelaxation())
        return;

    // the scramble keeps the strands about a tube thickness apart
    relax_.setTubeRadius(
        static_cast<double>(
            thicknessSlider_->value()) /
        100.0);

    relax_.beginScramble(
        QRandomGenerator::global()->generate());

    if (relaxRunning_)
        relaxTurnLeft_ = -1.0;
    else
        runRelaxation();
}

void MainWindow::relaxReset()
{
    // builds the original curve again (and forgets the moved one)
    generateMesh();
}

void MainWindow::relaxTick()
{
    // the rope follows the hand (most of the frame for the physics)
    if (pulling_)
    {
        relax_.advance(
            10.0);

        renderRelaxFrame();

        requestRelaxAnalysis();

        return;
    }

    if (!relaxRunning_)
        return;

    const double dt =
        std::min(
            0.1,
            static_cast<double>(
                relaxClock_.restart()) /
                1000.0);

    auto explain = [](const QString &title, const QString &body)
    {
        return QStringLiteral("<b>%1</b><br>%2").arg(title, body);
    };

    QString text =
        relaxStatusLabel_->text();

    if (relax_.scrambling() ||
        !relax_.settled())
    {
        // most of the frame for the physics, the rest for drawing;
        // scrambling at 3 steps a frame takes about 2.5 seconds
        relax_.advance(
            10.0,
            3);

        text =
            relax_.scrambling()
                ? explain(
                      tr("Scrambling"),
                      tr("Random swirls crumple the knot. No strand ever "
                         "passes through another, so it is still the same "
                         "knot - only much harder to recognise."))
                : explain(
                      tr("Relaxing"),
                      tr("Every part of the string pushes the others away, "
                         "like beads with the same electric charge, while "
                         "the string keeps its length. Watch the "
                         "classification on the right become sure of "
                         "itself as the picture gets simpler."));
    }
    else if (relaxTurnLeft_ < 0.0)
    {
        KnotMath::Vec3 axis;
        double angle = 0.0;

        relaxTurnLeft_ =
            relax_.bestViewRotation(axis, angle)
                ? angle
                : 0.0;

        relaxTurnAxis_ =
            axis;
    }
    else if (relaxTurnLeft_ > 0.0)
    {
        // about 70 degrees per second
        const double turn =
            std::min(
                relaxTurnLeft_,
                1.2 * dt);

        relax_.rotate(
            relaxTurnAxis_,
            turn);

        relaxTurnLeft_ -=
            turn;

        text = explain(
            tr("Finding the best view"),
            tr("The knot turns until its picture (seen straight from the "
               "front) has the fewest crossings."));
    }
    else
    {
        pauseRelaxation();

        relaxStatusLabel_->setText(
            explain(
                tr("Relaxed"),
                tr("No strand passed through another, so this is still "
                   "the same knot. Scramble it again, or press Reset to "
                   "go back to the original.")));

        return;
    }

    if (relaxStatusLabel_->text() != text)
        relaxStatusLabel_->setText(text);

    renderRelaxFrame();

    requestRelaxAnalysis();
}

void MainWindow::renderRelaxFrame()
{
    if (!relax_.isValid())
        return;

    const double requestedRadius =
        static_cast<double>(
            thicknessSlider_->value()) /
        100.0;

    relax_.setTubeRadius(
        requestedRadius);

    // thinner when strands come close, so tubes never run into each other
    const double limit =
        std::max(
            0.004,
            relax_.tubeLimit(
                requestedRadius));

    // It gets thinner at once but thicker only gradually, so the tube
    // does not flicker when two strands brush past each other.
    if (relaxShownRadius_ <= 0.0 ||
        limit < relaxShownRadius_)
    {
        relaxShownRadius_ =
            limit;
    }
    else
    {
        relaxShownRadius_ +=
            0.15 * (limit - relaxShownRadius_);
    }

    const double radius =
        relaxShownRadius_;

    // Drawn along a smooth spline with twice the points. The factor is
    // fixed so that every frame has the same rings (smooth frame hint).
    constexpr int refine =
        2;

    KnotMath::Mesh mesh =
        KnotMath::buildTubeMesh(
            KnotMath::refineCurve(
                relax_.curve(),
                refine,
                true),
            radius,
            32,
            true,
            KnotMath::ColorMode::SingleColor,
            2,
            &relaxFrameHint_);

    if (!mesh.isValid())
        return;

    KnotMath::computeAmbientOcclusion(
        mesh,
        0.7);

    glWidget_->setMesh(
        mesh);

    updateGrabCurve();
}

void MainWindow::requestRelaxAnalysis()
{
    if (!relax_.isValid())
        return;

    if (relaxAnalysing_)
    {
        relaxAnalysisWanted_ =
            true;

        return;
    }

    relaxAnalysing_ =
        true;

    relaxAnalysisWanted_ =
        false;

    const QVector<KnotMath::Vec3> curve =
        relax_.curve();

    const quint64 session =
        relaxSession_;

    auto result =
        std::make_shared<KnotMath::Invariants>();

    // The polynomials of a tangled curve take a moment, so they are
    // computed on a worker thread from a copy of the curve; meanwhile
    // the animation goes on.
    QThread *worker =
        QThread::create(
            [curve, result]()
            {
                *result =
                    KnotMath::computeInvariantsWithCrossings(
                        KnotMath::Preset::CustomCoordinates,
                        curve,
                        KnotMath::detectProjectionCrossings(
                            curve,
                            2,
                            700),
                        true);
            });

    connect(
        worker,
        &QThread::finished,
        worker,
        &QObject::deleteLater);

    connect(
        worker,
        &QThread::finished,
        this,
        [this, result, session]()
        {
            relaxAnalysing_ =
                false;

            if (session == relaxSession_)
            {
                // a joined knot keeps its name: moving it changes nothing
                applyConnectedSumInvariants(*result);

                updateInvariantPanel(*result);
            }

            if (relaxAnalysisWanted_)
                requestRelaxAnalysis();
        });

    worker->start();
}

void MainWindow::updateRelaxControls()
{
    if (relaxPlayButton_ == nullptr)
        return;

    relaxPlayButton_->setText(
        relaxRunning_
            ? tr("PAUSE")
            : tr("RELAX", "button"));

    Icons::set(relaxPlayButton_, relaxRunning_ ? QStringLiteral("pause") : QStringLiteral("play"));

    relaxResetButton_->setEnabled(
        relaxRunning_ || relaxChanged_);
}

namespace
{

// The hand stays within this distance of the centre of the view (the
// rope cannot be pulled away out of sight).
constexpr double PullReach =
    3.3;

QString pullExplanation(
    const QString &title,
    const QString &body)
{
    return QStringLiteral("<b>%1</b><br>%2").arg(title, body);
}

} // namespace

bool MainWindow::grabEnabled() const
{
    return grabButton_ != nullptr &&
           grabButton_->isChecked();
}

void MainWindow::fixPullBounds()
{
    const float extent =
        static_cast<float>(PullReach) +
        static_cast<float>(
            thicknessSlider_->value()) /
            100.0f;

    glWidget_->setFixedBounds(
        QVector3D(-extent, -extent, -extent),
        QVector3D(extent, extent, extent));
}

void MainWindow::updateGrabCurve()
{
    if (!grabEnabled() || glWidget_ == nullptr)
        return;

    const double requestedRadius =
        static_cast<double>(
            thicknessSlider_->value()) /
        100.0;

    // the curve on screen: the moving one, or the current knot
    if (relax_.isValid() &&
        (pulling_ || relaxRunning_ || relaxChanged_))
    {
        glWidget_->setGrabCurve(
            relax_.curve(),
            relaxShownRadius_ > 0.0
                ? relaxShownRadius_
                : requestedRadius);
    }
    else if (hasData_ &&
             !currentData_.multiStrand &&
             currentData_.closed &&
             !seifertActive_ &&
             !unknotActive_ &&
             !joinActive_)
    {
        glWidget_->setGrabCurve(
            currentData_.points,
            requestedRadius);
    }
    else
    {
        // nothing to grab (several strands, an open curve, or an
        // animation shows something else)
        glWidget_->setGrabCurve(
            {},
            0.0);
    }
}

void MainWindow::grabToggled(
    bool enabled)
{
    if (!enabled && photoRopeOn_)
    {
        // the rope stays where it was left
        if (photoRope_ != nullptr && photoRope_->heldPoint() >= 0)
            photoRopeReleased();

        glWidget_->setRopeGrab(false);

        relaxStatusLabel_->clear();

        updatePhotoRopeButtons();

        return;
    }

    if (!enabled)
    {
        if (pulling_)
            ropeReleased();

        glWidget_->setRopeGrab(
            false);

        if (!relaxRunning_)
        {
            glWidget_->clearFixedBounds();

            rebuildMesh();
        }

        relaxStatusLabel_->clear();

        return;
    }

    // Photo mode: the rope lying on the table is picked up as it is
    if (!relaxRunning_ && glWidget_->photoMode() && startPhotoRope())
    {
        glWidget_->setRopeGrab(true);

        showPhotoRope(false);

        relaxStatusLabel_->setText(
            pullExplanation(
                tr("Grab the rope"),
                tr("Pick the rope up with the left mouse button and move "
                   "it: held up, it goes over the other strands; hold "
                   "Shift while grabbing to push it under them. Cut the "
                   "rope to tie another knot.")));

        return;
    }

    // a running relaxation stops where it is
    if (relaxRunning_)
        pauseRelaxation();

    if (!prepareRelaxation())
    {
        const QSignalBlocker blocker(
            grabButton_);

        grabButton_->setChecked(
            false);

        return;
    }

    glWidget_->setRopeGrab(
        true);

    fixPullBounds();

    // Photo mode: the rope is lifted off the table to be pulled
    if (glWidget_->photoMode())
        rebuildMesh();

    updateGrabCurve();

    relaxStatusLabel_->setText(
        pullExplanation(
            tr("Grab the rope"),
            tr("Drag the rope with the left mouse button: the rest "
               "follows, and no strand can pass through another. Drag "
               "next to the rope to turn the view.")));
}

void MainWindow::ropeGrabbed(
    const QVector3D &point)
{
    if (!grabEnabled())
    {
        glWidget_->cancelRopeGrab();
        return;
    }

    // Photo mode: the rope on the table
    if (photoRopeOn_)
    {
        photoRopeGrabbed(point);
        return;
    }

    // the shape on screen is the relaxed one so far
    if (relaxRunning_)
        pauseRelaxation();

    if (!prepareRelaxation())
    {
        glWidget_->cancelRopeGrab();
        return;
    }

    relax_.setTubeRadius(
        static_cast<double>(
            thicknessSlider_->value()) /
        100.0);

    relax_.beginPull(
        KnotMath::Vec3(
            point.x(),
            point.y(),
            point.z()));

    pulling_ =
        true;

    // the numbered markers belong to the curve before it moves
    glWidget_->setShowCrossingNumbers(
        false);

    glWidget_->clearFlashes();

    fixPullBounds();

    relaxClock_.restart();

    relaxTimer_->start();

    relaxStatusLabel_->setText(
        pullExplanation(
            tr("Pulling"),
            tr("The rest of the rope follows. Strands push each other "
               "aside but never pass through each other, so it is still "
               "the same knot - watch the classification on the right.")));

    updateRelaxControls();
}

void MainWindow::ropeDragged(
    const QVector3D &point)
{
    if (photoRopeOn_)
    {
        photoRopeDragged(point);
        return;
    }

    if (!pulling_)
        return;

    KnotMath::Vec3 target(
        point.x(),
        point.y(),
        point.z());

    const double distance =
        target.length();

    if (distance > PullReach)
        target *= PullReach / distance;

    relax_.setPullTarget(
        target);
}

void MainWindow::ropeReleased()
{
    if (photoRopeOn_)
    {
        photoRopeReleased();
        return;
    }

    if (!pulling_)
        return;

    relax_.endPull();

    pulling_ =
        false;

    relaxTimer_->stop();

    // the pulled curve becomes the current one (as after a pause)
    currentData_.points =
        relax_.curve();

    currentData_.closed =
        true;

    currentCrossings_ =
        KnotMath::detectProjectionCrossings(
            currentData_.points,
            2,
            700);

    relaxChanged_ =
        true;

    rebuildMesh();

    requestRelaxAnalysis();

    updateRelaxControls();

    relaxStatusLabel_->setText(
        pullExplanation(
            tr("Let go"),
            tr("No strand passed through another, so this is still the "
               "same knot. Pull again, press Relax to let it settle, or "
               "Reset to go back to the original.")));

    noteChange();
}

/*
 * =============================================================
 * PHOTO MODE: THE ROPE IN THE HAND
 * =============================================================
 */

namespace
{

// How high the hand lifts the rope above the table (radii, centre line):
// clear over two strands lying on each other.
constexpr double HandHeight = 6.5;

// a rope let go comes to rest in a few seconds; one that still creeps
// after this long is taken as it lies
constexpr double LongestFall = 8.0;

bool sameCurve(
    const QVector<KnotMath::Vec3> &a,
    const QVector<KnotMath::Vec3> &b)
{
    return a.size() == b.size() &&
           std::equal(
               a.cbegin(),
               a.cend(),
               b.cbegin(),
               [](const KnotMath::Vec3 &p, const KnotMath::Vec3 &q)
               {
                   return p.x == q.x && p.y == q.y && p.z == q.z;
               });
}

} // namespace

bool MainWindow::startPhotoRope()
{
    if (glWidget_ == nullptr || !glWidget_->photoMode() || !hasData_)
        return false;

    if (photoRopeOn_)
        return true;

    const auto scene =
        static_cast<Photo::Scene>(photoSceneCombo_->currentData().toInt());

    // the lace on the sneaker is held by its eyelets
    if (Photo::sceneInfo(scene).sneaker)
        return false;

    // the rope as it lies on the table now
    if (!restingRope_.valid ||
        restingScene_ != static_cast<int>(scene) ||
        !sameCurve(restingFrom_, currentData_.points))
    {
        return false;
    }

    const double radius = restingRadius_;

    QVector<KnotMath::Vec3> points;

    points.reserve(restingRope_.points.size());

    for (const KnotMath::Vec3 &p : restingRope_.points)
    {
        const QVector3D q = restingRope_.pose.map(QVector3D(
            static_cast<float>(p.x),
            static_cast<float>(p.y),
            static_cast<float>(p.z)));

        points.push_back(KnotMath::Vec3(q.x(), q.y(), q.z()) / radius);
    }

    const Photo::RopeMaterial material = Photo::sceneInfo(scene).rope;

    Photo::RopeSimulation rope(points, {}, currentData_.closed, material);

    // the same rope: its strands where they lay, the bends it lay in
    {
        QVector<KnotMath::Vec3> frames;

        for (const KnotMath::Vec3 &f : restingRope_.frames)
        {
            const QVector3D q = restingRope_.pose.mapVector(
                QVector3D(static_cast<float>(f.x), static_cast<float>(f.y), static_cast<float>(f.z)));

            frames.push_back(KnotMath::Vec3(q.x(), q.y(), q.z()));
        }

        rope.useFrames(frames);
        rope.takeSet(material.tiedSet);
    }

    if (!rope.isValid())
        return false;

    photoRope_ = std::make_unique<Photo::RopeInHand>(rope);

    photoRopeOn_ = true;
    photoRopeFrom_ = currentData_.points;
    photoRopeRadius_ = radius;
    photoRopeScene_ = static_cast<int>(scene);
    photoRopePose_ = restingRope_.pose;
    photoRopeMaterial_ = material;
    photoRopeShown_ = 0;

    ++photoRopeSession_;

    updatePhotoRopeButtons();

    return true;
}

void MainWindow::stopPhotoRope()
{
    if (photoRopeTimer_ != nullptr)
        photoRopeTimer_->stop();

    finishPhotoRopeJob();

    if (photoRopeOn_ && glWidget_ != nullptr)
    {
        glWidget_->cancelRopeGrab();
        glWidget_->setRopeHandLevel(std::numeric_limits<float>::quiet_NaN());
    }

    photoRopeOn_ = false;
    photoRope_.reset();
    photoRopeFrom_.clear();

    // analyses still running are for a rope that is gone
    ++photoRopeSession_;
    photoRopeAnalysisWanted_ = false;

    updatePhotoRopeButtons();
}

bool MainWindow::showKeptPhotoRope(double radius)
{
    if (!photoRopeOn_)
        return false;

    const auto scene =
        static_cast<Photo::Scene>(photoSceneCombo_->currentData().toInt());

    const bool same =
        glWidget_ != nullptr && glWidget_->photoMode() && hasData_ &&
        static_cast<int>(scene) == photoRopeScene_ &&
        std::abs(radius - photoRopeRadius_) < 1.0e-9 &&
        sameCurve(photoRopeFrom_, currentData_.points) &&
        !relaxRunning_ && !pulling_ && !seifertActive_ && !unknotActive_ && !joinActive_;

    if (!same)
    {
        stopPhotoRope();
        return false;
    }

    showPhotoRope(photoRopeTimer_->isActive());

    return true;
}

QVector<KnotMath::Vec3> MainWindow::photoRopeFrames(const QVector<KnotMath::Vec3> &table) const
{
    const QMatrix4x4 back = photoRopePose_.inverted();

    QVector<KnotMath::Vec3> frames;

    frames.reserve(table.size());

    for (const KnotMath::Vec3 &f : table)
    {
        const QVector3D q = back.mapVector(
            QVector3D(static_cast<float>(f.x), static_cast<float>(f.y), static_cast<float>(f.z)));

        frames.push_back(KnotMath::Vec3(q.x(), q.y(), q.z()));
    }

    return frames;
}

QVector<KnotMath::Vec3> MainWindow::photoRopeCurve(
    QVector<KnotMath::Vec3> table,
    bool closed,
    bool joined) const
{
    /*
     * The knot of a rope with ends: its ends joined far from the rest. Here
     * by an arch over everything: straight up from one end to above the
     * highest part of the rope, across, and down to the other end (as a
     * knot is read from a rope lying on a table).
     */
    if (joined && !closed && table.size() >= 2)
    {
        double top = 0.0;

        for (const KnotMath::Vec3 &p : table)
            top = std::max(top, p.y);

        const double bridge = top + 4.0;

        const KnotMath::Vec3 last = table.last();
        const KnotMath::Vec3 first = table.first();

        const KnotMath::Vec3 corners[4] = {
            last,
            KnotMath::Vec3(last.x, bridge, last.z),
            KnotMath::Vec3(first.x, bridge, first.z),
            first};

        for (int side = 0; side < 3; ++side)
        {
            const KnotMath::Vec3 a = corners[side];
            const KnotMath::Vec3 b = corners[side + 1];

            const int pieces = std::max(2, static_cast<int>(std::ceil((b - a).length() / 0.5)));

            for (int k = 1; k < pieces; ++k)
                table.push_back(a + (b - a) * (static_cast<double>(k) / pieces));

            if (side < 2)
                table.push_back(b);
        }
    }

    const QMatrix4x4 back = photoRopePose_.inverted();
    const double radius = photoRopeRadius_;

    QVector<KnotMath::Vec3> curve;

    curve.reserve(table.size());

    for (const KnotMath::Vec3 &p : table)
    {
        const QVector3D q = back.map(QVector3D(
            static_cast<float>(p.x * radius),
            static_cast<float>(p.y * radius),
            static_cast<float>(p.z * radius)));

        curve.push_back(KnotMath::Vec3(q.x(), q.y(), q.z()));
    }

    return curve;
}

void MainWindow::showPhotoRope(bool quick)
{
    if (!photoRopeOn_ || glWidget_ == nullptr)
        return;

    finishPhotoRopeJob();

    const bool closed = photoRope_->closed();

    QVector<KnotMath::Vec3> tableFrames;

    const QVector<KnotMath::Vec3> curve =
        photoRopeCurve(photoRope_->points(&photoRopeShown_, &tableFrames), closed, false);

    KnotMath::Mesh mesh =
        Photo::buildRopeMesh(curve, closed, photoRopeRadius_, photoRopeMaterial_, quick,
                             photoRopeFrames(tableFrames));

    if (!mesh.isValid())
        return;

    // the numbered crossings belong to the knot as it was laid down
    glWidget_->setShowCrossingNumbers(false);
    glWidget_->setCrossings({});

    // the table stays where it is; only the rope moves on it
    glWidget_->setRestingRope(std::move(mesh), photoRopePose_, true);

    if (!quick)
    {
        glWidget_->setMeshColor(meshColor_);

        applySurface();
    }

    glWidget_->setGrabCurve(curve, photoRopeRadius_, closed);
}

void MainWindow::photoRopeTick()
{
    if (!photoRopeOn_ || photoRope_ == nullptr)
    {
        photoRopeTimer_->stop();
        return;
    }

    // the newest shape, built on the helper thread, goes to the view
    if (photoRopeJob_.valid())
    {
        if (photoRopeJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        PhotoRopeFrame frame = photoRopeJob_.get();

        if (frame.mesh.isValid())
        {
            glWidget_->setRestingRope(std::move(frame.mesh), photoRopePose_, true);
            glWidget_->setGrabCurve(frame.curve, photoRopeRadius_, frame.closed);
        }
    }

    const double elapsed = std::min(static_cast<double>(photoRopeClock_.restart()) / 1000.0, 0.1);

    const bool held = photoRope_->heldPoint() >= 0;

    if (!held)
        photoRopeLoose_ += elapsed;

    // a rope that still creeps after a long while is taken as it lies
    if (!held && photoRopeLoose_ > LongestFall)
        photoRope_->rest();

    quint64 version = 0;

    QVector<KnotMath::Vec3> tableFrames;

    const QVector<KnotMath::Vec3> points = photoRope_->points(&version, &tableFrames);

    if (!held && photoRope_->resting())
    {
        // at rest: drawn at full quality, and the knot is named
        photoRopeTimer_->stop();

        showPhotoRope(false);

        requestPhotoRopeAnalysis();

        updatePhotoRopeButtons();

        return;
    }

    // nothing new since the last shape
    if (version == photoRopeShown_)
        return;

    photoRopeShown_ = version;

    /*
     * The rope moves on its own thread (Photo::RopeInHand); its surface is
     * built from this copy on a helper thread while the view draws.
     */
    const bool closed = photoRope_->closed();
    const QVector<KnotMath::Vec3> curve = photoRopeCurve(points, closed, false);
    const QVector<KnotMath::Vec3> ropeFrames = photoRopeFrames(tableFrames);
    const double radius = photoRopeRadius_;
    const Photo::RopeMaterial material = photoRopeMaterial_;

    photoRopeJob_ = std::async(
        std::launch::async,
        [curve, ropeFrames, closed, radius, material]()
        {
            PhotoRopeFrame frame;

            frame.curve = curve;
            frame.closed = closed;
            frame.mesh = Photo::buildRopeMesh(curve, closed, radius, material, true, ropeFrames);

            return frame;
        });
}

void MainWindow::finishPhotoRopeJob()
{
    if (photoRopeJob_.valid())
        photoRopeJob_.get();
}

void MainWindow::photoRopeGrabbed(const QVector3D &point)
{
    if (photoRope_ == nullptr)
    {
        glWidget_->cancelRopeGrab();
        return;
    }

    const double radius = photoRopeRadius_;
    const QVector3D onTable = photoRopePose_.map(point) / static_cast<float>(radius);

    // the point of the rope under the mouse
    const QVector<KnotMath::Vec3> points = photoRope_->points();

    int nearest = 0;
    double best = std::numeric_limits<double>::max();

    for (int i = 0; i < points.size(); ++i)
    {
        const double d =
            (points.at(i) - KnotMath::Vec3(onTable.x(), onTable.y(), onTable.z())).lengthSquared();

        if (d < best)
        {
            best = d;
            nearest = i;
        }
    }

    // lifted, it goes over the rest; with Shift, pressed down, under it
    const bool under = glWidget_->ropeGrabUnder();
    const double level = under ? 1.0 : HandHeight;

    glWidget_->setRopeHandLevel(static_cast<float>(level * radius));

    const KnotMath::Vec3 held = points.at(nearest);

    photoRope_->hold(nearest, KnotMath::Vec3(held.x, level, held.z), under);

    photoRopeClock_.restart();
    photoRopeLoose_ = 0.0;

    // what was being worked out is for the rope before this move
    ++photoRopeSession_;

    photoRopeTimer_->start();

    updatePhotoRopeButtons();

    relaxStatusLabel_->setText(
        under
            ? pullExplanation(
                  tr("Pushing the rope under"),
                  tr("The part in your hand slides along the table, and "
                     "the strands it meets are lifted over it."))
            : pullExplanation(
                  tr("Holding the rope"),
                  tr("The part in your hand is lifted over the other "
                     "strands; the rest of the rope follows across the "
                     "table. Hold Shift while grabbing to push it under "
                     "them instead.")));
}

void MainWindow::photoRopeDragged(const QVector3D &point)
{
    if (photoRope_ == nullptr || photoRope_->heldPoint() < 0)
        return;

    const QVector3D onTable = photoRopePose_.map(point) / static_cast<float>(photoRopeRadius_);

    const double level = photoRope_->holdsUnder() ? 1.0 : HandHeight;

    photoRope_->moveHand(KnotMath::Vec3(onTable.x(), level, onTable.z()));
}

void MainWindow::photoRopeReleased()
{
    if (photoRope_ == nullptr)
        return;

    photoRope_->letGo();

    glWidget_->setRopeHandLevel(std::numeric_limits<float>::quiet_NaN());

    photoRopeLoose_ = 0.0;

    if (!photoRopeTimer_->isActive())
    {
        photoRopeClock_.restart();
        photoRopeTimer_->start();
    }

    updatePhotoRopeButtons();

    relaxStatusLabel_->setText(
        pullExplanation(
            tr("Let go"),
            tr("The rope falls back onto the table. Once it lies still, "
               "its knot is named.")));

    photoRopeTouched_ = true;

    updateUndoControls();
}

void MainWindow::cutPhotoRope()
{
    if (!photoRopeOn_ || photoRope_ == nullptr || !photoRope_->closed() || photoRope_->heldPoint() >= 0)
        return;

    finishPhotoRopeJob();

    photoRopeTouched_ = true;

    updateUndoControls();

    const QVector<KnotMath::Vec3> points = photoRope_->points();
    const int n = points.size();

    /*
     * Cut where the rope lies most on its own (farthest across the table
     * from any other part of it): nothing lies over the cut, so its ends
     * joined over the top again make the same knot.
     */
    const int apart = static_cast<int>(std::ceil(8.0 / 0.5));

    int cut = 0;
    double loneliest = -1.0;

    for (int i = 0; i < n; ++i)
    {
        double nearest = std::numeric_limits<double>::max();

        for (int j = 0; j < n; ++j)
        {
            const int gap = std::abs(i - j);

            if (std::min(gap, n - gap) <= apart)
                continue;

            const double dx = points.at(i).x - points.at(j).x;
            const double dz = points.at(i).z - points.at(j).z;

            nearest = std::min(nearest, dx * dx + dz * dz);
        }

        if (nearest > loneliest)
        {
            loneliest = nearest;
            cut = i;
        }
    }

    // a gap of a radius and a half between the two ends
    QVector<KnotMath::Vec3> open;

    open.reserve(n - 3);

    for (int k = 0; k < n - 3; ++k)
        open.push_back(points.at((cut + 2 + k) % n));

    photoRope_ = std::make_unique<Photo::RopeInHand>(
        Photo::RopeSimulation(open, {}, false, photoRopeMaterial_));

    ++photoRopeSession_;

    photoRopeClock_.restart();
    photoRopeLoose_ = 0.0;
    photoRopeTimer_->start();

    showPhotoRope(true);

    updatePhotoRopeButtons();

    relaxStatusLabel_->setText(
        pullExplanation(
            tr("The rope is cut"),
            tr("Now it has two ends. Drag them about: lifted, a part of the "
               "rope goes over the others, with Shift held while grabbing "
               "it goes under them. After every move the ends are joined "
               "over the top and the knot is named.")));
}

void MainWindow::tiePhotoRopeEnds()
{
    if (!photoRopeOn_ || photoRope_ == nullptr || photoRope_->closed() || photoRope_->heldPoint() >= 0)
        return;

    const QVector<KnotMath::Vec3> joined = photoRopeCurve(photoRope_->points(), false, true);

    // a point every radius is plenty for the knot's curve
    QVector<KnotMath::Vec3> curve;

    for (int i = 0; i < joined.size(); i += 2)
        curve.push_back(joined.at(i));

    if (curve.size() < 8)
        return;

    stopPhotoRope();

    {
        const QSignalBlocker blocker(grabButton_);

        grabButton_->setChecked(false);
    }

    glWidget_->setRopeGrab(false);

    // the knot tied in the rope becomes a knot of its own, as one drawn
    // by hand does
    const int customIndex =
        presetCombo_->findData(static_cast<int>(KnotMath::Preset::CustomCoordinates));

    if (customIndex >= 0)
        presetCombo_->setCurrentIndex(customIndex);

    customInputEdit_->setPlainText(KnotMath::diagramToCoordinateText(curve));

    generateMesh();

    relaxStatusLabel_->setText(
        pullExplanation(
            tr("The ends are tied"),
            tr("The knot in the rope is now the knot shown: every analysis "
               "works on it, and it is laid on the table afresh.")));
}

void MainWindow::requestPhotoRopeAnalysis()
{
    if (!photoRopeOn_ || photoRope_ == nullptr)
        return;

    if (photoRopeAnalysing_)
    {
        photoRopeAnalysisWanted_ = true;
        return;
    }

    photoRopeAnalysing_ = true;
    photoRopeAnalysisWanted_ = false;

    const bool open = !photoRope_->closed();
    const QVector<KnotMath::Vec3> curve = photoRopeCurve(photoRope_->points(), !open, true);
    const quint64 session = photoRopeSession_;

    auto result = std::make_shared<KnotMath::Invariants>();

    // the polynomials take a moment: worked out on a worker thread
    QThread *worker = QThread::create(
        [curve, result]()
        {
            *result = KnotMath::computeInvariantsWithCrossings(
                KnotMath::Preset::CustomCoordinates,
                curve,
                KnotMath::detectProjectionCrossings(curve, 2, 700),
                true);
        });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);

    connect(
        worker,
        &QThread::finished,
        this,
        [this, result, session, open]()
        {
            photoRopeAnalysing_ = false;

            if (session == photoRopeSession_ && photoRopeOn_)
            {
                updateInvariantPanel(*result);

                const QString name =
                    result->classification.isEmpty()
                        ? tr("not recognised")
                        : result->classification;

                relaxStatusLabel_->setText(
                    open
                        ? pullExplanation(
                              tr("The rope on the table"),
                              tr("With its ends joined over the top: %1. Tie "
                                 "the ends to make it the knot shown.")
                                  .arg(name))
                        : pullExplanation(
                              tr("The rope on the table"),
                              tr("Still the same knot: %1. No strand can pass "
                                 "through another; cut the rope to tie a "
                                 "different knot.")
                                  .arg(name)));
            }

            if (photoRopeAnalysisWanted_)
                requestPhotoRopeAnalysis();
        });

    worker->start();
}

void MainWindow::updatePhotoRopeButtons()
{
    if (cutRopeButton_ == nullptr || tieEndsButton_ == nullptr)
        return;

    const bool on = photoRopeOn_ && photoRope_ != nullptr;
    const bool holding = on && photoRope_->heldPoint() >= 0;
    const bool closed = on && photoRope_->closed();

    cutRopeButton_->setVisible(on);
    tieEndsButton_->setVisible(on);

    cutRopeButton_->setEnabled(on && closed && !holding);
    tieEndsButton_->setEnabled(on && !closed && !holding);
}

/*
 * =============================================================
 * PHOTO MODE: UNKNOTTING THE ROPE
 * =============================================================
 */

namespace
{

// the longest the finished loop may take to come to rest when it falls
// back onto the table
constexpr double UnknotLongestDrop = 6.0;

// the rope's own colour blended towards `hot` by w (0..1)
QColor blendColour(const QColor &base, const QColor &hot, double w)
{
    return QColor::fromRgbF(
        static_cast<float>(base.redF() + (hot.redF() - base.redF()) * w),
        static_cast<float>(base.greenF() + (hot.greenF() - base.greenF()) * w),
        static_cast<float>(base.blueF() + (hot.blueF() - base.blueF()) * w));
}

} // namespace

bool MainWindow::startPhotoUnknot()
{
    photoUnknot_ = false;

    if (glWidget_ == nullptr || !glWidget_->photoMode())
        return false;

    const auto scene =
        static_cast<Photo::Scene>(photoSceneCombo_->currentData().toInt());

    // the lace on the sneaker is held by its eyelets: the usual tube there
    if (Photo::sceneInfo(scene).sneaker)
        return false;

    if (!restingRope_.valid ||
        restingScene_ != static_cast<int>(scene) ||
        !sameCurve(restingFrom_, currentData_.points))
    {
        return false;
    }

    // a rope in the hand is put down
    stopPhotoRope();

    unknotPose_ = restingRope_.pose;
    unknotRopeRadius_ = restingRadius_;
    unknotMaterial_ = Photo::sceneInfo(scene).rope;

    /*
     * How high: the box of every frame of the animation, turned onto the
     * table, clear of it by a third of its size (the table top is y = 0).
     */
    // the rope's length: the knot's curve as the animation begins
    {
        const QVector<KnotMath::Vec3> first = unknotAnimation_.curveAt(0.0);

        unknotRopeLength_ = 0.0;

        for (int i = 0; i < first.size(); ++i)
            unknotRopeLength_ += (first.at((i + 1) % first.size()) - first.at(i)).length();
    }

    // where the knot is pulled open
    unknotWideFrom_ = 1.0;

    for (const KnotMath::UnknottingAnimation::TimelineMark &mark : unknotAnimation_.timeline())
    {
        if (mark.stage == KnotMath::UnknottingAnimation::Stage::Untangle)
            unknotWideFrom_ = mark.time;
    }

    // the box of all the frames, at the rope's length, and of the frames
    // before and after it is pulled open (the two camera shots)
    KnotMath::Vec3 lo(1.0e30, 1.0e30, 1.0e30);
    KnotMath::Vec3 hi(-1.0e30, -1.0e30, -1.0e30);

    KnotMath::Vec3 closeLo = lo;
    KnotMath::Vec3 closeHi = hi;
    KnotMath::Vec3 wideLo = lo;
    KnotMath::Vec3 wideHi = hi;

    auto grow = [](KnotMath::Vec3 &low, KnotMath::Vec3 &high, const KnotMath::Vec3 &p)
    {
        low = KnotMath::Vec3(std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z));
        high = KnotMath::Vec3(std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z));
    };

    for (int k = 0; k <= 100; ++k)
    {
        const double t = k / 100.0;

        for (const KnotMath::Vec3 &p : keepLength(unknotAnimation_.curveAt(t), unknotRopeLength_))
        {
            grow(lo, hi, p);

            if (t < unknotWideFrom_)
                grow(closeLo, closeHi, p);
            else
                grow(wideLo, wideHi, p);
        }
    }

    if (closeLo.x > closeHi.x)
    {
        closeLo = lo;
        closeHi = hi;
    }

    if (wideLo.x > wideHi.x)
    {
        wideLo = lo;
        wideHi = hi;
    }

    double lowest = std::numeric_limits<double>::max();

    for (int corner = 0; corner < 8; ++corner)
    {
        const QVector3D p = unknotPose_.map(QVector3D(
            static_cast<float>((corner & 1) ? hi.x : lo.x),
            static_cast<float>((corner & 2) ? hi.y : lo.y),
            static_cast<float>((corner & 4) ? hi.z : lo.z)));

        lowest = std::min(lowest, static_cast<double>(p.y()));
    }

    const double size = (hi - lo).length();
    const double rise = -lowest + unknotRopeRadius_ + 0.3 * size;

    const QVector3D up = unknotPose_.inverted().mapVector(QVector3D(0.0f, static_cast<float>(rise), 0.0f));

    unknotAir_ = KnotMath::Vec3(up.x(), up.y(), up.z());

    unknotLiftFrom_ = restingRope_.points;
    unknotLift_ = 0.0;
    unknotDrop_.reset();
    unknotLanded_.clear();

    photoUnknot_ = true;

    // the rope on the table and the knot in the air are all in view
    KnotMath::Vec3 low = lo;
    KnotMath::Vec3 high = hi + unknotAir_;

    for (const KnotMath::Vec3 &p : unknotLiftFrom_)
    {
        low = KnotMath::Vec3(std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z));
        high = KnotMath::Vec3(std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z));
    }

    const float margin = static_cast<float>(unknotRopeRadius_);

    glWidget_->setFixedBounds(
        QVector3D(static_cast<float>(low.x) - margin, static_cast<float>(low.y) - margin, static_cast<float>(low.z) - margin),
        QVector3D(static_cast<float>(high.x) + margin, static_cast<float>(high.y) + margin, static_cast<float>(high.z) + margin));

    // the camera goes round so the sun is behind it (the rope lit from
    // the front and a little from the side, no glare), a little above,
    // close on the knot
    auto shot = [this](const KnotMath::Vec3 &low, const KnotMath::Vec3 &high, QVector3D &centre, float &radius)
    {
        const KnotMath::Vec3 middle = (low + high) * 0.5 + unknotAir_;

        centre = QVector3D(static_cast<float>(middle.x), static_cast<float>(middle.y), static_cast<float>(middle.z));
        radius = static_cast<float>(0.5 * (high - low).length() + unknotRopeRadius_);
    };

    shot(closeLo, closeHi, unknotCloseCentre_, unknotCloseRadius_);
    shot(wideLo, wideHi, unknotWideCentre_, unknotWideRadius_);

    // (the box holds the loop turning as it opens: a little less is enough)
    unknotWideRadius_ *= 0.8f;

    unknotWideShot_ = false;

    glWidget_->flyWithSunBehind(unknotCloseCentre_, unknotCloseRadius_, 35.0f, 22.0f);

    glWidget_->setRopeTinted(true);

    // a portrait of the rope: the room behind out of focus and in the
    // dark, a spotlight on the knot
    glWidget_->setPhotoAperture(2.0f);
    glWidget_->setPhotoStage(true);

    return true;
}

void MainWindow::stopPhotoUnknot()
{
    if (!photoUnknot_)
        return;

    photoUnknot_ = false;
    unknotLift_ = 1.0;
    unknotDrop_.reset();
    unknotLanded_.clear();

    glWidget_->setRopeTinted(false);
    glWidget_->setCaption(QString(), QString());
    glWidget_->setPhotoAperture(8.0f);
    glWidget_->setPhotoStage(false);
}

void MainWindow::showPhotoUnknotRope(
    const QVector<KnotMath::Vec3> &curve,
    const QVector<float> &highlight,
    int along,
    const QColor &over,
    const QColor &under)
{
    KnotMath::Mesh mesh =
        Photo::buildRopeMesh(curve, true, unknotRopeRadius_, unknotMaterial_, true);

    if (!mesh.isValid())
        return;

    /*
     * Every ring, and the fibres on it, takes the tint of the point of the
     * animation at the same place along the rope (the strands of a
     * crossing change: blue on top, amber below, as in the diagram).
     */
    const int vertices = mesh.positions.size() / 3;
    const float length = mesh.strandLength.value(0, 1.0f);

    mesh.colors.resize(3 * vertices);

    for (int v = 0; v < vertices; ++v)
    {
        const int ring = mesh.vertexSample.value(v, 0);
        const float fraction = mesh.centerlineArc.value(ring, 0.0f) / std::max(length, 1.0e-6f);

        const int k = std::clamp(static_cast<int>(fraction * along), 0, std::max(along - 1, 0));
        const float w = std::clamp(highlight.value(k, 0.0f), -1.0f, 1.0f);

        const QColor colour = blendColour(unknotMaterial_.color, w >= 0.0f ? over : under, 0.85 * std::abs(w));

        mesh.colors[3 * v] = static_cast<float>(colour.redF());
        mesh.colors[3 * v + 1] = static_cast<float>(colour.greenF());
        mesh.colors[3 * v + 2] = static_cast<float>(colour.blueF());
    }

    // the table stays where it is; only the rope moves above it
    glWidget_->setRestingRope(std::move(mesh), unknotPose_, true);
}

void MainWindow::startUnknotDrop()
{
    // the finished loop (as long as the rope), where it is in the air, on
    // the table in radii
    const QVector<KnotMath::Vec3> loop = keepLength(unknotAnimation_.curveAt(1.0), unknotRopeLength_);

    QVector<KnotMath::Vec3> table;

    table.reserve(loop.size());

    for (const KnotMath::Vec3 &p : loop)
    {
        const KnotMath::Vec3 q = p + unknotAir_;
        const QVector3D w = unknotPose_.map(QVector3D(
            static_cast<float>(q.x),
            static_cast<float>(q.y),
            static_cast<float>(q.z)));

        table.push_back(KnotMath::Vec3(w.x(), w.y(), w.z()) / unknotRopeRadius_);
    }

    // points half a radius apart, as the rope physics wants them
    double length = 0.0;

    for (int i = 0; i < table.size(); ++i)
        length += (table.at((i + 1) % table.size()) - table.at(i)).length();

    const int n = std::clamp(static_cast<int>(std::lround(length / 0.5)), 16, 6000);

    unknotDrop_ = std::make_unique<Photo::RopeSimulation>(
        KnotMath::resampleClosedCurve(table, n, true), QVector<bool>(), true, unknotMaterial_);

    unknotDropTime_ = 0.0;

    if (!unknotDrop_->isValid())
        unknotDrop_.reset();
}

bool MainWindow::advanceUnknotDrop(double seconds)
{
    if (unknotDrop_ == nullptr)
        return false;

    // as much rope time as real time has passed
    const int steps = std::min(
        static_cast<int>(std::lround(seconds / unknotDrop_->secondsPerStep())),
        40);

    for (int k = 0; k < steps && !unknotDrop_->atRest(); ++k)
        unknotDrop_->step();

    unknotDropTime_ += seconds;

    // where it is now, in the knot's coordinates
    const QMatrix4x4 back = unknotPose_.inverted();

    unknotLanded_.clear();

    for (const KnotMath::Vec3 &p : unknotDrop_->points())
    {
        const QVector3D q = back.map(QVector3D(
            static_cast<float>(p.x * unknotRopeRadius_),
            static_cast<float>(p.y * unknotRopeRadius_),
            static_cast<float>(p.z * unknotRopeRadius_)));

        unknotLanded_.push_back(KnotMath::Vec3(q.x(), q.y(), q.z()));
    }

    if (unknotDrop_->atRest() || unknotDropTime_ > UnknotLongestDrop)
    {
        unknotDrop_.reset();
        return false;
    }

    return true;
}

/*
 * =============================================================
 * JOINING TWO KNOTS (connected sum)
 * =============================================================
 */

namespace
{

// Stages of the animation (seconds): the knots take their places, the
// cuts open, the band grows, and the parts fuse into one knot, which is
// shown a moment before it becomes the current knot.
constexpr double JoinIntroEnd = 1.8;
constexpr double JoinCutEnd = 2.8;
constexpr double JoinBridgeEnd = 4.4;
constexpr double JoinFuseEnd = 5.4;
constexpr double JoinEnd = 6.2;

const QColor JoinFirstColor(91, 155, 255);
const QColor JoinSecondColor(217, 155, 66);
const QColor JoinBandColor(255, 244, 214);

double joinEase(double t)
{
    t = std::clamp(t, 0.0, 1.0);

    return t * t * (3.0 - 2.0 * t);
}

QColor mixColors(const QColor &a, const QColor &b, double f)
{
    const float g = static_cast<float>(std::clamp(f, 0.0, 1.0));

    return QColor::fromRgbF(
        a.redF() + (b.redF() - a.redF()) * g,
        a.greenF() + (b.greenF() - a.greenF()) * g,
        a.blueF() + (b.blueF() - a.blueF()) * g);
}

std::map<int, long long> polynomialMap(
    const QVector<long long> &coefficients,
    int lowestPower)
{
    std::map<int, long long> result;

    for (int i = 0; i < coefficients.size(); ++i)
    {
        if (coefficients[i] != 0)
            result[lowestPower + i] = coefficients[i];
    }

    return result;
}

// bounding box of some curves
void curveBounds(
    std::initializer_list<const QVector<KnotMath::Vec3> *> curves,
    QVector3D &minimum,
    QVector3D &maximum)
{
    minimum = QVector3D(1e30f, 1e30f, 1e30f);
    maximum = QVector3D(-1e30f, -1e30f, -1e30f);

    for (const QVector<KnotMath::Vec3> *curve : curves)
    {
        for (const KnotMath::Vec3 &p : *curve)
        {
            const QVector3D q(
                static_cast<float>(p.x),
                static_cast<float>(p.y),
                static_cast<float>(p.z));

            minimum.setX(std::min(minimum.x(), q.x()));
            minimum.setY(std::min(minimum.y(), q.y()));
            minimum.setZ(std::min(minimum.z(), q.z()));

            maximum.setX(std::max(maximum.x(), q.x()));
            maximum.setY(std::max(maximum.y(), q.y()));
            maximum.setZ(std::max(maximum.z(), q.z()));
        }
    }
}

} // namespace

QString MainWindow::currentKnotSymbol() const
{
    if (!joined_.text.isEmpty() &&
        currentPreset() == KnotMath::Preset::CustomCoordinates &&
        customInputEdit_->toPlainText() == joined_.text)
    {
        return joined_.name;
    }

    switch (currentPreset())
    {
    case KnotMath::Preset::Unknot:
        return QStringLiteral("0₁");

    case KnotMath::Preset::Trefoil:
        return QStringLiteral("3₁");

    case KnotMath::Preset::FigureEight:
        return QStringLiteral("4₁");

    case KnotMath::Preset::Cinquefoil:
        return QStringLiteral("5₁");

    case KnotMath::Preset::Stevedore:
        return QStringLiteral("6₁");

    case KnotMath::Preset::CustomTorus:
        return QStringLiteral("T(%1,%2)")
            .arg(torusPSpin_->value())
            .arg(torusQSpin_->value());

    case KnotMath::Preset::KnotTable:
        return KnotMath::KnotTable::displayName(tableKnotName_);

    default:
        return QStringLiteral("K");
    }
}

void MainWindow::joinPlayStop()
{
    if (joinActive_)
    {
        stopJoin();

        joinStatusLabel_->setText(tr("Stopped."));

        return;
    }

    startJoin();
}

void MainWindow::startJoin()
{
    if (!hasData_ ||
        currentData_.multiStrand ||
        !currentData_.closed ||
        currentData_.points.size() < 4)
    {
        joinStatusLabel_->setText(
            tr("Joining needs a single closed knot. Choose a knot preset, "
               "a table knot or a custom closed curve."));

        return;
    }

    /*
     * The second knot
     */

    const QString choice =
        joinSecondCombo_->currentData().toString();

    QVector<KnotMath::Vec3> second;
    QString secondSymbol;

    if (choice == QStringLiteral("same"))
    {
        second = currentData_.points;
        secondSymbol = currentKnotSymbol();

        if (secondSymbol.contains(QLatin1Char('#')))
            secondSymbol = QStringLiteral("(%1)").arg(secondSymbol);
    }
    else if (choice == QStringLiteral("table"))
    {
        KnotBrowserDialog dialog(
            joinTableName_,
            this);

        if (dialog.exec() != QDialog::Accepted ||
            dialog.selectedKnot().isEmpty())
        {
            return;
        }

        joinTableName_ =
            dialog.selectedKnot();

        second =
            KnotMath::buildPreset(
                KnotMath::Preset::KnotTable,
                meshDensitySlider_->value(),
                2,
                3,
                joinTableName_)
                .points;

        secondSymbol =
            KnotMath::KnotTable::displayName(joinTableName_);
    }
    else
    {
        struct Choice
        {
            const char *key;
            KnotMath::Preset preset;
            const char *symbol;
        };

        static const Choice choices[] = {
            {"trefoil", KnotMath::Preset::Trefoil, "3₁"},
            {"figure8", KnotMath::Preset::FigureEight, "4₁"},
            {"cinquefoil", KnotMath::Preset::Cinquefoil, "5₁"},
            {"stevedore", KnotMath::Preset::Stevedore, "6₁"},
        };

        for (const Choice &c : choices)
        {
            if (choice != QLatin1String(c.key))
                continue;

            second =
                KnotMath::buildPreset(
                    c.preset,
                    meshDensitySlider_->value(),
                    2,
                    3,
                    QString())
                    .points;

            secondSymbol = QString::fromUtf8(c.symbol);
        }
    }

    if (second.size() < 4)
        return;

    const bool mirror =
        joinMirrorCheck_->isChecked();

    // mirror image: K*
    if (mirror)
        secondSymbol += QStringLiteral("*");

    // the curve as it is shown now is the first knot
    stopRelaxation();

    if (unknotActive_ || unknotPreparing_)
        stopUnknotting(false);

    if (seifertActive_)
        stopSeifert();

    /*
     * Placing the two knots and the band; a thick tube may leave no room
     * for the band, then its strands are drawn thinner.
     */

    const double requested =
        static_cast<double>(thicknessSlider_->value()) / 100.0;

    KnotMath::ConnectedSum sum;
    double radius = requested;

    for (const double factor : {1.0, 0.6, 0.35})
    {
        radius = requested * factor;

        sum = KnotMath::buildConnectedSum(
            currentData_.points,
            second,
            radius,
            mirror);

        if (sum.valid)
            break;
    }

    if (!sum.valid)
    {
        joinStatusLabel_->setText(sum.error);
        return;
    }

    /*
     * The invariants that follow from the two parts (of the curves as they
     * are shown, so mirror images count): the Alexander and Jones
     * polynomials of a connected sum are the products.
     */

    JoinedKnot joined;

    joined.name =
        currentKnotSymbol() + QStringLiteral(" # ") + secondSymbol;

    const KnotMath::AlexanderPolynomial alexanderA =
        KnotMath::computeAlexanderPolynomial(sum.firstBase);

    const KnotMath::AlexanderPolynomial alexanderB =
        KnotMath::computeAlexanderPolynomial(sum.secondBase);

    if (alexanderA.valid && alexanderB.valid)
    {
        joined.alexanderValid = true;
        joined.alexander =
            KnotMath::multiplyPolynomials(
                alexanderA.coefficients, alexanderA.lowestPower,
                alexanderB.coefficients, alexanderB.lowestPower);
    }

    const KnotMath::JonesPolynomial jonesA =
        KnotMath::computeJonesPolynomial(sum.firstBase);

    const KnotMath::JonesPolynomial jonesB =
        KnotMath::computeJonesPolynomial(sum.secondBase);

    if (jonesA.valid && jonesB.valid)
    {
        joined.jonesValid = true;
        joined.jones =
            KnotMath::multiplyPolynomials(
                jonesA.coefficients, jonesA.lowestPower,
                jonesB.coefficients, jonesB.lowestPower);
    }

    // two trefoils: the granny knot or the square knot
    const std::map<int, long long> trefoil = {{-1, 1}, {0, -1}, {1, 1}};

    if (alexanderA.valid && alexanderB.valid &&
        polynomialMap(alexanderA.coefficients, alexanderA.lowestPower) == trefoil &&
        polynomialMap(alexanderB.coefficients, alexanderB.lowestPower) == trefoil &&
        jonesA.valid && jonesB.valid)
    {
        if (jonesA == jonesB)
            joined.remark = tr("the granny knot");
        else if (jonesA == jonesB.mirrored())
            joined.remark = tr("the square knot");
    }

    if (currentPreset() == KnotMath::Preset::Unknot)
        joined.remark = tr("the same knot as the second one: joining the unknot changes nothing");

    pendingJoined_ = joined;

    /*
     * The animation
     */

    join_ = sum;
    joinTubeRadius_ = radius;
    joinTime_ = 0.0;
    joinStage_ = -1;
    joinActive_ = true;

    glWidget_->setShowCrossingNumbers(false);
    glWidget_->clearFlashes();

    // the floor under everything that will be shown, and a view from the
    // front: the first knot on the left, the second on the right
    QVector3D minimum;
    QVector3D maximum;

    curveBounds({&sum.firstBase, &sum.first, &sum.second, &sum.joined}, minimum, maximum);

    const QVector3D pad(
        static_cast<float>(radius),
        static_cast<float>(radius),
        static_cast<float>(radius));

    glWidget_->setFixedBounds(minimum - pad, maximum + pad);

    // The pair is wide and low: the ball the camera fits is as large as
    // needed for the width across the wider angle of view, or the height.
    curveBounds({&sum.first, &sum.second, &sum.joined}, minimum, maximum);

    const QVector3D size = maximum - minimum + 2.0f * pad;

    const float aspect =
        glWidget_->height() > 0
            ? static_cast<float>(glWidget_->width()) / static_cast<float>(glWidget_->height())
            : 1.0f;

    glWidget_->flyToSphere(
        0.5f * (minimum + maximum),
        0.5f * std::max({size.x() / std::max(1.0f, aspect), size.y(), size.z()}),
        0.0f,
        14.0f);

    joinButton_->setText(tr("STOP"));
    Icons::set(joinButton_, QStringLiteral("stop"));

    joinClock_.restart();
    joinTimer_->start();

    renderJoinFrame();
    updateJoinStatus();
}

void MainWindow::stopJoin(
    bool restoreKnot)
{
    if (!joinActive_)
        return;

    joinActive_ = false;

    joinTimer_->stop();

    glWidget_->clearFlashes();
    glWidget_->clearFixedBounds();

    joinButton_->setText(tr("JOIN"));
    Icons::set(joinButton_, QStringLiteral("play"));

    if (restoreKnot)
    {
        rebuildMesh();
        resetCamera();
    }
}

void MainWindow::joinTick()
{
    if (!joinActive_)
        return;

    joinTime_ +=
        std::min(
            0.05,
            static_cast<double>(joinClock_.restart()) / 1000.0);

    const double t = joinTime_;

    // flashes where the cuts open and where the two halves of each strand
    // of the band meet
    QVector<KnotMath::Vec3> flashes;
    QVector<float> progress;

    const double cut = (t - JoinIntroEnd) / 0.9;

    if (cut > 0.0 && cut < 1.0)
    {
        flashes << join_.first[join_.firstPeak] << join_.second[join_.secondPeak];
        progress << static_cast<float>(cut) << static_cast<float>(cut);
    }

    const double meet = (t - (JoinBridgeEnd - 0.15)) / 0.9;

    if (meet > 0.0 && meet < 1.0)
    {
        flashes << join_.bridgeOut[join_.bridgeOut.size() / 2]
                << join_.bridgeBack[join_.bridgeBack.size() / 2];
        progress << static_cast<float>(meet) << static_cast<float>(meet);
    }

    if (flashes.isEmpty())
        glWidget_->clearFlashes();
    else
        glWidget_->setFlashes(flashes, progress, QVector<int>(flashes.size(), 0));

    if (t >= JoinEnd)
    {
        finishJoin();
        return;
    }

    renderJoinFrame();
    updateJoinStatus();
}

void MainWindow::renderJoinFrame()
{
    using KnotMath::Vec3;

    const KnotMath::ConnectedSum &s = join_;
    const double t = joinTime_;

    auto tube = [](const QVector<Vec3> &curve, double radius, bool closed, const QColor &color)
    {
        KnotMath::Mesh part;

        if (curve.size() < 2)
            return part;

        part = KnotMath::buildTubeMesh(
            curve,
            radius,
            24,
            closed,
            KnotMath::ColorMode::SingleColor,
            2);

        if (part.isValid())
            part.fillColor(color);

        return part;
    };

    // the knot with a gap opened around its peak (0..1 of the cut)
    auto opened = [](const QVector<Vec3> &curve, int start, int peak, int end, double p)
    {
        const int n = curve.size();
        const int back = ((peak - start) % n + n) % n;
        const int ahead = ((end - peak) % n + n) % n;

        const int from = peak - std::max(1, static_cast<int>(std::lround(p * back)));
        const int to = peak + std::max(1, static_cast<int>(std::lround(p * ahead)));

        return KnotMath::curveWithout(curve, (from % n + n) % n, to % n);
    };

    const double r = joinTubeRadius_;

    KnotMath::Mesh mesh;

    if (t < JoinIntroEnd)
    {
        // the current knot moves to the left and turns its arc to face
        // the other one
        const double e = joinEase(t / JoinIntroEnd);
        const KnotMath::ConnectedSum::Pose &a = s.firstPose;

        mesh.append(tube(
            KnotMath::placeCurve(s.firstBase, a.yaw * e, a.tilt * e, a.shift * e),
            r,
            true,
            mixColors(meshColor_, JoinFirstColor, e)));

        // the second knot flies in from the right, spinning and growing
        const double f = joinEase((t - 0.35) / (JoinIntroEnd - 0.35));

        if (f > 0.0)
        {
            const KnotMath::ConnectedSum::Pose &b = s.secondPose;

            const double spin = (1.0 - f) * 1.5 * 3.14159265358979;
            const Vec3 shift = b.shift + Vec3(7.0 * (1.0 - f), 1.2 * (1.0 - f), 0.0);
            const double scale = std::max(0.05, f);

            mesh.append(tube(
                KnotMath::placeCurve(s.secondBase, b.yaw + spin, b.tilt, shift, scale),
                r * std::max(0.3, f),
                true,
                JoinSecondColor));
        }
    }
    else if (t < JoinCutEnd)
    {
        const double p = joinEase((t - JoinIntroEnd) / (JoinCutEnd - JoinIntroEnd));

        mesh.append(tube(
            opened(s.first, s.firstCutStart, s.firstPeak, s.firstCutEnd, p),
            r, false, JoinFirstColor));

        mesh.append(tube(
            opened(s.second, s.secondCutStart, s.secondPeak, s.secondCutEnd, p),
            r, false, JoinSecondColor));
    }
    else if (t < JoinBridgeEnd)
    {
        const double p = joinEase((t - JoinCutEnd) / (JoinBridgeEnd - JoinCutEnd));

        mesh.append(tube(
            KnotMath::curveWithout(s.first, s.firstCutStart, s.firstCutEnd),
            r, false, JoinFirstColor));

        mesh.append(tube(
            KnotMath::curveWithout(s.second, s.secondCutStart, s.secondCutEnd),
            r, false, JoinSecondColor));

        // every strand of the band grows from both of its ends and the two
        // halves meet in the middle
        for (const QVector<Vec3> *strand : {&s.bridgeOut, &s.bridgeBack})
        {
            const int count = strand->size();
            const int grown =
                std::min(count, 2 + static_cast<int>(std::lround(p * 0.5 * (count - 1))));

            mesh.append(tube(strand->mid(0, grown), r, false, JoinBandColor));
            mesh.append(tube(strand->mid(count - grown), r, false, JoinBandColor));
        }
    }
    else
    {
        // one closed knot; the colours of its parts fade into the colour
        // of the knot
        const double f = joinEase((t - JoinBridgeEnd) / (JoinFuseEnd - JoinBridgeEnd));

        mesh = tube(s.joined, r, true, meshColor_);

        const int samples = mesh.centerline.size() / 4;
        const int vertices = mesh.positions.size() / 3;
        const int points = s.joined.size();

        if (samples > 0 && mesh.vertexSample.size() == vertices)
        {
            for (int v = 0; v < vertices; ++v)
            {
                const int point =
                    std::clamp(mesh.vertexSample[v] * points / samples, 0, points - 1);

                const int part = s.joinedPart[point];

                const QColor from =
                    part == 0 ? JoinFirstColor : part == 1 ? JoinSecondColor : JoinBandColor;

                const QColor color = mixColors(from, meshColor_, f);

                mesh.colors[3 * v] = static_cast<float>(color.redF());
                mesh.colors[3 * v + 1] = static_cast<float>(color.greenF());
                mesh.colors[3 * v + 2] = static_cast<float>(color.blueF());
            }
        }
    }
    if (!mesh.isValid())
        return;

    glWidget_->setMesh(mesh);
    glWidget_->setMeshColor(meshColor_);

    applySurface();
}

void MainWindow::updateJoinStatus()
{
    const double t = joinTime_;

    const int stage =
        t < JoinIntroEnd    ? 0
        : t < JoinCutEnd    ? 1
        : t < JoinBridgeEnd ? 2
                            : 3;

    if (stage == joinStage_)
        return;

    joinStage_ = stage;

    auto explain = [](const QString &title, const QString &body)
    {
        return QStringLiteral("<b>%1</b><br>%2").arg(title, body);
    };

    switch (stage)
    {
    case 0:
        joinStatusLabel_->setText(explain(
            tr("1 / 4  Two knots"),
            tr("The two knots are placed side by side, each turned so that "
               "one of its arcs faces the other knot.")));
        break;

    case 1:
        joinStatusLabel_->setText(explain(
            tr("2 / 4  Cut"),
            tr("A small arc is cut out of each knot. Beyond the cut there "
               "is nothing else of either knot.")));
        break;

    case 2:
        joinStatusLabel_->setText(explain(
            tr("3 / 4  The band"),
            tr("Two strands join the four loose ends. They run straight "
               "across the gap, so the band adds no knotting of its own.")));
        break;

    default:
        joinStatusLabel_->setText(explain(
            tr("4 / 4  One knot"),
            tr("The two knots are one now: %1. A plane between the two "
               "halves meets it in just two points.")
                .arg(pendingJoined_.name)));
        break;
    }
}

void MainWindow::finishJoin()
{
    using KnotMath::Vec3;

    const QVector<Vec3> &curve = join_.joined;

    joinActive_ = false;

    joinTimer_->stop();

    glWidget_->clearFlashes();
    glWidget_->clearFixedBounds();

    joinButton_->setText(tr("JOIN"));
    Icons::set(joinButton_, QStringLiteral("play"));

    // The joined knot becomes the current knot, as a Custom Curve (like a
    // drawn knot): every analysis and animation works on it, and it can be
    // saved.
    joined_ = pendingJoined_;
    joined_.text = KnotMath::diagramToCoordinateText(curve);

    const int customIndex =
        presetCombo_->findData(
            static_cast<int>(
                KnotMath::Preset::CustomCoordinates));

    if (customIndex >= 0)
        presetCombo_->setCurrentIndex(customIndex);

    customInputEdit_->setPlainText(joined_.text);

    generateMesh();

    QString text =
        QStringLiteral("<b>%1</b><br>%2")
            .arg(tr("Connected sum %1").arg(joined_.name),
                 tr("Its Alexander and Jones polynomials are the products of "
                    "those of the two knots (see Analysis), its genus is the "
                    "sum of theirs. So a sum of two knotted knots can never "
                    "be undone. Press Relax above to watch it settle into "
                    "its own shape."));

    if (joined_.remark == tr("the granny knot") ||
        joined_.remark == tr("the square knot"))
    {
        text += QStringLiteral("<br><br>") +
                tr("Two trefoils of the same handedness give the granny knot, "
                   "of opposite handedness the square knot: the same Alexander "
                   "polynomial, but different Jones polynomials.");
    }

    joinStatusLabel_->setText(text);
}

void MainWindow::applyConnectedSumInvariants(
    KnotMath::Invariants &invariants) const
{
    if (joined_.text.isEmpty() ||
        currentPreset() != KnotMath::Preset::CustomCoordinates ||
        customInputEdit_->toPlainText() != joined_.text)
    {
        return;
    }

    invariants.classification =
        tr("Connected sum %1").arg(joined_.name);

    if (!joined_.remark.isEmpty())
        invariants.classification += QStringLiteral(", ") + joined_.remark;

    QStringList notes;

    if (joined_.alexanderValid)
    {
        const QString product = KnotMath::formatPolynomial(joined_.alexander);

        if (invariants.alexanderPolynomial == product)
        {
            notes << tr("The Alexander polynomial of the joined curve is the "
                        "product of those of the two knots, as it must be.");
        }
        else
        {
            invariants.alexanderPolynomial = product;

            notes << tr("Alexander polynomial: the product of those of the "
                        "two knots.");
        }
    }

    if (joined_.jonesValid)
    {
        const QString product = KnotMath::formatPolynomial(joined_.jones);

        if (invariants.jonesPolynomial == product)
        {
            notes << tr("So is its Jones polynomial.");
        }
        else
        {
            invariants.jonesPolynomial = product;

            notes << tr("Jones polynomial: the product of those of the two "
                        "knots (this diagram is too large to compute it "
                        "directly).");
        }
    }

    invariants.methodology = notes.join(QLatin1Char(' '));
}
