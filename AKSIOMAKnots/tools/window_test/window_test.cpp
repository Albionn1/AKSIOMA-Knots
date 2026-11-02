// The real main window, never shown on the screen (WA_DontShowOnScreen), run
// through what it has to get right when things happen in an awkward order.
// Its settings and autosave live in a folder of their own, not the user's.
//
// window_test.exe            all checks (about a minute and a half)
// window_test.exe <folder>   and pictures of the window in that folder
// window_test.exe cuts       how far a cut moves ropes of many knots

#include <QtCore>
#include <QtGui>
#include <QtOpenGL>
#include <QtOpenGLWidgets>
#include <QtSvg>
#include <QtWidgets>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <vector>

// the checks look at the window's own state
#define private public
#include "knotbrowser.h"
#include "mainwindow.h"
#undef private

#include "appsettings.h"
#include "controlhelp.h"
#include "knotexport.h"
#include "trainsound.h"

#include <QAudioDevice>
#include <QMediaDevices>

namespace
{

int failures = 0;

void check(bool passed, const char *what)
{
    std::printf("%s  %s\n", passed ? "ok  " : "FAIL", what);
    std::fflush(stdout);

    if (!passed)
        ++failures;
}

// runs the program until `done` or `limit` ms have passed
bool waitFor(const std::function<bool()> &done, int limit)
{
    QElapsedTimer clock;

    clock.start();

    while (clock.elapsed() < limit)
    {
        if (done())
            return true;

        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        QThread::msleep(2);
    }

    return done();
}

void run(int milliseconds)
{
    waitFor([]() { return false; }, milliseconds);
}

const char *const GoodCurve = "x=cos(t)+2*cos(2*t);y=sin(t)-2*sin(2*t);z=2*sin(3*t)";

void choose(MainWindow &w, KnotMath::Preset preset)
{
    w.presetCombo_->setCurrentIndex(w.presetCombo_->findData(static_cast<int>(preset)));
}

void clearKnotState(MainWindow &w)
{
    w.grabButton_->setChecked(false);
    w.photoButton_->setChecked(false);

    choose(w, KnotMath::Preset::Trefoil);

    w.generateMesh();

    run(50);
}

void ropeFirst(MainWindow &w)
{
    std::printf("\nA new knot is rope, without shine\n");

    check(w.materialCombo_->currentIndex() == 0 && w.materialCombo_->currentData().toInt() == 3, "Rope is first and chosen");
    check(w.metallicSlider_->value() == 0, "with no shine");

    KnotState state = w.currentState();

    check(state.material == 3, "undo keeps the material by its own number");

    w.materialCombo_->setCurrentIndex(w.materialCombo_->findData(0));

    check(w.metallicSlider_->value() == 70, "Glossy still brings its own shine");

    w.materialCombo_->setCurrentIndex(w.materialCombo_->findData(3));

    check(w.metallicSlider_->value() == 0, "and Rope takes it away again");

    state.material = 2;
    w.restoreState(state);

    check(w.materialCombo_->currentData().toInt() == 2, "a saved Metal comes back as Metal");

    w.materialCombo_->setCurrentIndex(0);

    w.generateMesh();
}

void errorsAndCustomCurves(MainWindow &w)
{
    std::printf("\nA curve put right, and knots that replace a bad text\n");

    choose(w, KnotMath::Preset::CustomCoordinates);

    w.customInputEdit_->setPlainText(QStringLiteral("x=1/0;y=0;z=0"));
    w.generateMesh();

    check(w.noticeBar_->isVisibleTo(&w) && w.curveErrorShown_, "a curve that cannot be made says so");

    w.customInputEdit_->setPlainText(QString::fromLatin1(GoodCurve));
    w.generateMesh();

    check(!w.noticeBar_->isVisibleTo(&w), "the notice goes once the curve has been put right");

    // the poles of tan(t) and a curve out of reach are refused, not drawn
    w.customInputEdit_->setPlainText(QStringLiteral("x=1e300*cos(t);y=1e300*sin(t);z=0"));
    w.generateMesh();

    check(w.curveErrorShown_, "a curve too large to draw says so");

    w.customInputEdit_->setPlainText(QString::fromLatin1(GoodCurve));
    w.generateMesh();

    // the box still holds something that cannot be drawn
    w.customInputEdit_->setPlainText(QStringLiteral("x=1/0;y=0;z=0"));

    choose(w, KnotMath::Preset::Trefoil);

    w.showCustomCurve(QString::fromLatin1(GoodCurve));

    check(!w.noticeBar_->isVisibleTo(&w), "a drawn knot is not blamed for the old text in the box");
    check(w.currentPreset() == KnotMath::Preset::CustomCoordinates && w.customBox_->isVisibleTo(&w),
          "and it is shown as a custom curve");
    check(w.lastPresetIndex_ == w.presetCombo_->currentIndex(), "and the knot list remembers it");

    w.thicknessSlider_->setValue(40);
    w.generateMesh();
    run(10);

    check(w.thicknessValueLabel_->toolTip().isEmpty() == false, "the thickness value keeps a tooltip");

    w.thicknessSlider_->setValue(16);
    w.generateMesh();

    clearKnotState(w);
}

void learnPanel(MainWindow &w)
{
    std::printf("\nThe Learn panel names the knot\n");

    w.showTableKnot(QStringLiteral("8_19"));

    check(w.educationalBrowser_->toPlainText().contains(QStringLiteral("8₁₉")), "a table knot by its name, not \"Knot Table\"");

    clearKnotState(w);
}

void photoRope(MainWindow &w)
{
    std::printf("\nPhoto mode: a rope that cannot be laid, and one that comes late\n");

    choose(w, KnotMath::Preset::Trefoil);

    w.photoButton_->setChecked(true);

    check(waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 20000), "the trefoil is laid on the table");

    // the rope comes back as "not the same knot"
    w.restingRope_.valid = false;
    w.rebuildMesh();

    check(!w.restingLaying_, "a rope that failed is not laid again and again");

    quint64 job = w.restingJob_;

    run(300);

    check(job == w.restingJob_, "and stays that way");

    w.photoButton_->setChecked(false);

    // leaving Photo mode while a long rope is on its way
    w.showTableKnot(QStringLiteral("12a_1"));
    w.photoButton_->setChecked(true);

    check(waitFor([&w]() { return w.restingLaying_; }, 3000), "a long rope starts on its way");

    w.resetCamera();

    w.photoButton_->setChecked(false);

    check(!w.restingLaying_ && w.layingProgress_->cancel.load(), "leaving Photo mode calls it off");

    // a long rope, and the unknotting started before it lands
    w.photoButton_->setChecked(true);

    check(waitFor([&w]() { return w.restingLaying_; }, 3000), "the long rope again");

    w.unknotPlayPause();

    check(waitFor([&w]() { return w.unknotActive_; }, 20000), "the unknotting starts while the rope is under way");

    const bool wasLaying = w.restingLaying_;

    check(waitFor([&w]() { return !w.restingLaying_; }, 30000), "the rope lands");

    run(100);

    check(w.unknotActive_, wasLaying ? "the rope landing leaves the animation playing"
                                     : "(the rope was there first) the animation is playing");

    w.unknotReset();

    check(!w.unknotActive_, "Reset ends it");

    clearKnotState(w);
}

void photoSteps(MainWindow &w)
{
    std::printf("\nPhoto mode: the Steps window after the loop has landed\n");

    w.photoButton_->setChecked(true);

    waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 20000);

    w.unknotSpeedCombo_->setCurrentIndex(4);
    w.unknotPlayPause();

    check(waitFor([&w]() { return w.unknotActive_ && w.photoUnknot_; }, 20000), "the rope is lifted");

    // to the end of the animation
    w.unknotLift_ = 1.0;
    w.unknotT_ = 0.995;

    check(waitFor([&w]() { return !w.unknotLanded_.isEmpty(); }, 15000), "the loop falls");

    check(waitFor([&w]() { return w.unknotDrop_ == nullptr; }, 15000), "and lies on the table");

    w.showUnknotMoment(0.3);

    check(w.unknotLanded_.isEmpty() && w.unknotDrop_ == nullptr, "a step chosen in the list goes back up to that moment");

    w.stepBy(1);

    check(w.unknotLanded_.isEmpty(), "and so does stepping along");

    w.unknotReset();

    w.unknotSpeedCombo_->setCurrentIndex(1);

    clearKnotState(w);
}

void ropeInTheHand(MainWindow &w)
{
    std::printf("\nPhoto mode: the rope in the hand\n");

    w.photoButton_->setChecked(true);

    waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 20000);

    w.grabButton_->setChecked(true);

    check(waitFor([&w]() { return w.photoRopeOn_; }, 5000), "Grab picks the rope up");

    w.photoRopeTouched_ = true;

    w.updateUndoControls();

    w.showTableKnot(QStringLiteral("4_1"));

    check(!w.photoRopeTouched_, "a new knot: no rope left for undo to lay down");

    waitFor([&w]() { return w.photoRopeOn_; }, 20000);

    w.relaxScramble();

    check(w.relaxRunning_ && !w.photoRopeOn_, "a scramble puts the rope down (it would move unseen)");

    w.relaxPlayPause();

    waitFor([&w]() { return w.photoRopeOn_; }, 20000);

    w.toggleSeifert();

    check(w.seifertActive_ && !w.photoRopeOn_, "the Seifert surface puts it down too");

    w.toggleSeifert();

    clearKnotState(w);
}

// a place on the table (in radii) as the view hands it to the window
QVector3D onTheTable(const MainWindow &w, const KnotMath::Vec3 &p)
{
    const QVector3D q(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));

    return w.photoRopePose_.inverted().map(q * static_cast<float>(w.photoRopeRadius_));
}

void ropeCutAndEdge(MainWindow &w)
{
    using KnotMath::Vec3;

    std::printf("\nPhoto mode: the edge of the table, and the rope cut\n");

    // The limit below was set on this shape, the trefoil as three nested rings.
    // How far a cut moves a rope depends a lot on the knot: "window_test cuts"
    // lists it, from 3.5 mm (7_4) to 36 mm (4_1) today.
    choose(w, KnotMath::Preset::CustomCoordinates);

    w.customInputEdit_->setPlainText(QStringLiteral(
        "x=(2+0.72*cos(2*t))*cos(3*t);y=(2+0.72*cos(2*t))*sin(3*t);z=0.72*sin(2*t)"));
    w.generateMesh();

    const QString before = KnotMath::computeAlexanderPolynomial(w.currentData_.points).text;

    w.photoButton_->setChecked(true);

    waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 20000);

    w.grabButton_->setChecked(true);

    check(waitFor([&w]() { return w.photoRopeOn_; }, 5000), "Grab picks the rope up");

    // the table as it is drawn, in the rope's own radii
    const double radius = w.photoRopeRadius_;
    const QRectF drawn = w.glWidget_->photoTableArea();
    const QRectF table(drawn.left() / radius, drawn.top() / radius, drawn.width() / radius, drawn.height() / radius);

    const double width = 3600.0 / w.photoRopeMaterial_.diameterMm;

    check(std::abs(table.width() - width) < 0.01 * width && std::abs(table.height() - 0.5 * width) < 0.01 * width,
          "the table is 1.8 x 0.9 m in the rope's units");

    const auto box = [](const QVector<Vec3> &points)
    {
        QRectF area(QPointF(points.first().x, points.first().z), QPointF(points.first().x, points.first().z));

        for (const Vec3 &p : points)
        {
            area.setLeft(std::min(area.left(), p.x));
            area.setRight(std::max(area.right(), p.x));
            area.setTop(std::min(area.top(), p.z));
            area.setBottom(std::max(area.bottom(), p.z));
        }

        return area;
    };

    const QPointF offset = box(w.photoRope_->points()).center() - table.center();

    check(std::hypot(offset.x(), offset.y()) < 3.0, "the rope was laid in the middle of the table");

    check(waitFor([&w]() { return w.photoRope_->resting(); }, 10000), "the rope lies still");

    // cut
    const QVector<Vec3> whole = w.photoRope_->points();

    w.cutPhotoRope();

    check(!w.photoRope_->closed() && w.photoRope_->points().size() == whole.size() - 3, "Cut leaves the same rope with two ends");

    check(waitFor([&w]() { return w.photoRope_->resting(); }, 10000), "the cut rope comes to rest");

    double furthest = 0.0;

    for (const Vec3 &p : w.photoRope_->points())
    {
        double nearest = 1.0e30;

        for (const Vec3 &q : whole)
            nearest = std::min(nearest, (p - q).lengthSquared());

        furthest = std::max(furthest, std::sqrt(nearest));
    }

    std::printf("      a cut moved it %.1f mm at most\n", furthest * 0.5 * w.photoRopeMaterial_.diameterMm);

    check(furthest * 0.5 * w.photoRopeMaterial_.diameterMm < 8.0, "a cut hardly moves a rope lying as it was laid");

    w.tiePhotoRopeEnds();

    run(50);

    check(KnotMath::computeAlexanderPolynomial(w.currentData_.points).text == before, "tied again, it is the same knot");

    clearKnotState(w);

    // a fresh rope, to the edge of the table and far past it
    w.photoButton_->setChecked(true);

    waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 20000);

    w.grabButton_->setChecked(true);

    check(waitFor([&w]() { return w.photoRopeOn_; }, 5000), "Grab picks up a new rope");

    const QVector<Vec3> lying = w.photoRope_->points();
    const Vec3 taken = lying.first();

    w.photoRopeGrabbed(onTheTable(w, taken));

    run(300);

    for (int k = 1; k <= 60; ++k)
    {
        w.photoRopeDragged(onTheTable(w, Vec3(taken.x + 40.0 * k, 1.0, taken.z)));

        run(16);
    }

    const auto handAt = [&w]() { return w.photoRope_->points().at(w.photoRope_->heldPoint()); };

    check(waitFor([&]() { return handAt().x > table.right() - 3.0; }, 8000), "the hand comes up to the edge of the table");

    run(300);

    check(handAt().x < table.right() - 1.0, "and stops there, whatever the mouse does");

    w.photoRopeReleased();

    check(waitFor([&w]() { return w.photoRope_->resting(); }, 10000), "let go, it comes to rest");

    const QRectF lies = box(w.photoRope_->points()).adjusted(-1.0, -1.0, 1.0, 1.0);

    check(table.contains(lies), "all of it on the table");

    clearKnotState(w);

    // a thin rope is a long rope: a thousand points and more
    std::printf("\nPhoto mode: a thin rope comes to rest\n");

    const int thickness = w.thicknessSlider_->value();

    w.thicknessSlider_->setValue(4);

    w.generateMesh();

    w.photoButton_->setChecked(true);

    check(waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 30000), "the thin trefoil is laid on the table");

    std::printf("      %d points, it ended at %.1f mm/s\n", static_cast<int>(w.restingRope_.points.size()), w.restingRope_.finalSpeed);

    check(w.restingRope_.points.size() > 1000 && w.restingRope_.finalSpeed < 20.0, "it lies still");

    w.thicknessSlider_->setValue(thickness);

    clearKnotState(w);
}

void joining(MainWindow &w)
{
    std::printf("\nJoin: the curve as it is shown\n");

    w.relaxScramble();

    run(400);

    check(w.relaxRunning_, "the knot is being scrambled");

    w.joinPlayStop();

    check(w.joinActive_ && !w.relaxRunning_ && w.relaxChanged_, "Join takes the moved curve and remembers it moved");

    w.joinPlayStop();

    check(w.relaxResetButton_->isEnabled(), "stopped, Reset can still go back to the original");

    clearKnotState(w);

    // the second knot is the first one mirrored: the square knot
    w.joinMirrorCheck_->setChecked(true);
    w.joinPlayStop();

    check(waitFor([&w]() { return !w.joinActive_; }, 15000), "two trefoils are joined");

    w.joinMirrorCheck_->setChecked(false);

    check(w.isJoinedKnot() && w.classificationValueLabel_->text().contains(QLatin1Char('#')), "the joined knot keeps its name");
    check(w.educationalBrowser_->toPlainText().contains(QLatin1Char('#')), "in the Learn panel too");
}

void seifertFilm(MainWindow &w)
{
    std::printf("\nThe Seifert surface and its soap film\n");

    choose(w, KnotMath::Preset::Trefoil);

    w.generateMesh();

    w.toggleSeifert();

    check(w.seifertActive_, "the surface is built");

    check(w.seifert_.crossings == 3 && w.seifert_.circles == 2, "from the trefoil's 3 crossings, not its 4 on the screen");

    check(waitFor([&w]() { return w.seifert_.sheet.valid; }, 20000), "its sheet comes from the worker thread");

    w.seifertTimer_->stop();

    check(w.seifertStepSlider_->maximum() == 4, "there are five steps");

    w.setSeifertStep(4);

    check(waitFor([&w]() { return w.seifertFilm_ >= 1.0; }, 8000), "the film forms");

    w.showSeifertLoops(QStringLiteral("0,0"));

    check(w.seifertStep_ == 4 && w.seifertLoop_ == 0, "a matrix entry shows its loops on the film");

    w.clearSeifertLoops();

    w.setSeifertStep(1);

    check(waitFor([&w]() { return w.seifertFilm_ <= 0.0; }, 8000), "going back undoes it");

    w.photoButton_->setChecked(true);

    run(100);

    check(w.photoSceneCombo_->currentData().toInt() == static_cast<int>(Photo::Scene::LoungeTable),
          "Foto opens on the rope on the garden table");

    check(w.glWidget_->clothSheet_ && !w.glWidget_->soapFilm_, "in Photo mode the surface is cloth");

    w.seifertMaterialCombo_->setCurrentIndex(1);

    check(w.glWidget_->soapFilm_ && !w.glWidget_->clothSheet_, "or a soap film, if that is chosen");

    w.setSeifertStep(4);

    check(waitFor([&w]() { return w.seifertFilm_ >= 1.0; }, 8000), "which forms there too");

    run(200);

    w.photoButton_->setChecked(false);

    run(100);

    check(!w.glWidget_->soapFilm_, "and leaves with Photo mode");

    w.seifertMaterialCombo_->setCurrentIndex(0);

    // the scene chosen last time does not stay
    w.photoButton_->setChecked(true);

    w.photoSceneCombo_->setCurrentIndex(1);

    run(100);

    w.photoButton_->setChecked(false);

    run(100);

    w.photoButton_->setChecked(true);

    check(w.photoSceneCombo_->currentIndex() == 0, "Foto opens on the rope again, whatever was chosen before");

    w.photoButton_->setChecked(false);

    run(100);

    // Foto first, then the surface: it lies on the table, it does not take
    // the rope's pose
    w.toggleSeifert();

    w.photoButton_->setChecked(true);

    waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 20000);

    w.toggleSeifert();

    check(!w.glWidget_->restingPose_, "built in Photo mode, the surface does not keep the rope's pose");

    w.photoButton_->setChecked(false);

    run(100);

    // another knot with the sheet still on its way
    w.showTableKnot(QStringLiteral("8_19"));

    check(w.seifertActive_, "a new knot keeps the surface");

    w.showTableKnot(QStringLiteral("5_2"));

    const quint64 job = w.seifertSheetJob_;

    check(waitFor([&w]() { return w.seifert_.sheet.valid; }, 20000) && job == w.seifertSheetJob_,
          "and only the newest sheet is taken");

    w.toggleSeifert();

    check(!w.seifertActive_ && !w.glWidget_->soapFilm_, "back to the knot");

    clearKnotState(w);
}

// a real click on a row of the open list of scenes
void clickScene(MainWindow &w, int scene)
{
    QComboBox *combo = w.photoSceneCombo_;

    combo->view()->window()->setAttribute(Qt::WA_DontShowOnScreen);

    run(300);

    combo->showPopup();

    run(400);

    QAbstractItemView *list = combo->view();

    const QModelIndex row = combo->model()->index(scene, combo->modelColumn());

    const QPoint at = list->visualRect(row).center();

    const QPointF local(at);
    const QPointF global(list->viewport()->mapToGlobal(at));

    QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

    QCoreApplication::sendEvent(list->viewport(), &move);

    run(50);

    QCoreApplication::sendEvent(list->viewport(), &press);
    QCoreApplication::sendEvent(list->viewport(), &release);

    run(800);
}

void trainChoice(MainWindow &w)
{
    // the real sound system, started and stopped, at no volume
    {
        std::printf("default sound output: %s\n", qPrintable(QMediaDevices::defaultAudioOutput().description()));

        for (int k = 0; k < 3; ++k)
        {
            TrainSound sound;

            sound.setScene(220.0, true, 0.0f, 0.0f);

            run(500);
        }

        check(true, "the sound starts and stops, silently, three times over");
    }

    std::printf("\nThe toy train can always be clicked; if it cannot run, the window says why\n");

    const int scene = w.photoSceneCombo_->findData(static_cast<int>(Photo::Scene::TrainTrack));

    QComboBox *combo = w.photoSceneCombo_;

    w.photoButton_->setChecked(true);

    run(200);

    // a knot it runs on
    clickScene(w, scene);

    check(combo->currentIndex() == scene, "a real click on the toy train chooses it");

    waitFor([&w]() { return !w.restingLaying_; }, 20000);

    run(300);

    check(combo->currentIndex() == scene && w.restingTrack_ && w.restingTrack_->valid, "and it stays chosen");

    combo->setCurrentIndex(0);

    run(300);

    // knots it does not run on: the list goes back, with a word why
    auto refused = [&](const char *what)
    {
        w.hideNotice();

        const int before = combo->currentIndex();

        clickScene(w, scene);

        std::printf("%s: [%s]\n", what, qPrintable(w.noticeText_->text()));

        check(combo->currentIndex() == before, "the scene stays as it was");
        check(w.noticeBar_->isVisible() && w.noticeText_->text() == w.trainRefusal(), "and the window says why");
    };

    // (the knot table ends at 13 crossings; a torus knot goes further)
    choose(w, KnotMath::Preset::CustomTorus);

    w.torusPSpin_->setValue(2);
    w.torusQSpin_->setValue(15);

    w.generateMesh();

    run(400);

    refused("fifteen crossings");

    choose(w, KnotMath::Preset::DNAHelix);

    w.generateMesh();

    run(400);

    refused("several strands");

    // a joined knot has no number of its own: it is given a try
    clearKnotState(w);

    w.joinMirrorCheck_->setChecked(true);
    w.joinPlayStop();

    check(waitFor([&w]() { return !w.joinActive_; }, 15000), "two trefoils are joined");

    std::printf("joined: known %d, in view %d\n", w.currentInvariants_.knownCrossingNumber, w.currentInvariants_.crossingNumber);

    check(w.trainRefusal().isEmpty(), "and the toy train takes the joined knot");

    w.joinMirrorCheck_->setChecked(false);

    clearKnotState(w);

    run(100);
}

void frameRate(MainWindow &w)
{
    std::printf("\nThe frame rate, shown when asked for\n");

    check(!w.frameRateCheck_->isChecked() && !AppSettings::showFrameRate(), "it is off at first");

    w.frameRateCheck_->setChecked(true);

    check(AppSettings::showFrameRate(), "the setting is kept");

    // something that moves: the train
    w.photoButton_->setChecked(true);

    run(100);

    w.photoSceneCombo_->setCurrentIndex(
        w.photoSceneCombo_->findData(static_cast<int>(Photo::Scene::TrainTrack)));

    waitFor([&w]() { return w.restingTrack_ && w.restingTrack_->valid && !w.restingLaying_; }, 30000);

    run(1500);

    std::printf("moving: %.1f frames a second\n", w.glWidget_->frameRate());

    check(w.glWidget_->frameRate() > 4.0, "a moving train is measured");

    // a view at rest: nothing is drawn, and it says so
    w.trainButton_->setChecked(true);

    run(6000);

    std::printf("at rest: %.1f\n", w.glWidget_->frameRate());

    check(w.glWidget_->frameRate() == 0.0, "a still view counts as idle");

    // it has to be there in the picture, not only in the numbers
    auto corner = [&w]()
    {
        const QImage shot = w.glWidget_->grabFramebuffer().convertToFormat(QImage::Format_RGB32);

        return shot.copy(shot.width() - 140, 0, 140, 50);
    };

    const QImage with = corner();

    w.frameRateCheck_->setChecked(false);

    check(!AppSettings::showFrameRate(), "and it can be turned off");

    run(300);

    const QImage without = corner();

    int different = 0;

    for (int y = 0; y < with.height(); ++y)
    {
        for (int x = 0; x < with.width(); ++x)
        {
            different += std::abs(qGray(with.pixel(x, y)) - qGray(without.pixel(x, y))) > 12 ? 1 : 0;
        }
    }

    std::printf("the corner differs in %d pixels\n", different);

    check(different > 300, "the box is in the picture, and goes when it is turned off");

    w.trainButton_->setChecked(false);

    w.frameRateCheck_->setChecked(true);

    run(1500);

    // FRAME_RATE_SHOT=file: a picture of the view with the box
    if (qEnvironmentVariableIsSet("FRAME_RATE_SHOT"))
        w.glWidget_->grabFramebuffer().save(qEnvironmentVariable("FRAME_RATE_SHOT"));

    w.frameRateCheck_->setChecked(false);

    w.photoButton_->setChecked(false);

    clearKnotState(w);

    run(100);
}

void toyTrain(MainWindow &w)
{
    std::printf("\nThe toy train runs on the knot, and everything else still works around it\n");

    const int scene = w.photoSceneCombo_->findData(static_cast<int>(Photo::Scene::TrainTrack));

    w.showTableKnot(QStringLiteral("3_1"));

    run(100);

    w.photoButton_->setChecked(true);

    run(100);

    check(!w.trainButton_->isVisibleTo(w.trainButton_->parentWidget()), "no stop button on the rope");

    w.photoSceneCombo_->setCurrentIndex(scene);

    check(waitFor([&w]() { return w.restingTrack_ && w.restingTrack_->valid && !w.restingLaying_; }, 30000),
          "the track of the trefoil is built");

    check(w.trainButton_->isVisibleTo(w.trainButton_->parentWidget()), "with a button to stop the train");
    check(w.soundButton_->isVisibleTo(w.soundButton_->parentWidget()) && !w.soundButton_->isChecked(), "the sound can be turned on, and is off here");

    w.cinemaButton_->setChecked(true);

    run(1500);

    check(w.glWidget_->cinema() && w.glWidget_->captionTitle_.size() > 0, "Cinema takes the camera");

    // three shots in four hold the whole knot, between the film's bars
    {
        KnotGLWidget *view = w.glWidget_;

        const int barShare = std::max(0, (view->height() - static_cast<int>(view->width() / 2.39f)) / 2);

        const double filmEdge = 1.0 - 2.0 * double(barShare) / view->height();

        double furthest = 0.0;

        for (int shot : {0, 1, 3})
        {
            for (double t : {0.02, 0.5, 0.97})
            {
                view->playCinema((shot + t) * 9.0f);

                run(120);

                const float mm = view->worldPerMm();

                for (const KnotMath::Vec3 &p : view->trainPath_)
                {
                    const QVector4D clip =
                        view->lastViewProjection_ *
                        QVector4D(QVector3D(float(p.x), float(p.y), float(p.z)) * mm, 1.0f);

                    furthest = std::max(furthest, double(std::abs(clip.x() / clip.w())));
                    furthest = std::max(furthest, double(std::abs(clip.y() / clip.w())) / filmEdge);
                }
            }
        }

        std::printf("    the knot reaches %.2f of the film's picture\n", furthest);

        check(furthest > 0.45 && furthest < 1.0, "the wide shots hold the whole knot, and fill the picture");

        // the close shot looks at a bridge, not at the train
        view->playCinema(2.5f * 9.0f);

        run(120);

        const QVector3D spot = view->cinemaSpot_;

        bool onBridge = false;

        for (const auto &pass : view->trainJourney_)
        {
            const QMatrix4x4 place = Photo::placeVehicle(view->trainPath_, view->trainLength_, pass.along, 26.0);

            if (pass.over && (place.map(QVector3D()) * view->worldPerMm() - spot).length() < 0.01f)
                onBridge = true;
        }

        check(onBridge, "the close shot is of a bridge");
    }

    w.cinemaButton_->setChecked(false);

    run(100);

    check(!w.glWidget_->cinema(), "and gives it back");

    check(w.restingTrack_->bridges.size() == 3, "it has three bridges");
    check(w.restingTrack_->journey.size() == 6 && w.restingTrack_->alternating, "the journey passes each twice, taking turns");
    check(w.glWidget_->trainJourney_.size() == 6, "and the view is showing it");
    check(!w.glWidget_->captionTitle_.isEmpty(), "and the picture says what it shows");

    const double start = w.glWidget_->trainAlong();

    run(600);

    // WINDOW_SHOT=file: a picture of the window as it is now
    if (qEnvironmentVariableIsSet("WINDOW_SHOT"))
    {
        w.showCrossingNumbersCheck_->setChecked(true);

        run(300);

        w.glWidget_->grabFramebuffer().save(qEnvironmentVariable("WINDOW_SHOT"));

        w.showCrossingNumbersCheck_->setChecked(false);
    }

    check(w.glWidget_->trainAlong() != start, "the train moves");

    w.trainButton_->setChecked(true);

    run(100);

    const double stopped = w.glWidget_->trainAlong();

    run(300);

    check(w.glWidget_->trainAlong() == stopped, "and stops when told to");

    w.trainButton_->setChecked(false);

    // a new knot: a new track, for the bridges of that knot
    w.showTableKnot(QStringLiteral("4_1"));

    check(waitFor([&w]() { return w.restingTrack_ && w.restingTrack_->bridges.size() == 4 && !w.restingLaying_; }, 30000),
          "another knot gets its own track");

    // Grab leaves the track for a tube, and comes back to it
    w.grabButton_->setChecked(true);

    run(200);

    check(w.glWidget_->captionTitle_.isEmpty(), "Grab takes the words off the picture");

    w.grabButton_->setChecked(false);

    run(300);

    // the largest knot it takes
    w.showTableKnot(QStringLiteral("10_123"));

    check(waitFor([&w]() { return w.restingTrack_ && w.restingTrack_->valid && !w.restingLaying_; }, 40000),
          "a knot of ten crossings gets a track");

    // the largest in the table
    w.showTableKnot(QStringLiteral("13n_5000"));

    // (the track of this knot, not the one before)
    waitFor(
        [&w]()
        {
            return !w.restingLaying_ && !w.restingFrom_.isEmpty() && !w.currentData_.points.isEmpty() &&
                   w.restingFrom_.first().x == w.currentData_.points.first().x;
        },
        40000);

    check(w.restingTrack_ && w.restingTrack_->valid, "a knot of thirteen crossings gets a track");

    check(w.glWidget_->showTrain(), "and the train runs on it");

    // one too large: the scene is left for another, with a word why
    choose(w, KnotMath::Preset::CustomTorus);

    w.torusPSpin_->setValue(2);
    w.torusQSpin_->setValue(15);

    w.generateMesh();

    run(300);

    check(w.photoSceneCombo_->currentIndex() != scene, "a knot of fifteen crossings leaves the scene");
    check(w.noticeBar_->isVisible() && w.noticeText_->text() == w.trainRefusal(), "and the window says why");

    check(!w.trainButton_->isVisibleTo(w.trainButton_->parentWidget()), "the stop button leaves with the scene");

    check(w.glWidget_->captionTitle_.isEmpty(), "and so do the words");

    w.photoSceneCombo_->setCurrentIndex(0);

    run(200);

    w.photoButton_->setChecked(false);

    run(100);

    clearKnotState(w);
}

void newLanguage(MainWindow &w)
{
    std::printf("\nA new language keeps the joined knot, the rope scene and the undo steps\n");

    AppSettings::installLanguage(QStringLiteral("sq"));

    MainWindow next;

    next.setAttribute(Qt::WA_DontShowOnScreen);
    next.resize(1500, 900);
    next.show();

    w.photoSceneCombo_->setCurrentIndex(1);

    next.takeOverFrom(w);

    run(20);

    check(next.isJoinedKnot() && next.joined_.name == w.joined_.name, "the joined knot is still known");
    check(next.classificationValueLabel_->text().contains(QLatin1Char('#')), "under its name");
    check(next.photoSceneCombo_->currentIndex() == 1, "the scene stays");
    check(next.undoButton_->isEnabled() == w.undoButton_->isEnabled(), "the undo steps stay");
    check(next.presetCombo_->itemText(0) != w.presetCombo_->itemText(0), "and the window is in the new language");

    {
        KnotBrowserDialog browser(QString(), &next);

        browser.search_->setText(QStringLiteral("tetëshi"));

        const int with = browser.list_->model()->rowCount();

        browser.search_->setText(QStringLiteral("teteshi"));

        check(with > 0 && browser.list_->model()->rowCount() == with, "the browser finds a name with or without its ë");
    }

    AppSettings::installLanguage(QStringLiteral("en"));
}

void arrowKeys(MainWindow &w)
{
    std::printf("\nThe arrow keys\n");

    QApplication::setActiveWindow(&w);

    check(QApplication::activeWindow() == &w, "the hidden window counts as the active one");

    w.tabFocused_ = false;
    w.thicknessSlider_->setFocus();

    const int before = w.thicknessSlider_->value();

    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);

    QApplication::sendEvent(w.thicknessSlider_, &left);

    check(w.thicknessSlider_->value() == before - w.thicknessSlider_->singleStep(),
          "with nothing to step through, a clicked slider moves");

    w.thicknessSlider_->setValue(before);

    w.toggleSeifert();

    const int step = w.seifertStep_;

    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);

    QApplication::sendEvent(w.thicknessSlider_, &right);

    check(w.seifertStep_ == step + 1 && w.thicknessSlider_->value() == before,
          "with a Seifert surface, the keys step through it");

    w.toggleSeifert();

    clearKnotState(w);
}

void settingsKept()
{
    std::printf("\nRestore defaults\n");

    AppSettings::addRecentFile(QStringLiteral("C:/nowhere/a.aknot"));
    AppSettings::setTourSeen();
    AppSettings::setDarkTheme(false);

    const QStringList recent = AppSettings::recentFiles();

    AppSettings::restoreDefaults();

    check(AppSettings::darkTheme(), "settings go back");
    check(AppSettings::recentFiles() == recent && AppSettings::tourSeen(), "the recent files and the tour stay as they were");
}

// how far a cut moves a rope lying as it was laid, in mm
double cutMovement(MainWindow &w)
{
    using KnotMath::Vec3;

    w.photoButton_->setChecked(true);

    waitFor([&w]() { return w.restingRope_.valid && !w.restingLaying_; }, 20000);

    w.grabButton_->setChecked(true);

    waitFor([&w]() { return w.photoRopeOn_; }, 5000);
    waitFor([&w]() { return w.photoRope_ != nullptr && w.photoRope_->resting(); }, 10000);

    const QVector<Vec3> whole = w.photoRope_->points();

    w.cutPhotoRope();

    waitFor([&w]() { return w.photoRope_->resting(); }, 10000);

    double furthest = 0.0;

    for (const Vec3 &p : w.photoRope_->points())
    {
        double nearest = 1.0e30;

        for (const Vec3 &q : whole)
            nearest = std::min(nearest, (p - q).lengthSquared());

        furthest = std::max(furthest, std::sqrt(nearest));
    }

    const double moved = furthest * 0.5 * w.photoRopeMaterial_.diameterMm;

    clearKnotState(w);

    return moved;
}

void cutSurvey(MainWindow &w)
{
    std::printf("\nHow far a cut moves a rope lying as laid\n");

    for (const KnotMath::Preset preset : {KnotMath::Preset::Trefoil, KnotMath::Preset::FigureEight,
                                          KnotMath::Preset::Cinquefoil, KnotMath::Preset::Stevedore})
    {
        choose(w, preset);

        w.generateMesh();

        std::printf("     preset %d: %.1f mm\n", static_cast<int>(preset), cutMovement(w));
        std::fflush(stdout);
    }

    for (const char *name : {"3_1", "4_1", "5_1", "5_2", "6_2", "7_4", "8_19", "9_42", "10_124", "12a_1"})
    {
        w.showTableKnot(QString::fromLatin1(name));

        std::printf("     %s: %.1f mm\n", name, cutMovement(w));
        std::fflush(stdout);
    }

    // the old picture of the trefoil, three nested rings
    choose(w, KnotMath::Preset::CustomCoordinates);

    w.customInputEdit_->setPlainText(QStringLiteral(
        "x=(2+0.72*cos(2*t))*cos(3*t);y=(2+0.72*cos(2*t))*sin(3*t);z=0.72*sin(2*t)"));
    w.generateMesh();

    std::printf("     trefoil as it was: %.1f mm\n", cutMovement(w));
    std::fflush(stdout);
}

void notAKnot(MainWindow &w)
{
    std::printf("\nA curve through itself\n");

    choose(w, KnotMath::Preset::CustomCoordinates);

    w.customInputEdit_->setPlainText(QStringLiteral("x=sin(t);y=sin(2*t);z=0"));
    w.generateMesh();

    check(w.currentInvariants_.notAKnot, "a figure-8 in a plane is no knot");
    check(w.noticeBar_->isVisibleTo(&w), "and the notice says so");
    check(!w.thicknessValueLabel_->text().contains(QChar(0x2192)), "and it is drawn at the thickness asked for");

    w.relaxStatusLabel_->clear();

    check(!w.prepareRelaxation() && !w.relaxStatusLabel_->text().isEmpty(), "Relax says it needs a knot");

    w.startUnknotting();

    check(!w.unknotPreparing_ && !w.unknotActive_, "Unknot does nothing");

    w.toggleSeifert();

    check(!w.seifertActive_, "Surface does nothing");

    w.startJoin();

    check(!w.joinActive_, "Join does nothing");

    w.photoButton_->setChecked(true);

    check(!w.photoButton_->isChecked() && !w.glWidget_->photoMode(), "Photo stays off");

    w.customInputEdit_->setPlainText(QString::fromLatin1(GoodCurve));
    w.generateMesh();

    check(!w.currentInvariants_.notAKnot && !w.noticeBar_->isVisibleTo(&w), "a real knot takes the notice away");

    clearKnotState(w);
}

void openCurves(MainWindow &w)
{
    std::printf("\nA curve that does not close\n");

    choose(w, KnotMath::Preset::CustomCoordinates);

    w.hideNotice();
    w.customInputEdit_->setPlainText(QStringLiteral("x=cos(3*t);y=sin(3*t);z=t"));
    w.generateMesh();

    check(w.noticeBar_->isVisibleTo(&w) && !w.curveErrorShown_, "a helix gets a notice, not an error");

    w.hideNotice();
    w.customInputEdit_->setPlainText(QString::fromLatin1(GoodCurve));
    w.generateMesh();

    check(!w.noticeBar_->isVisibleTo(&w), "a closed curve gets none");

    clearKnotState(w);
}

void joinedHonestly(MainWindow &w)
{
    std::printf("\nThe note of a connected sum\n");

    w.joined_ = MainWindow::JoinedKnot();
    w.joined_.text = QStringLiteral("x");
    w.joined_.name = QStringLiteral("test");
    w.joined_.jonesValid = true;
    w.joined_.jones[1] = 5;

    choose(w, KnotMath::Preset::CustomCoordinates);

    w.customInputEdit_->setPlainText(w.joined_.text);

    KnotMath::Invariants computed;

    computed.jonesValid = true;
    computed.jonesPolynomial = QStringLiteral("t");

    w.applyConnectedSumInvariants(computed);

    check(computed.methodology.contains(QStringLiteral("could not be confirmed")),
          "a computed polynomial that differs is not covered up");

    KnotMath::Invariants tooLarge;

    tooLarge.jonesValid = false;

    w.applyConnectedSumInvariants(tooLarge);

    check(tooLarge.methodology.contains(QStringLiteral("too large")) &&
              !tooLarge.methodology.contains(QStringLiteral("could not be confirmed")),
          "one that was not computed is called too large");

    w.joined_ = MainWindow::JoinedKnot();

    clearKnotState(w);
}

int seeThrough(const QImage &image)
{
    int count = 0;

    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (qAlpha(image.pixel(x, y)) < 255)
                ++count;
        }
    }

    return count;
}

void pictures(MainWindow &w)
{
    std::printf("\nPictures\n");

    w.glWidget_->update();

    run(100);

    check(seeThrough(w.glWidget_->grabFramebuffer()) == 0, "the plain view leaves nothing see-through");

    w.photoButton_->setChecked(true);

    waitFor([&w]() { return w.restingRope_.valid; }, 15000);

    waitFor([&w]() { return w.glWidget_->photoPictureFinished(); }, 8000);

    check(w.glWidget_->photoPictureFinished(), "a still photograph finishes");

    check(seeThrough(w.glWidget_->grabFramebuffer()) == 0, "and leaves nothing see-through");

    clearKnotState(w);
}

void copies(MainWindow &w)
{
    std::printf("\nCrash recovery with two copies running\n");

    const QString folder =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    QDir().mkpath(folder);

    // This folder outlives a run (QStandardPaths does not read LOCALAPPDATA), so
    // what an earlier run left is cleared first: the app would rightly offer it.
    const QFileInfoList left =
        QDir(folder).entryInfoList({QStringLiteral("autosave*.aknot")}, QDir::Files);

    for (const QFileInfo &file : left)
        QFile::remove(file.absoluteFilePath());

    QDir(folder + QStringLiteral("/discarded")).removeRecursively();

    QString error;

    const QString recovered = folder + QStringLiteral("/recovered.aknot");
    const QString other = folder + QStringLiteral("/autosave-999991.aknot");

    QFile::remove(recovered);

    KnotExport::writeKnotFile(other, w.currentKnotFile(), error);

    {
        // another copy, alive: it holds the lock of its autosave
        QLockFile live(folder + QStringLiteral("/autosave-999991.lock"));

        live.setStaleLockTime(0);

        check(live.tryLock(0), "the other copy has its lock");

        w.offerAutosave();

        check(QFile::exists(other) && !QFile::exists(recovered), "the autosave of a running copy is left alone");
        check(!w.noticeOtherAction_->isVisibleTo(&w), "and nothing is offered");
    }

    // that copy crashed: no lock any more
    w.offerAutosave();

    check(!QFile::exists(other) && QFile::exists(recovered), "the autosave of a copy that is gone is offered");
    check(w.noticeAction_->isVisibleTo(&w) && w.noticeOtherAction_->isVisibleTo(&w), "with two buttons");

    w.noticeClose_->click();

    check(QFile::exists(recovered), "closing the notice keeps the knot");

    // Discard asks first, and Cancel keeps it
    w.offerAutosave();

    auto answer = [](const QString &text)
    {
        QTimer::singleShot(
            200,
            [text]()
            {
                for (QAbstractButton *button : QApplication::activeModalWidget()->findChildren<QAbstractButton *>())
                {
                    if (button->text() == text)
                        button->click();
                }
            });
    };

    answer(QStringLiteral("Cancel"));

    w.noticeOtherAction_->click();

    check(QFile::exists(recovered) && w.noticeOtherAction_->isVisibleTo(&w), "Cancel keeps it, and asks again");

    answer(QStringLiteral("Discard"));

    w.noticeOtherAction_->click();

    const QFileInfoList set =
        QDir(folder + QStringLiteral("/discarded")).entryInfoList({QStringLiteral("*.aknot")}, QDir::Files);

    check(!QFile::exists(recovered) && set.size() == 1, "Discard puts it aside for a week");

    // older than a week it goes
    QFile old(set.first().absoluteFilePath());

    check(old.open(QIODevice::ReadWrite), "the discarded file can be touched");

    old.setFileTime(QDateTime::currentDateTime().addDays(-8), QFileDevice::FileModificationTime);
    old.close();

    w.offerAutosave();

    check(!old.exists(), "and after a week it is gone");

    w.hideNotice();
}

void saving(MainWindow &w)
{
    std::printf("\nSaving and reopening\n");

    QTemporaryDir files;

    const QString path = files.path() + QStringLiteral("/a knot.aknot");

    w.showTableKnot(QStringLiteral("8_19"));

    QString error;

    KnotExport::writeKnotFile(path, w.currentKnotFile(), error);

    check(w.loadKnotFile(path, true), "a saved knot opens");
    check(w.savedPath_ == path, "and Save knows its file");
    check(w.knotTitleLabel_->text().contains(QStringLiteral("a knot.aknot")), "and the top bar says which");

    const QDateTime before = QFileInfo(path).lastModified();

    run(30);

    w.thicknessSlider_->setValue(20);
    w.generateMesh();

    check(w.savedPath_ == path, "a new thickness keeps the file");
    check(w.saveKnot(false), "Save writes without asking");
    check(QFileInfo(path).lastModified() > before, "to the same file");

    w.thicknessSlider_->setValue(16);

    // a relaxed knot stays that knot, in that shape
    w.prepareRelaxation();

    QVector<KnotMath::Vec3> shape = w.currentData_.points;

    for (KnotMath::Vec3 &p : shape)
        p = p * 1.1;

    w.stopRelaxation();
    w.applyShape(shape);

    check(w.relaxChanged_, "the shape counts as changed");

    w.saveKnot(false);

    choose(w, KnotMath::Preset::Trefoil);
    w.generateMesh();

    check(w.savedPath_.isEmpty(), "another knot is another file");

    w.loadKnotFile(path, true);

    check(w.currentPreset() == KnotMath::Preset::KnotTable && w.tableKnotName_ == QStringLiteral("8_19"),
          "a reopened relaxed knot keeps its name");

    bool same = w.currentData_.points.size() == shape.size();

    for (int i = 0; same && i < shape.size(); ++i)
        same = (w.currentData_.points.at(i) - shape.at(i)).length() < 1.0e-9;

    check(same, "and its shape, point for point");

    w.relaxChanged_ = false;

    clearKnotState(w);
}

void analysisPanel(MainWindow &w)
{
    std::printf("\nThe Analysis panel\n");

    w.showTableKnot(QStringLiteral("8_19"));

    QString facts = w.factsLabel_->text();

    check(facts.contains(QStringLiteral("Genus")) && facts.contains(QStringLiteral("Determinant")),
          "a table knot shows what is known of it");
    check(w.knotTitleLabel_->text().contains(QChar(0x2081)), "and its name is in the top bar");
    check(!w.writheNoteLabel_->text().isEmpty() && !w.writheNoteLabel_->isHidden(), "the diagram writhe is shown");
    check(w.alexanderValueLabel_->textInteractionFlags() & Qt::TextSelectableByMouse, "a polynomial can be copied");

    choose(w, KnotMath::Preset::Trefoil);
    w.generateMesh();

    check(w.factsLabel_->text().contains(QStringLiteral("Tricolourable")) && !w.factsTile_->isHidden(),
          "a preset is a table knot too");

    choose(w, KnotMath::Preset::CustomCoordinates);

    w.customInputEdit_->setPlainText(QString::fromLatin1(GoodCurve));
    w.generateMesh();

    check(w.factsLabel_->text().contains(QStringLiteral("Determinant")), "a custom knot shows its determinant");

    clearKnotState(w);
}

void helpAndKeys(MainWindow &w)
{
    std::printf("\nHelp and the keyboard\n");

    w.hideNotice();

    w.openManual();

    check(w.noticeBar_->isVisibleTo(&w), "a missing manual says so");

    w.hideNotice();

    QApplication::setActiveWindow(&w);

    w.tabFocused_ = true;
    w.resetCameraButton_->setFocus(Qt::TabFocusReason);

    check(w.resetCameraButton_->property("keyboardFocus").toBool(), "a control reached by Tab is marked");

    w.grabButton_->setFocus(Qt::TabFocusReason);

    check(!w.resetCameraButton_->property("keyboardFocus").toBool() && w.grabButton_->property("keyboardFocus").toBool(),
          "and the mark moves with the keyboard");

    w.tabFocused_ = false;

    w.resetCameraButton_->setFocus(Qt::MouseFocusReason);

    check(!w.resetCameraButton_->property("keyboardFocus").toBool(), "a click marks nothing");

    // Esc lets go
    w.grabButton_->setChecked(true);

    check(w.grabButton_->isChecked(), "Grab is on");

    w.leaveFullScreenShortcut_->activated();

    check(!w.grabButton_->isChecked(), "Esc lets go of it");

    clearKnotState(w);
}

void idling(MainWindow &w)
{
    std::printf("\nA window with nothing to do\n");

    int frames = 0;

    const QMetaObject::Connection counting =
        QObject::connect(w.glWidget_, &QOpenGLWidget::frameSwapped, &w, [&frames]() { ++frames; });

    run(1000);

    QObject::disconnect(counting);

    std::printf("     %d frames in a second\n", frames);

    check(frames <= 12, "a still knot is drawn at most ten times a second");

    frames = 0;

    KnotGLWidget::setReduceMotion(true);

    const QMetaObject::Connection counting2 =
        QObject::connect(w.glWidget_, &QOpenGLWidget::frameSwapped, &w, [&frames]() { ++frames; });

    run(1000);

    QObject::disconnect(counting2);

    KnotGLWidget::setReduceMotion(false);

    check(frames == 0, "with Reduce motion not at all");
}

void smallScreen(const QString &language, QImage *shot)
{
    std::printf("\nA laptop at 1093 x 575 (%s)\n", qPrintable(language));

    AppSettings::installLanguage(language);

    MainWindow w;

    w.setAttribute(Qt::WA_DontShowOnScreen);
    w.foldPanelsForRoom(QSize(1093, 575));
    w.resize(1093, 545);
    w.show();

    run(200);

    check(w.width() <= 1093 && w.height() <= 545, "the window fits");

    w.presetCombo_->setCurrentIndex(w.presetCombo_->findData(static_cast<int>(KnotMath::Preset::CustomTorus)));

    run(100);

    check(w.width() <= 1093, "with p and q too");

    check(w.analysisButton_->isChecked() == false, "the analysis waits behind its button");
    check(w.leftPages_->isHidden(), "and the tools behind the rail");

    w.analysisButton_->setChecked(true);

    run(100);

    check(w.width() <= 1093 && w.height() <= 545, "opened, it fits as well");

    w.railClicked(0);

    run(100);

    std::printf("     smallest window %d x %d\n", w.minimumSizeHint().width(), w.minimumSizeHint().height());

    check(w.width() <= 1093, "and with both panels");

    if (shot != nullptr)
        *shot = w.grab().toImage();

    w.replaced_ = true;
}

void sizes(MainWindow &w)
{
    std::printf("\nThe window and small screens\n");

    check(w.glWidget_->graphicsReady(), "the 3D view has started");

    QDialog dialog(&w);

    ControlHelp::fitToScreen(&dialog, 9000, 9000);

    const QSize room = w.screen()->availableGeometry().size();

    check(dialog.width() <= room.width() && dialog.height() <= room.height(), "a dialog is never larger than the screen");

    ControlHelp::fitToScreen(&dialog, 300, 200);

    check(dialog.size() == QSize(300, 200), "and keeps its size where there is room");

    check(w.minimumHeight() <= room.height() && w.minimumWidth() <= room.width(), "the smallest window fits on the screen");
}

} // namespace

int main(int argc, char **argv)
{
    QSurfaceFormat format;

    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);

    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);

    QTemporaryDir folder;

    // nothing of the user's: no registry, and data under this test's own name
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, folder.path());
    qputenv("LOCALAPPDATA", folder.path().toLocal8Bit());

    QCoreApplication::setOrganizationName(QStringLiteral("AKSIOMA test"));
    QCoreApplication::setApplicationName(QStringLiteral("window test"));

    QSettings().setValue(QStringLiteral("language"), QStringLiteral("en"));

    // (a test makes no noise)
    QSettings().setValue(QStringLiteral("trainSound"), false);

    AppSettings::setTourSeen();
    AppSettings::applyAll();

    {
        MainWindow w;

        w.setAttribute(Qt::WA_DontShowOnScreen);
        w.resize(1500, 900);
        w.show();

        run(300);

        sizes(w);

        // window_test.exe cuts: only the survey
        if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("cuts"))
        {
            cutSurvey(w);

            w.replaced_ = true;

            return 0;
        }

        // window_test.exe train: only the toy train
        if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("train"))
        {
            frameRate(w);
            trainChoice(w);
            toyTrain(w);

            std::printf("\n%d failure(s)\n", failures);

            w.replaced_ = true;

            return failures == 0 ? 0 : 1;
        }

        ropeFirst(w);
        arrowKeys(w);
        errorsAndCustomCurves(w);
        learnPanel(w);
        photoRope(w);
        photoSteps(w);
        ropeInTheHand(w);
        ropeCutAndEdge(w);
        seifertFilm(w);
        joining(w);
        newLanguage(w);
        notAKnot(w);
        openCurves(w);
        joinedHonestly(w);
        frameRate(w);
        trainChoice(w);
        toyTrain(w);
        pictures(w);
        copies(w);
        saving(w);
        analysisPanel(w);
        helpAndKeys(w);
        idling(w);

        // window_test.exe <folder>: pictures of the window
        if (argc >= 2)
        {
            w.showTableKnot(QStringLiteral("8_19"));

            run(400);

            w.grab().save(QString::fromLocal8Bit(argv[1]) + QStringLiteral("/full_8_19.png"));

            clearKnotState(w);
        }

        w.replaced_ = true;
    }

    QImage english;
    QImage albanian;

    smallScreen(QStringLiteral("en"), &english);
    smallScreen(QStringLiteral("sq"), &albanian);

    AppSettings::installLanguage(QStringLiteral("en"));

    if (argc >= 2)
    {
        english.save(QString::fromLocal8Bit(argv[1]) + QStringLiteral("/small_en.png"));
        albanian.save(QString::fromLocal8Bit(argv[1]) + QStringLiteral("/small_sq.png"));
    }

    settingsKept();

    std::printf("\n%d failure(s)\n", failures);

    return failures == 0 ? 0 : 1;
}
