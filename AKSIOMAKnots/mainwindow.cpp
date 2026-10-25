#include "mainwindow.h"

#include "aboutdialog.h"
#include "appsettings.h"
#include "diagrameditor.h"
#include "knotbrowser.h"
#include "knotdiagram.h"
#include "knottable.h"
#include "theme.h"
#include "timelineslider.h"
#include "unknotsummarydialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QColorDialog>
#include <QEvent>
#include <QKeySequence>
#include <QShortcut>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTabWidget>
#include <QSplitter>
#include <QSpinBox>
#include <QSlider>
#include <QTextBrowser>
#include <QTextEdit>
#include <QGuiApplication>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

MainWindow::MainWindow(
    QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(
        tr("AKSIOMA Knots"));

    setMinimumSize(
        QSize(
            1000,
            650));

    QWidget *central = new QWidget(this);

    QVBoxLayout *rootLayout = new QVBoxLayout(central);

    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    /*
     * TOP BAR: brand on the left, one quiet line on the right.
     */

    QWidget *topBar = new QWidget(central);

    topBar->setObjectName(QStringLiteral("TopBar"));
    topBar->setAttribute(Qt::WA_StyledBackground, true);
    topBar->setFixedHeight(52);

    QHBoxLayout *topLayout = new QHBoxLayout(topBar);

    topLayout->setContentsMargins(24, 0, 24, 0);
    topLayout->setSpacing(14);

    QLabel *brandLabel = new QLabel(tr("AKSIOMA"), topBar);

    brandLabel->setObjectName(QStringLiteral("Brand"));

    QLabel *brandSub = new QLabel(tr("KNOTS"), topBar);

    brandSub->setObjectName(QStringLiteral("BrandSub"));

    QLabel *topHint = new QLabel(tr("KNOT THEORY  /  3D ANALYSIS"), topBar);

    topHint->setObjectName(QStringLiteral("Caption"));

    topLayout->addWidget(brandLabel);
    topLayout->addWidget(brandSub);
    topLayout->addStretch(1);
    topLayout->addWidget(topHint);

    rootLayout->addWidget(topBar);

    /*
     * MAIN AREA: sidebar | 3D view | analysis
     */

    mainSplitter_ = new QSplitter(Qt::Horizontal, central);

    mainSplitter_->setChildrenCollapsible(false);
    mainSplitter_->setHandleWidth(1);

    QWidget *leftPanel = createLeftPanel();

    QWidget *rightPanel = createRightPanel();

    // 3D view with a slim bar underneath
    QWidget *viewport = new QWidget(central);

    QVBoxLayout *viewportLayout = new QVBoxLayout(viewport);

    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(0);

    glWidget_ = new KnotGLWidget(viewport);

    viewportLayout->addWidget(glWidget_, 1);

    QWidget *viewBar = new QWidget(viewport);

    viewBar->setObjectName(QStringLiteral("ViewBar"));
    viewBar->setAttribute(Qt::WA_StyledBackground, true);
    viewBar->setFixedHeight(48);

    QHBoxLayout *viewBarLayout = new QHBoxLayout(viewBar);

    viewBarLayout->setContentsMargins(24, 0, 24, 0);
    viewBarLayout->setSpacing(10);

    QLabel *navigationHint =
        new QLabel(
            tr("DRAG  ORBIT      RIGHT-DRAG  PAN      SCROLL  ZOOM"),
            viewBar);

    navigationHint->setObjectName(QStringLiteral("Caption"));

    resetCameraButton_ = new QPushButton(tr("Reset view"), viewBar);

    colorButton_ = new QPushButton(tr("Tube color"), viewBar);

    QPushButton *aboutButton = new QPushButton(tr("About"), viewBar);

    connect(aboutButton, &QPushButton::clicked, this, &MainWindow::showAbout);

    viewBarLayout->addWidget(navigationHint);
    viewBarLayout->addStretch(1);
    viewBarLayout->addWidget(resetCameraButton_);
    viewBarLayout->addWidget(colorButton_);
    viewBarLayout->addWidget(aboutButton);

    viewportLayout->addWidget(viewBar);

    mainSplitter_->addWidget(leftPanel);
    mainSplitter_->addWidget(viewport);
    mainSplitter_->addWidget(rightPanel);

    mainSplitter_->setStretchFactor(0, 0);
    mainSplitter_->setStretchFactor(1, 1);
    mainSplitter_->setStretchFactor(2, 0);

    mainSplitter_->setSizes(QVector<int>{ 330, 1000, 370 });

    rootLayout->addWidget(mainSplitter_, 1);

    setCentralWidget(central);

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

    applySurface();

    presetCombo_->setCurrentIndex(
        presetCombo_->findData(
            static_cast<int>(
                KnotMath::Preset::Trefoil)));

    updateCustomInputVisibility();

    updateLearnSection();

    generateMesh();
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

    QLabel *indexLabel = new QLabel(index, row);

    indexLabel->setObjectName(QStringLiteral("SectionIndex"));

    QLabel *titleLabel = new QLabel(title, row);

    titleLabel->setObjectName(QStringLiteral("Section"));

    QFrame *rule = new QFrame(row);

    rule->setObjectName(QStringLiteral("Rule"));
    rule->setFixedHeight(1);
    rule->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    rowLayout->addWidget(indexLabel);
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
    const QString &valueText)
{
    QWidget *block = new QWidget(parent);

    QVBoxLayout *blockLayout = new QVBoxLayout(block);

    blockLayout->setContentsMargins(0, 0, 0, 0);
    blockLayout->setSpacing(6);

    QHBoxLayout *captionRow = new QHBoxLayout();

    captionRow->setContentsMargins(0, 0, 0, 0);

    QLabel *captionLabel = new QLabel(caption, block);

    captionLabel->setObjectName(QStringLiteral("Field"));

    valueLabel = new QLabel(valueText, block);

    valueLabel->setObjectName(QStringLiteral("Value"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    captionRow->addWidget(captionLabel);
    captionRow->addStretch(1);
    captionRow->addWidget(valueLabel);

    slider = new QSlider(Qt::Horizontal, block);

    slider->setRange(minimum, maximum);
    slider->setValue(value);

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

QWidget *MainWindow::createLeftPanel()
{
    QScrollArea *scroll = new QScrollArea(this);

    scroll->setObjectName(QStringLiteral("PanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(300);
    scroll->setMaximumWidth(380);

    QWidget *panel = new QWidget(scroll);

    panel->setObjectName(QStringLiteral("Panel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *layout = new QVBoxLayout(panel);

    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);

    /*
     * 01  KNOT
     */

    addSectionHeader(layout, panel, QStringLiteral("01"), tr("KNOT"));

    presetCombo_ = new QComboBox(panel);

    presetCombo_->setMinimumHeight(36);

    presetCombo_->addItem(tr("Unknot"), static_cast<int>(KnotMath::Preset::Unknot));
    presetCombo_->addItem(tr("Trefoil Knot (3\u2081)"), static_cast<int>(KnotMath::Preset::Trefoil));
    presetCombo_->addItem(tr("Figure-Eight Knot (4\u2081)"), static_cast<int>(KnotMath::Preset::FigureEight));
    presetCombo_->addItem(tr("Cinquefoil (5\u2081)"), static_cast<int>(KnotMath::Preset::Cinquefoil));
    presetCombo_->addItem(tr("Stevedore Knot (6\u2081)"), static_cast<int>(KnotMath::Preset::Stevedore));
    presetCombo_->addItem(tr("DNA Double Helix (Multi-strand)"), static_cast<int>(KnotMath::Preset::DNAHelix));
    presetCombo_->addItem(tr("Celtic Armor (Multi-strand)"), static_cast<int>(KnotMath::Preset::CelticKnot));
    presetCombo_->addItem(tr("Custom Torus T(p,q)"), static_cast<int>(KnotMath::Preset::CustomTorus));
    presetCombo_->addItem(tr("Custom Curve"), static_cast<int>(KnotMath::Preset::CustomCoordinates));
    presetCombo_->addItem(tr("Knot Table (12,965 knots)"), static_cast<int>(KnotMath::Preset::KnotTable));

    layout->addWidget(presetCombo_);

    browseKnotsButton_ = new QPushButton(tr("Browse all knots..."), panel);

    browseKnotsButton_->setMinimumHeight(34);
    browseKnotsButton_->setToolTip(
        tr("All 12,965 prime knots with up to 13 crossings."));

    layout->addWidget(browseKnotsButton_);

    // torus parameters (only for T(p,q))
    torusRow_ = new QWidget(panel);

    QHBoxLayout *torusLayout = new QHBoxLayout(torusRow_);

    torusLayout->setContentsMargins(0, 0, 0, 0);
    torusLayout->setSpacing(10);

    torusPSpin_ = new QSpinBox(torusRow_);
    torusQSpin_ = new QSpinBox(torusRow_);

    torusPSpin_->setRange(1, 20);
    torusQSpin_->setRange(1, 20);
    torusPSpin_->setValue(3);
    torusQSpin_->setValue(2);
    torusPSpin_->setMinimumHeight(36);
    torusQSpin_->setMinimumHeight(36);

    torusLayout->addWidget(makeFieldLabel(tr("p"), torusRow_));
    torusLayout->addWidget(torusPSpin_, 1);
    torusLayout->addSpacing(8);
    torusLayout->addWidget(makeFieldLabel(tr("q"), torusRow_));
    torusLayout->addWidget(torusQSpin_, 1);

    layout->addWidget(torusRow_);

    // custom curve (only for Custom Curve)
    customBox_ = new QWidget(panel);

    QVBoxLayout *customLayout = new QVBoxLayout(customBox_);

    customLayout->setContentsMargins(0, 0, 0, 0);
    customLayout->setSpacing(8);

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

    QHBoxLayout *customButtons = new QHBoxLayout();

    customButtons->setSpacing(8);

    QPushButton *drawKnotButton = new QPushButton(tr("Draw a knot..."), customBox_);

    drawKnotButton->setMinimumHeight(36);
    drawKnotButton->setToolTip(
        tr("Draw a loop with the mouse and choose over/under at each crossing."));

    generateButton_ = new QPushButton(tr("APPLY"), customBox_);

    generateButton_->setObjectName(QStringLiteral("GenerateButton"));
    generateButton_->setMinimumHeight(36);

    customButtons->addWidget(drawKnotButton, 1);
    customButtons->addWidget(generateButton_, 1);

    customLayout->addLayout(customButtons);

    layout->addWidget(customBox_);

    connect(drawKnotButton, &QPushButton::clicked,
            this, &MainWindow::drawKnotDialog);

    layout->addSpacing(10);

    /*
     * 02  APPEARANCE
     */

    addSectionHeader(layout, panel, QStringLiteral("02"), tr("APPEARANCE"));

    addSliderRow(layout, panel, tr("Tube thickness"),
                 thicknessSlider_, thicknessValueLabel_,
                 2, 60, 16, QStringLiteral("0.16"));

    addSliderRow(layout, panel, tr("Curve density"),
                 meshDensitySlider_, meshDensityValueLabel_,
                 48, 320, 160, QStringLiteral("160"));

    layout->addWidget(makeFieldLabel(tr("Material"), panel));

    materialCombo_ = new QComboBox(panel);

    materialCombo_->setMinimumHeight(36);
    materialCombo_->addItem(tr("Glossy"));
    materialCombo_->addItem(tr("Satin"));
    materialCombo_->addItem(tr("Metal"));
    materialCombo_->addItem(tr("Rope"));

    layout->addWidget(materialCombo_);

    addSliderRow(layout, panel, tr("Material shine"),
                 metallicSlider_, metallicValueLabel_,
                 0, 100, 70, QStringLiteral("0.70"));

    layout->addWidget(makeFieldLabel(tr("Color mode"), panel));

    colorModeCombo_ = new QComboBox(panel);

    colorModeCombo_->setMinimumHeight(36);
    colorModeCombo_->addItem(tr("Single Color"));
    colorModeCombo_->addItem(tr("Crossing Colors"));
    colorModeCombo_->addItem(tr("Strand Colors"));
    colorModeCombo_->setCurrentIndex(
        static_cast<int>(KnotMath::ColorMode::SingleColor));

    layout->addWidget(colorModeCombo_);

    showCrossingNumbersCheck_ = new QCheckBox(tr("Show crossing numbers"), panel);
    autoRotationCheck_ = new QCheckBox(tr("Auto-rotate"), panel);

    showCrossingNumbersCheck_->setChecked(false);
    autoRotationCheck_->setChecked(false);

    layout->addWidget(showCrossingNumbersCheck_);
    layout->addWidget(autoRotationCheck_);

    layout->addSpacing(10);

    /*
     * 03  UNKNOT
     */

    addSectionHeader(layout, panel, QStringLiteral("03"), tr("RELAX"));

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

    relaxPlayButton_ = new QPushButton(tr("▶  RELAX"), relaxGroup);

    relaxPlayButton_->setObjectName(QStringLiteral("GenerateButton"));
    relaxPlayButton_->setMinimumHeight(40);

    scrambleButton_ = new QPushButton(tr("Scramble"), relaxGroup);

    scrambleButton_->setMinimumHeight(40);

    relaxResetButton_ = new QPushButton(tr("Reset"), relaxGroup);

    relaxResetButton_->setMinimumHeight(40);
    relaxResetButton_->setEnabled(false);

    relaxButtons->addWidget(relaxPlayButton_, 3);
    relaxButtons->addWidget(scrambleButton_, 2);
    relaxButtons->addWidget(relaxResetButton_, 2);

    relaxLayout->addLayout(relaxButtons);

    relaxStatusLabel_ = new QLabel(QString(), relaxGroup);

    relaxStatusLabel_->setObjectName(QStringLiteral("Status"));
    relaxStatusLabel_->setWordWrap(true);
    relaxStatusLabel_->setMinimumHeight(40);
    relaxStatusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    relaxLayout->addWidget(relaxStatusLabel_);

    layout->addWidget(relaxGroup);

    addSectionHeader(layout, panel, QStringLiteral("04"), tr("UNKNOT"));

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

    QHBoxLayout *unknotButtons = new QHBoxLayout();

    unknotButtons->setSpacing(8);

    unknotPlayButton_ = new QPushButton(tr("\u25B6  UNKNOT"), unknotGroup_);

    unknotPlayButton_->setObjectName(QStringLiteral("GenerateButton"));
    unknotPlayButton_->setMinimumHeight(40);

    unknotResetButton_ = new QPushButton(tr("Reset"), unknotGroup_);

    unknotResetButton_->setMinimumHeight(40);
    unknotResetButton_->setEnabled(false);

    unknotSummaryButton_ = new QPushButton(tr("Steps"), unknotGroup_);

    unknotSummaryButton_->setMinimumHeight(40);
    unknotSummaryButton_->setEnabled(false);
    unknotSummaryButton_->setToolTip(
        tr("Every crossing change of the animation, one by one."));

    unknotButtons->addWidget(unknotPlayButton_, 3);
    unknotButtons->addWidget(unknotSummaryButton_, 1);
    unknotButtons->addWidget(unknotResetButton_, 1);

    unknotLayout->addLayout(unknotButtons);

    unknotScrubSlider_ = new TimelineSlider(unknotGroup_);

    unknotScrubSlider_->setRange(0, 1000);
    unknotScrubSlider_->setEnabled(false);

    unknotLayout->addWidget(unknotScrubSlider_);

    QHBoxLayout *speedLayout = new QHBoxLayout();

    speedLayout->setSpacing(10);

    unknotSpeedCombo_ = new QComboBox(unknotGroup_);

    unknotSpeedCombo_->setMinimumHeight(34);
    unknotSpeedCombo_->addItem(QStringLiteral("0.25x"), 0.25);
    unknotSpeedCombo_->addItem(QStringLiteral("0.5x"), 0.5);
    unknotSpeedCombo_->addItem(QStringLiteral("1x"), 1.0);
    unknotSpeedCombo_->addItem(QStringLiteral("2x"), 2.0);
    unknotSpeedCombo_->addItem(QStringLiteral("4x"), 4.0);
    unknotSpeedCombo_->setCurrentIndex(2);

    speedLayout->addWidget(makeFieldLabel(tr("Speed"), unknotGroup_));
    speedLayout->addWidget(unknotSpeedCombo_, 1);

    unknotLayout->addLayout(speedLayout);

    unknotStepCheck_ =
        new QCheckBox(tr("Step by step (pause at every stage)"), unknotGroup_);

    unknotStepCheck_->setChecked(false);

    unknotLayout->addWidget(unknotStepCheck_);

    unknotStatusLabel_ = new QLabel(QString(), unknotGroup_);

    unknotStatusLabel_->setObjectName(QStringLiteral("Status"));
    unknotStatusLabel_->setWordWrap(true);
    unknotStatusLabel_->setMinimumHeight(40);
    unknotStatusLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    unknotLayout->addWidget(unknotStatusLabel_);

    layout->addWidget(unknotGroup_);

    layout->addStretch(1);

    scroll->setWidget(panel);

    return scroll;
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

    auto makeTile = [&](const QString &caption, QLabel *&valueLabel)
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

        tiles->addWidget(tile, 1);
    };

    makeTile(tr("WRITHE"), writheValueLabel_);
    makeTile(tr("CROSSINGS"), crossingValueLabel_);

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

    tabs->addTab(createSettingsPanel(), tr("SETTINGS"));

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

    // texts with their own colours
    updateLearnSection();

    if (unknotActive_)
        renderUnknotFrame();

    if (glWidget_ != nullptr)
        glWidget_->update();
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

    // full screen only when asked for, even if the window was closed so
    if (AppSettings::startFullScreen())
        setWindowState(windowState() | Qt::WindowFullScreen);
    else
        setWindowState(windowState() & ~Qt::WindowFullScreen);

    show();
}

void MainWindow::closeEvent(
    QCloseEvent *event)
{
    if (AppSettings::rememberWindow())
        AppSettings::setWindowGeometry(saveGeometry());

    QMainWindow::closeEvent(
        event);
}

void MainWindow::rebuildWindow()
{
    MainWindow *replacement = new MainWindow();

    replacement->setAttribute(
        Qt::WA_DeleteOnClose);

    replacement->takeOverFrom(
        *this);

    replacement->show();

    close();
}

void MainWindow::takeOverFrom(
    MainWindow &previous)
{
    previous.stopUnknotting(
        false);

    restoreGeometry(
        previous.saveGeometry());

    mainSplitter_->setSizes(
        previous.mainSplitter_->sizes());

    rightTabs_->setCurrentIndex(
        previous.rightTabs_->currentIndex());

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

    colorButton_->setStyleSheet(previous.colorButton_->styleSheet());

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

    updateLearnSection();
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
    if (!chooseTableKnot())
        return;

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
            QStringLiteral(
                "x=(2+cos(2*t))*cos(3*t);\n"
                "y=(2+cos(2*t))*sin(3*t);\n"
                "z=sin(4*t);"));
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
            updateInvariantPanel(
                KnotMath::computeInvariantsWithCrossings(
                    preset,
                    currentData_.points,
                    currentCrossings_,
                    currentData_.closed,
                    torusPSpin_->value(),
                    torusQSpin_->value(),
                    currentData_.tableIndex));
        }

        hasData_ =
            true;

        rebuildMesh();

        updateLearnSection();
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
    // the moving curve is drawn every frame (with the current thickness)
    if (relaxRunning_)
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
}

void MainWindow::updateInvariantPanel(
    const KnotMath::Invariants &invariants)
{
    if (writheValueLabel_ != nullptr)
    {
        writheValueLabel_->setText(
            QString::number(
                invariants.writhe,
                'f',
                5));
    }

    if (crossingValueLabel_ != nullptr)
    {
        if (invariants.crossingNumber < 0)
        {
            crossingValueLabel_->setText(
                QStringLiteral("—"));
        }
        else if (invariants.knownCrossingNumber >= 0 &&
                 invariants.knownCrossingNumber !=
                     invariants.crossingNumber)
        {
            // The XY diagram is not always a minimal diagram.
            crossingValueLabel_->setText(
                tr("%1 (minimum %2)")
                    .arg(invariants.crossingNumber)
                    .arg(invariants.knownCrossingNumber));
        }
        else
        {
            crossingValueLabel_->setText(
                QString::number(
                    invariants.crossingNumber));
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

    colorButton_->setStyleSheet(
        QStringLiteral(
            "QPushButton {"
            " background:%1;"
            " color:#060606;"
            " border:1px solid #BAB9B4;"
            " font-weight:700;"
            "}"
            "QPushButton:hover {"
            " border:1px solid #BAB9B4;"
            "}")
            .arg(
                meshColor_.name()));

    rebuildMesh();
}

void MainWindow::resetCamera()
{
    if (glWidget_ != nullptr)
        glWidget_->resetCamera();
}

void MainWindow::showError(
    const QString &message)
{
    QMessageBox::critical(
        this,
        tr("AKSIOMA — Knots"),
        tr(
            "The requested curve could not be generated.\n\n%1")
            .arg(
                message));
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
                  ? tr("\u275A\u275A  PAUSE")
                  : (nextStep
                         ? tr("\u25B6  NEXT")
                         : (unknotActive_
                                ? tr("\u25B6  PLAY")
                                : tr("\u25B6  UNKNOT"))));

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

    if (!unknotPlaying_)
        return;

    const double dt =
        static_cast<double>(
            unknotClock_.restart()) /
        1000.0;

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

        // the timer keeps running for the celebration
        unknotCelebrate_ =
            0.0;

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

    KnotMath::Mesh mesh =
        KnotMath::buildTubeMesh(
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

    if (mesh.isValid())
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
                event.position);

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

    // A title and a short explanation for every stage.
    auto explain = [](const QString &title, const QString &body)
    {
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

    unknotStatusLabel_->setText(
        Theme::html(text));

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
 * RELAX AND SCRAMBLE
 * =============================================================
 */

bool MainWindow::prepareRelaxation()
{
    if (!hasData_)
        return false;

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

    // relax_.curve() always fits in a ball of radius 2.7
    const float extent =
        2.7f +
        static_cast<float>(
            thicknessSlider_->value()) /
            100.0f;

    glWidget_->setFixedBounds(
        QVector3D(-extent, -extent, -extent),
        QVector3D(extent, extent, extent));

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

    glWidget_->clearFixedBounds();

    rebuildMesh();

    requestRelaxAnalysis();

    updateRelaxControls();
}

void MainWindow::stopRelaxation()
{
    if (relaxTimer_ != nullptr)
        relaxTimer_->stop();

    const bool wasRunning =
        relaxRunning_;

    relaxRunning_ =
        false;

    relaxChanged_ =
        false;

    relax_ =
        KnotMath::KnotRelaxation();

    // analyses still running are for a curve that is gone
    ++relaxSession_;

    relaxAnalysisWanted_ =
        false;

    if (wasRunning && glWidget_ != nullptr)
        glWidget_->clearFixedBounds();

    if (relaxStatusLabel_ != nullptr)
        relaxStatusLabel_->clear();

    updateRelaxControls();
}

void MainWindow::relaxPlayPause()
{
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
    if (!prepareRelaxation())
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
                updateInvariantPanel(*result);

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
            ? tr("❚❚  PAUSE")
            : tr("▶  RELAX"));

    relaxResetButton_->setEnabled(
        relaxRunning_ || relaxChanged_);
}
