#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "knotglwidget.h"
#include "knotjoin.h"
#include "knotmath.h"
#include "knotrelax.h"
#include "knotseifert.h"

#include <QColor>
#include <QComboBox>
#include <QLabel>
#include <QPointer>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QVector>
#include <QVector3D>
#include <QElapsedTimer>
#include <QPair>

#include <map>

class QCheckBox;
class QDialog;
class QGroupBox;
class QProgressBar;
class QShortcut;
class QTabWidget;
class TimelineSlider;
class UnknotSummaryDialog;
class QSplitter;

class MainWindow final
    : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(
        QWidget *parent = nullptr);

    ~MainWindow() override = default;

    // Shows the window with the saved size and position, or full screen.
    void showInitially();

    // Opens a knot file (.aknot or a coordinate list), e.g. one given on
    // the command line by a double click in Explorer.
    void openKnotFilePath(
        const QString &path);

protected:
    void closeEvent(
        QCloseEvent *event) override;

    void changeEvent(
        QEvent *event) override;

private slots:
    void generateMesh();

    void presetChanged(
        int index);

    void chooseColor();

    void resetCamera();

    // Photo button and scene list -> the 3D view
    void applyPhotoMode();

    void updateLearnSection();

    void drawKnotDialog();

    // Relax and scramble (knotrelax.h)
    void relaxPlayPause();
    void relaxScramble();
    void relaxReset();
    void relaxTick();

    // Grab and pull the rope with the mouse (KnotRelaxation::beginPull)
    void grabToggled(
        bool enabled);
    void ropeGrabbed(
        const QVector3D &point);
    void ropeDragged(
        const QVector3D &point);
    void ropeReleased();

    // Unknotting animation
    void unknotPlayPause();
    void unknotReset();
    void unknotTick();
    void unknotScrubbed(
        int value);

    // Joining two knots (connected sum, knotjoin.h)
    void joinPlayStop();
    void joinTick();

private:
    // knot choice in the top bar: preset, T(p,q), Browse, Draw
    QWidget *createKnotBar(
        QWidget *parent);

    QWidget *createLeftPanel();

    QWidget *createRightPanel();

    QWidget *createSettingsPanel();

    // Settings, Lighting: presets, lights and shadows turned on and off,
    // and the video memory meter.
    QWidget *createLightingPanel();

    // Shows, saves and reflects new lighting options.
    void applyLighting(
        const Lighting::Options &options);

    void updateLightingControls();

    void updateMemoryMeter();

    // F11; Esc leaves full screen
    void toggleFullScreen();

    // Full screen once the window is on screen (start, new language).
    void showFullScreenLater();

    // Settings window: theme and text size apply at once, a new language
    // needs a new window (all texts are set during construction).
    void applyThemeSettings();
    void rebuildWindow();

    // Side panel widths for the text size: wider with larger text, and
    // never narrower than their tabs.
    void fitPanelsToText();

    // Takes over the knot, the view settings and the window placement.
    void takeOverFrom(
        MainWindow &previous);

    void updateSettingsControls();

    QString buildEducationalHtml() const;

    void updateInvariantPanel(
        const KnotMath::Invariants &invariants);

    void updateCustomInputVisibility();

    void applyDefaultCustomText(
        int presetIndex);

    void showError(
        const QString &message);

    KnotMath::Preset currentPreset() const;

    // Rebuilds only the tube mesh from the cached curve (thickness, colour
    // mode, colours) - no new curve, no new crossing detection.
    void rebuildMesh();

    // Material choice and shine slider -> metallic and roughness.
    void applySurface();

    // Knot Browser: lets the user pick a knot from the KnotInfo table;
    // true if one was chosen (tableKnotName_).
    bool chooseTableKnot();

    // "Browse all knots" button: choose a knot and show it.
    void openKnotBrowser();

    // Practice window (tricolouring, crossing signs) on the current knot.
    void openPractice();

    // File menu (knotexport.h): knot files and exports of the current knot.
    void openKnotFile();
    void saveKnotFile();
    void exportViewImage();
    void exportDiagram();
    void exportCoordinates();
    void exportReport();

    // Seifert surface (knotseifert.h): shown in the 3D view instead of the
    // knot, built up in four steps.
    void toggleSeifert();
    void stopSeifert();
    void renderSeifert();
    void setSeifertStep(
        int step);
    void updateSeifertInfo();

    // A clicked entry "i,j" of the Seifert matrix shows its two loops on
    // the surface; the same entry again, or "clear", removes them.
    void showSeifertLoops(
        const QString &link);
    void clearSeifertLoops();

    void showAbout();

    void showSettings();

    // Tube colour button shows the current colour.
    void updateColorSwatch();

    // Debounced update after slider changes.
    void scheduleUpdate(
        bool fullRegenerate);

    // Relax helpers
    // Starts the engine from the current curve if needed; false (with a
    // message) if the curve cannot be relaxed.
    bool prepareRelaxation();
    // Starts the timer; the curve moves from the next tick on.
    void runRelaxation();
    // Stops moving and keeps the curve as it is (the tube gets the usual
    // colours and the analysis panel its values).
    void pauseRelaxation();
    // Forgets the moved curve (a new curve was generated).
    void stopRelaxation();
    void renderRelaxFrame();
    // Recomputes the invariants of the moving curve on a worker thread;
    // at most one runs at a time, the newest curve is taken next.
    void requestRelaxAnalysis();
    void updateRelaxControls();

    // Grab helpers
    // Gives the 3D view the curve on screen to grab (none if it is not a
    // single closed curve).
    void updateGrabCurve();
    // Keeps the floor where it is while the rope is pulled about.
    void fixPullBounds();
    bool grabEnabled() const;

    // Unknotting animation helpers
    // Builds the animation on a worker thread; playback starts in
    // finishUnknotting() once it is ready.
    void startUnknotting();
    void finishUnknotting(
        quint64 buildId,
        const KnotMath::UnknottingAnimation &animation,
        double requestedRadius);
    void stopUnknotting(
        bool restoreMesh);
    void renderUnknotFrame();
    void setUnknotControlsEnabled(
        bool playing);

    // The window listing every crossing change of the animation (opens by
    // itself when the animation has played to the end).
    void showUnknotSummary();

    // Pauses the animation and shows it at time t (0..1).
    void showUnknotMoment(
        double t);

private:
    QSplitter *mainSplitter_ =
        nullptr;

    QTabWidget *leftTabs_ =
        nullptr;

    QTabWidget *rightTabs_ =
        nullptr;

    // text size the panel widths were last set for
    int panelTextScale_ =
        0;

    QDialog *settingsDialog_ =
        nullptr;

    // the knot the camera was last framed for (generateMesh): a new knot
    // is framed, a new tube density or thickness keeps the view
    QString framedKnot_;

    QComboBox *languageCombo_ =
        nullptr;

    QComboBox *themeCombo_ =
        nullptr;

    QComboBox *textSizeCombo_ =
        nullptr;

    QCheckBox *startFullScreenCheck_ =
        nullptr;

    QCheckBox *rememberWindowCheck_ =
        nullptr;

    QPushButton *fullScreenButton_ =
        nullptr;

    QComboBox *lightingPresetCombo_ =
        nullptr;

    QLabel *lightingPresetText_ =
        nullptr;

    // every on/off check box with the option it switches
    QVector<QPair<QCheckBox *, bool Lighting::Options::*>> lightingChecks_;

    QSlider *brightnessSlider_ =
        nullptr;

    QLabel *brightnessValue_ =
        nullptr;

    QSlider *shadowSoftnessSlider_ =
        nullptr;

    QLabel *shadowSoftnessValue_ =
        nullptr;

    QComboBox *shadowQualityCombo_ =
        nullptr;

    QLabel *memoryAdapterLabel_ =
        nullptr;

    QProgressBar *memoryBar_ =
        nullptr;

    QLabel *memoryUsedLabel_ =
        nullptr;

    QLabel *memoryViewLabel_ =
        nullptr;

    // refreshes the meter while the Settings window is open
    QTimer *memoryTimer_ =
        nullptr;

    QShortcut *leaveFullScreenShortcut_ =
        nullptr;

    KnotGLWidget *glWidget_ =
        nullptr;

    QComboBox *presetCombo_ =
        nullptr;

    QTextEdit *customInputEdit_ =
        nullptr;

    QSpinBox *torusPSpin_ =
        nullptr;

    QSpinBox *torusQSpin_ =
        nullptr;

    QSlider *thicknessSlider_ =
        nullptr;

    QSlider *meshDensitySlider_ =
        nullptr;

    QLabel *thicknessValueLabel_ =
        nullptr;

    QLabel *meshDensityValueLabel_ =
        nullptr;

    // Glossy, Satin, Metal, Rope
    QComboBox *materialCombo_ =
        nullptr;

    QPushButton *browseKnotsButton_ =
        nullptr;

    // KnotInfo name of the knot shown with Preset::KnotTable ("8_19")
    QString tableKnotName_;

    // preset to go back to when choosing a table knot is cancelled
    int lastPresetIndex_ =
        0;

    QComboBox *colorModeCombo_ =
        nullptr;

    QCheckBox *showCrossingNumbersCheck_ =
        nullptr;

    QCheckBox *autoRotationCheck_ =
        nullptr;

    QSlider *metallicSlider_ =
        nullptr;

    QLabel *metallicValueLabel_ =
        nullptr;

    QPushButton *colorButton_ =
        nullptr;

    QPushButton *generateButton_ =
        nullptr;

    QPushButton *leftGenerateButton_ =
        nullptr;

    // Photo mode (photoscene.h): on and off, and where the knot lies
    QPushButton *photoButton_ =
        nullptr;

    QComboBox *photoSceneCombo_ =
        nullptr;

    QPushButton *resetCameraButton_ =
        nullptr;

    QLabel *writheValueLabel_ =
        nullptr;

    QLabel *crossingValueLabel_ =
        nullptr;

    // under the crossing count: the least possible, when the diagram on
    // screen has more crossings
    QLabel *crossingNoteLabel_ =
        nullptr;

    QLabel *classificationValueLabel_ =
        nullptr;

    QLabel *alexanderValueLabel_ =
        nullptr;

    QLabel *jonesValueLabel_ =
        nullptr;

    QLabel *methodologyValueLabel_ =
        nullptr;

    QTextBrowser *educationalBrowser_ =
        nullptr;

    QTimer *rebuildTimer_ =
        nullptr;

    QWidget *unknotGroup_ =
        nullptr;

    QWidget *torusRow_ =
        nullptr;

    QWidget *customBox_ =
        nullptr;

    QPushButton *unknotPlayButton_ =
        nullptr;

    QPushButton *unknotResetButton_ =
        nullptr;

    QPushButton *unknotSummaryButton_ =
        nullptr;

    QPointer<UnknotSummaryDialog> unknotSummary_;

    // marks the stages and crossing changes
    TimelineSlider *unknotScrubSlider_ =
        nullptr;

    QComboBox *unknotSpeedCombo_ =
        nullptr;

    // pause at the start of every stage and every crossing change
    QCheckBox *unknotStepCheck_ =
        nullptr;

    QLabel *unknotStatusLabel_ =
        nullptr;

    // Joining two knots: the current knot and a second one become their
    // connected sum. The animation places them side by side, cuts an arc
    // out of each, grows the band between the ends and fuses them into one
    // knot (Relax can then let it settle into its own shape).
    void startJoin();

    // `restoreKnot`: show the current knot again
    void stopJoin(
        bool restoreKnot = true);

    void renderJoinFrame();
    void updateJoinStatus();
    void finishJoin();

    // the knot symbol of the current knot ("3₁", "T(2,5)", "3₁ # 4₁")
    QString currentKnotSymbol() const;

    // a knot made by joining: what it is made of, and the invariants that
    // follow from its parts
    struct JoinedKnot
    {
        // the Custom Curve text of the joined knot (it is the current knot
        // while the text is unchanged)
        QString text;

        // "3₁ # 4₁" and a remark ("the granny knot")
        QString name;
        QString remark;

        bool alexanderValid = false;
        std::map<int, long long> alexander;

        bool jonesValid = false;
        std::map<int, long long> jones;
    };

    // shows the joined knot's own name and the invariants of its parts
    void applyConnectedSumInvariants(
        KnotMath::Invariants &invariants) const;

    QComboBox *joinSecondCombo_ =
        nullptr;

    QCheckBox *joinMirrorCheck_ =
        nullptr;

    QPushButton *joinButton_ =
        nullptr;

    QLabel *joinStatusLabel_ =
        nullptr;

    QTimer *joinTimer_ =
        nullptr;

    QElapsedTimer joinClock_;

    bool joinActive_ =
        false;

    // seconds into the animation, and the stage shown in the status
    double joinTime_ =
        0.0;

    int joinStage_ =
        -1;

    KnotMath::ConnectedSum join_;

    double joinTubeRadius_ =
        0.16;

    // the last table knot chosen as the second knot
    QString joinTableName_ =
        QStringLiteral("3_1");

    JoinedKnot pendingJoined_;
    JoinedKnot joined_;

    QTimer *unknotTimer_ =
        nullptr;

    // Relax and scramble
    QPushButton *relaxPlayButton_ =
        nullptr;

    QPushButton *scrambleButton_ =
        nullptr;

    QPushButton *relaxResetButton_ =
        nullptr;

    QLabel *relaxStatusLabel_ =
        nullptr;

    // Grab the rope (checkable)
    QPushButton *grabButton_ =
        nullptr;

    // the rope is held with the mouse (relax_ is pulling)
    bool pulling_ =
        false;

    QTimer *relaxTimer_ =
        nullptr;

    QElapsedTimer relaxClock_;

    KnotMath::KnotRelaxation relax_;

    KnotMath::TubeFrameHint relaxFrameHint_;

    // tube radius of the last frame (follows the safe radius smoothly;
    // negative: no frame yet)
    double relaxShownRadius_ =
        -1.0;

    bool relaxRunning_ =
        false;

    // currentData_ holds a relaxed or scrambled curve (Reset brings back
    // the original)
    bool relaxChanged_ =
        false;

    // after settling the knot turns to the view with the fewest crossings:
    // the angle still to turn (negative: the view is not chosen yet)
    KnotMath::Vec3 relaxTurnAxis_;

    double relaxTurnLeft_ =
        -1.0;

    // results of an analysis for an older curve are ignored
    quint64 relaxSession_ =
        0;

    bool relaxAnalysing_ =
        false;

    bool relaxAnalysisWanted_ =
        false;

    QElapsedTimer unknotClock_;

    KnotMath::UnknottingAnimation unknotAnimation_;

    // keeps the tube orientation continuous from frame to frame
    KnotMath::TubeFrameHint unknotFrameHint_;

    // the diagram shown in the corner, and the times (0..1) where the
    // step-by-step mode pauses
    KnotMath::UnknottingAnimation::Diagram unknotDiagram_;
    QVector<double> unknotStops_;

    bool unknotActive_ =
        false;

    bool unknotPlaying_ =
        false;

    // true while a worker thread builds the animation
    bool unknotPreparing_ =
        false;

    // Identifies the newest build request; results of older ones are
    // ignored (reset or a new curve while preparing).
    quint64 unknotBuildId_ =
        0;

    double unknotT_ =
        0.0;

    double unknotRadius_ =
        0.1;

    // the tube radius that was asked for: the final circle grows back to
    // it (unknotRadius_ may be thinner, to keep the strands apart)
    double unknotRequestedRadius_ =
        0.1;

    // start of the Flatten stage (0..1)
    double unknotFlattenStart_ =
        1.0;

    // 0..1 during the short celebration after the animation has played to
    // the end, negative otherwise
    double unknotCelebrate_ =
        -1.0;

    bool pendingFullRegenerate_ =
        false;

    bool hasData_ =
        false;

    KnotMath::CurveData currentData_;

    // what the analysis panel shows (for the report)
    KnotMath::Invariants currentInvariants_;

    // Seifert surface mode
    bool seifertActive_ =
        false;

    // the knot changed since the surface was built
    bool seifertDirty_ =
        true;

    int seifertStep_ =
        0;

    KnotMath::SeifertSurface seifert_;

    // entry V[i][j] whose loops are shown (-1: none): loop i on the
    // surface, loop j lifted off it
    int seifertLoop_ =
        -1;

    int seifertLifted_ =
        -1;

    // goes through the steps after "Build"
    QTimer *seifertTimer_ =
        nullptr;

    QPushButton *seifertButton_ =
        nullptr;

    QSlider *seifertStepSlider_ =
        nullptr;

    QLabel *seifertStepTitle_ =
        nullptr;

    QLabel *seifertStepText_ =
        nullptr;

    QLabel *seifertInfoLabel_ =
        nullptr;

    QVector<KnotMath::CrossingInfo>
        currentCrossings_;

    QColor meshColor_ =
        QColor(
            QStringLiteral(
                "#BAB9B4"));
};

#endif // MAINWINDOW_H
