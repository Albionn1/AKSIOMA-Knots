#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "knotexport.h"
#include "knotglwidget.h"
#include "knothistory.h"
#include "knotjoin.h"
#include "knotmath.h"
#include "knotrelax.h"
#include "knotseifert.h"
#include "ropephysics.h"
#include "sneaker.h"

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

#include <functional>
#include <future>
#include <memory>
#include <map>

class QButtonGroup;
class QCheckBox;
class QDialog;
class QFrame;
class QGroupBox;
class QMenu;
class QProgressBar;
class QShortcut;
class QStackedWidget;
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

    bool eventFilter(
        QObject *object,
        QEvent *event) override;

    // a knot file dropped on the window
    void dragEnterEvent(
        QDragEnterEvent *event) override;

    void dropEvent(
        QDropEvent *event) override;

private slots:
    void generateMesh();

    void presetChanged(
        int index);

    void chooseColor();

    void resetCamera();

    // Photo button and scene list -> the 3D view
    void applyPhotoMode();

    // Photo mode, the knot standing still: it falls onto the table as a
    // rope of the scene's material (tubes of `radius`) and is shown at
    // rest. False when it is not shown that way (Photo mode off, an
    // animation or a pull, several strands, or the rope could not be
    // laid down as the same knot).
    bool showRestingRope(double radius);

    // lays the current knot on the table (as a rope of tubes of `radius`)
    // on a worker thread; rebuildMesh draws it once it lies still
    void layRestingRope(double radius);

    /*
     * Photo mode, the rope in the hand. With Grab on, the rope lying on
     * the table is picked up with the mouse and moves by its own physics
     * (Photo::RopeSimulation, in units of its radius on the table of
     * restingRope_.pose). Held up it goes over the other strands, pushed
     * down with Shift under them. Cut, it has two ends and can be tied
     * into another knot: after every move its ends are joined over the
     * top and the knot is named; "Tie the ends" makes it the knot shown.
     * The rope stays as it was left while the knot, its thickness and
     * the scene stay the same.
     */
    bool startPhotoRope();
    void stopPhotoRope();

    // the rope in the hand instead of the knot laid down afresh (false if
    // there is none for this knot, thickness and scene)
    bool showKeptPhotoRope(double radius);

    // draws the rope where it is now (`quick` while it moves)
    void showPhotoRope(bool quick);

    void photoRopeTick();

    // waits for the helper thread's surface of the rope and drops it
    void finishPhotoRopeJob();
    void photoRopeGrabbed(const QVector3D &point);
    void photoRopeDragged(const QVector3D &point);
    void photoRopeReleased();
    void cutPhotoRope();
    void tiePhotoRopeEnds();
    void requestPhotoRopeAnalysis();
    void updatePhotoRopeButtons();

    // the rope `table` (on the table, in radii) in the knot's
    // coordinates; open and `joined`, its ends joined by an arch over
    // everything else
    QVector<KnotMath::Vec3> photoRopeCurve(QVector<KnotMath::Vec3> table, bool closed, bool joined) const;

    // the rope's material frames (on the table) in the knot's coordinates
    QVector<KnotMath::Vec3> photoRopeFrames(const QVector<KnotMath::Vec3> &table) const;

    // The Sneaker scene takes knots of up to 7 crossings (one closed
    // strand): larger knots do not fit on a shoe.
    bool knotFitsSneaker() const;

    // the Sneaker scene greyed out for other knots (and left for the
    // shoelace on the floor if it was chosen)
    void updateSneakerChoice();

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

    /*
     * Photo mode: the unknotting is done with the rope of the scene. It is
     * lifted off the table, the camera turns so the sun is behind it, the
     * knot is undone in the air with a caption on the picture telling
     * what happens and why, and at the end the loop falls back onto the
     * table by its own weight. False (the usual tube) when the rope is
     * not lying on a table.
     */
    bool startPhotoUnknot();
    void stopPhotoUnknot();

    // the rope (centre line in the knot's coordinates, where it is) with
    // the strands of a crossing change tinted: signed `highlight` per
    // point of `along` points, towards `over` or `under`
    void showPhotoUnknotRope(
        const QVector<KnotMath::Vec3> &curve,
        const QVector<float> &highlight,
        int along,
        const QColor &over,
        const QColor &under);

    // the finished loop let go: it falls onto the table (false once it
    // lies still)
    void startUnknotDrop();
    bool advanceUnknotDrop(double seconds);
    void unknotScrubbed(
        int value);

    // Joining two knots (connected sum, knotjoin.h)
    void joinPlayStop();
    void joinTick();

private:
    // knot choice in the top bar: preset, T(p,q), Browse, Draw
    QWidget *createKnotBar(
        QWidget *parent);

    // the tools down the left edge, one page of createLeftPanel each
    QWidget *createRail(
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

    void updatePhotoQualityNote();

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

    // Opens the page of a tool; the tool already open folds its panel
    // away, which leaves the room to the 3D view.
    void railClicked(
        int page);

    // Marks the tools at work and shows the playback bar when it is wanted.
    void updateRailActivity();

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

    /*
     * Undo and redo (knothistory.h). Anything that changes the knot or its
     * look calls noteChange(); once things have been quiet for a moment the
     * state is recorded as one step, so a slider dragged across its range
     * is one step, not fifty. The rope in the hand is not a step: undo
     * lays it down afresh.
     */
    void noteChange();
    void recordState();
    KnotState currentState() const;
    void restoreState(const KnotState &state);
    void undo();
    void redo();
    void updateUndoControls();

    // the moved curve as the current knot, with its analysis
    void applyShape(const QVector<KnotMath::Vec3> &shape);

    /*
     * Work that exists nowhere else: a drawn, joined, tied or moved knot.
     * A knot from the table can always be had again, so it asks nothing.
     */
    bool worthKeeping() const;
    KnotExport::KnotFile currentKnotFile() const;

    // true when written; false when cancelled or failed (it says so)
    bool saveKnot();
    void writeAutosave();
    void offerAutosave();

    // opens a file; `remembered`: it counts as the file of this knot
    void loadKnotFile(const QString &path, bool remembered);

    /*
     * The bar over the 3D view: what happened and what can be done about
     * it, without stopping the work the way a window in the way does. Plain
     * notes go by themselves; errors stay until read.
     */
    enum class Notice
    {
        Info,
        Error,
        Busy
    };

    void showNotice(
        const QString &text,
        Notice kind,
        const QString &actionText = QString(),
        const std::function<void()> &action = nullptr);

    void hideNotice();
    void placeNotice();

    // says where a file went, with the way to it
    void showSaved(const QString &path);

    // laying a rope: progress in the bar, after a moment
    void updateLayingProgress();

    void restoreWorkspace();
    void saveWorkspace();

    // Space: what the open tool plays; left and right: a step
    void playPause();
    void stepBy(int direction);

    // F1 or ?: every key, and Ctrl+K: find anything
    void showShortcuts();
    void openCommandPalette();

    // a knot of the table as the current knot
    void showTableKnot(const QString &name);

    void fillRecentMenu();
    void openRecentFile(const QString &path);

    // the first-run tour: three steps, each pointing at its control
    void startTour();
    void showTourStep();
    void endTour();
    void placeTour();

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
    KnotHistory history_;

    QTimer *historyTimer_ =
        nullptr;

    // states are being put back, or taken over: nothing to record
    bool restoring_ =
        false;

    // this window is being replaced (a new language): it asks nothing
    bool replaced_ =
        false;

    // the rope in the hand was moved or cut
    bool photoRopeTouched_ =
        false;

    QAction *undoAction_ =
        nullptr;

    QAction *redoAction_ =
        nullptr;

    QPushButton *undoButton_ =
        nullptr;

    QPushButton *redoButton_ =
        nullptr;

    QMenu *fileMenu_ =
        nullptr;

    QMenu *recentMenu_ =
        nullptr;

    QWidget *railWidget_ =
        nullptr;

    // the keyboard is on a control because it was tabbed to, so Space and
    // the arrows belong to that control
    bool tabFocused_ =
        false;

    QFrame *tourCard_ =
        nullptr;

    QLabel *tourTitle_ =
        nullptr;

    QLabel *tourText_ =
        nullptr;

    QLabel *tourCount_ =
        nullptr;

    QPushButton *tourNext_ =
        nullptr;

    QPushButton *tourSkip_ =
        nullptr;

    int tourStep_ =
        -1;

    QFrame *noticeBar_ =
        nullptr;

    QLabel *noticeText_ =
        nullptr;

    QProgressBar *noticeProgress_ =
        nullptr;

    QPushButton *noticeAction_ =
        nullptr;

    QPushButton *noticeClose_ =
        nullptr;

    QTimer *noticeTimer_ =
        nullptr;

    std::function<void()> noticeDo_;

    // closing the notice decides something (an unrestored knot is let go)
    std::function<void()> noticeDismissed_;

    std::shared_ptr<Photo::RopeProgress> layingProgress_;
    QTimer *layingTimer_ =
        nullptr;
    QElapsedTimer layingClock_;

    QSplitter *mainSplitter_ =
        nullptr;

    QStackedWidget *leftPages_ =
        nullptr;

    QButtonGroup *railGroup_ =
        nullptr;

    // playback under the 3D view: the unknotting animation
    QWidget *transportBar_ =
        nullptr;

    // shows and hides rightTabs_
    QPushButton *analysisButton_ =
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

    // Photo mode's quality, and what it is working at now
    QComboBox *photoQualityCombo_ =
        nullptr;

    QLabel *photoQualityNow_ =
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

    // the last knot laid on the table, and what it was laid down from
    // (the same curve, thickness and scene are not simulated again)
    Photo::RestingRope restingRope_;
    QVector<KnotMath::Vec3> restingFrom_;
    double restingRadius_ = 0.0;
    int restingScene_ = -1;

    // the Sneaker scene: where the knot lies in the whole lace
    Photo::TiedLace restingTied_;

    // a rope being laid down on a worker thread (layRestingRope): which
    // job is the newest, and what it was asked for
    bool restingLaying_ = false;

    // the scene and knot the camera was last framed for, with the rope down
    QString restingFramed_;
    quint64 restingJob_ = 0;
    QVector<KnotMath::Vec3> restingJobFrom_;
    double restingJobRadius_ = 0.0;
    int restingJobScene_ = -1;

    // the rope in the hand (see startPhotoRope), what it was picked up
    // from, and the hand's height above the table (radii)
    std::unique_ptr<Photo::RopeInHand> photoRope_;
    bool photoRopeOn_ = false;
    QVector<KnotMath::Vec3> photoRopeFrom_;
    double photoRopeRadius_ = 0.0;
    int photoRopeScene_ = -1;
    QMatrix4x4 photoRopePose_;
    Photo::RopeMaterial photoRopeMaterial_;
    QTimer *photoRopeTimer_ = nullptr;
    QElapsedTimer photoRopeClock_;
    quint64 photoRopeShown_ = 0;
    double photoRopeLoose_ = 0.0;
    bool photoRopeAnalysing_ = false;
    bool photoRopeAnalysisWanted_ = false;
    quint64 photoRopeSession_ = 0;

    /*
     * While the rope moves, its surface is built on a helper thread from
     * the newest copy of it (photoRopeShown_: the version of the rope
     * last sent) while the view draws the last one.
     */
    struct PhotoRopeFrame
    {
        KnotMath::Mesh mesh;
        QVector<KnotMath::Vec3> curve;
        bool closed = true;
    };

    std::future<PhotoRopeFrame> photoRopeJob_;

    QPushButton *cutRopeButton_ = nullptr;
    QPushButton *tieEndsButton_ = nullptr;

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

    // Photo mode's unknotting (startPhotoUnknot): on; how far the rope has
    // been lifted (0..1) from where it lay; how far up it is unknotted (in
    // the knot's coordinates); the loop falling at the end, and where it
    // came to lie; the rope's thickness, turn and material
    bool photoUnknot_ = false;
    double unknotLift_ = 1.0;
    QVector<KnotMath::Vec3> unknotLiftFrom_;
    KnotMath::Vec3 unknotAir_;
    std::unique_ptr<Photo::RopeSimulation> unknotDrop_;
    double unknotDropTime_ = 0.0;
    QVector<KnotMath::Vec3> unknotLanded_;
    double unknotRopeRadius_ = 0.0;
    double unknotRopeLength_ = 0.0;

    // the camera: close on the knot while it is undone, further out from
    // the moment it is pulled open (unknotWideFrom_) for the opened loop
    // (unknotWideCentre_, unknotWideRadius_ in the knot's coordinates)
    double unknotWideFrom_ = 1.0;
    QVector3D unknotCloseCentre_;
    float unknotCloseRadius_ = 1.0f;
    QVector3D unknotWideCentre_;
    float unknotWideRadius_ = 1.0f;
    bool unknotWideShot_ = false;
    QMatrix4x4 unknotPose_;
    Photo::RopeMaterial unknotMaterial_;

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
