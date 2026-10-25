#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "knotglwidget.h"
#include "knotmath.h"
#include "knotrelax.h"

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
#include <QElapsedTimer>

class QCheckBox;
class QDialog;
class QGroupBox;
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

    void updateLearnSection();

    void drawKnotDialog();

    // Relax and scramble (knotrelax.h)
    void relaxPlayPause();
    void relaxScramble();
    void relaxReset();
    void relaxTick();

    // Unknotting animation
    void unknotPlayPause();
    void unknotReset();
    void unknotTick();
    void unknotScrubbed(
        int value);

private:
    // knot choice in the top bar: preset, T(p,q), Browse, Draw
    QWidget *createKnotBar(
        QWidget *parent);

    QWidget *createLeftPanel();

    QWidget *createRightPanel();

    QWidget *createSettingsPanel();

    // F11; Esc leaves full screen
    void toggleFullScreen();

    // Full screen once the window is on screen (start, new language).
    void showFullScreenLater();

    // Settings window: theme and text size apply at once, a new language
    // needs a new window (all texts are set during construction).
    void applyThemeSettings();
    void rebuildWindow();

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

    QDialog *settingsDialog_ =
        nullptr;

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

    QPushButton *resetCameraButton_ =
        nullptr;

    QLabel *writheValueLabel_ =
        nullptr;

    QLabel *crossingValueLabel_ =
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

    QVector<KnotMath::CrossingInfo>
        currentCrossings_;

    QColor meshColor_ =
        QColor(
            QStringLiteral(
                "#BAB9B4"));
};

#endif // MAINWINDOW_H
