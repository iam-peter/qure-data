// End-to-end tests of the editor: a real PatchWindow, real QML, real scene
// graph – driven with synthetic mouse/keyboard events.
//
// The Session runs with Audio::Silent, so no sound device is ever opened
// (DSP can be switched on safely). CTest runs this under xvfb-run when
// available, so nothing appears on the developer's desktop.

#include "CanvasView.hpp"
#include "ConsoleModel.hpp"
#include "NodeHandle.hpp"
#include "PatchDocument.hpp"
#include "Session.hpp"

#include "PdText.hpp"

#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(QurePlugin)

using namespace qure;
using namespace qure::ui;

class CanvasTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<Session> session_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    PatchDocument* doc_ = nullptr;
    QQuickWindow* window_ = nullptr;
    CanvasView* view_ = nullptr;

    Canvas& canvas() { return doc_->rootCanvas(); }

    /// Canvas coordinates → window coordinates.
    QPoint at(QPointF canvasPos) const {
        const QPointF item = (canvasPos - view_->origin()) * view_->zoom();
        return view_->mapToScene(item).toPoint();
    }
    QPoint centerOf(const Box& b) const { return at(QPointF(b.x + b.w / 2, b.y + b.h / 2)); }
    QPoint outletOf(const Box& b, int outlet) const {
        const int n = b.numOutlets();
        const double x = n > 1 ? b.x + (b.w - 8) * outlet / (n - 1) + 4 : b.x + 4;
        return at(QPointF(x, b.y + b.h - 1));
    }

    Box& add(const char* text, double x, double y, BoxKind kind = BoxKind::Object) {
        Box* b = doc_->createBox(canvas(), kind, QPointF(x, y), QString::fromLatin1(text));
        Q_ASSERT(b);
        return *b;
    }

    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) {
        QTest::mousePress(window_, Qt::LeftButton, mods, from);
        for (int i = 1; i <= 5; ++i)
            QTest::mouseMove(window_, from + (to - from) * i / 5);
        QTest::mouseRelease(window_, Qt::LeftButton, mods, to);
    }

    /// QTest::keyClicks only exists for widgets; type into the window.
    void type(const char* text) {
        for (const char* c = text; *c; ++c)
            QTest::keyClick(window_, *c);
    }

    bool consoleContains(const QString& text) const { return session_->console()->allText().contains(text); }

private slots:
    void initTestCase() { session_ = std::make_unique<Session>(nullptr, Session::Audio::Silent); }

    void init() {
        doc_ = session_->newPatch();
        engine_ = std::make_unique<QQmlApplicationEngine>();
        engine_->setInitialProperties({{QStringLiteral("canvasRef"), QVariant::fromValue(doc_->root())}});
        engine_->loadFromModule("Qure", "PatchWindow");
        QVERIFY(!engine_->rootObjects().isEmpty());
        window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
        QVERIFY(window_);
        window_->resize(900, 640);
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        // Menu shortcuts (Ctrl+1, Ctrl+D...) only fire in the active window.
        window_->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window_));
        view_ = window_->findChild<CanvasView*>();
        QVERIFY(view_);
        view_->forceActiveFocus();
    }

    void cleanup() {
        engine_.reset(); // destroys the window and its views first
        session_->closeDocument(doc_);
        doc_ = nullptr;
    }

    void cleanupTestCase() { session_.reset(); }

    // ---------------------------------------------------------------------

    void typesANewObject() {
        view_->setEditMode(true);
        // A click on empty canvas, then the shortcut: the box goes there.
        // (A plain mouse move would do on X11, but synthetic moves produce
        // no hover events on Windows and macOS.)
        QTest::mouseClick(window_, Qt::LeftButton, {}, at(QPointF(100, 80)));
        QTest::keyClick(window_, Qt::Key_1, Qt::ControlModifier);
        type("osc~ 440");
        QTest::keyClick(window_, Qt::Key_Return);

        QCOMPARE(canvas().boxes().size(), 1u);
        const Box& b = *canvas().boxes().front();
        QCOMPARE(QString::fromStdString(b.displayText()), QStringLiteral("osc~ 440"));
        QVERIFY(b.object);
        QVERIFY(!b.broken());
        QCOMPARE(b.x, 100.0);
        QCOMPARE(b.y, 80.0);
    }

    void autopatchesConsecutiveObjects() {
        view_->setEditMode(true);
        QTest::mouseMove(window_, at(QPointF(60, 60)));
        for (const char* text : {"osc~ 440", "*~ 0.1", "dac~"}) {
            QTest::keyClick(window_, Qt::Key_1, Qt::ControlModifier);
            type(text);
            QTest::keyClick(window_, Qt::Key_Return);
        }
        QCOMPARE(canvas().boxes().size(), 3u);
        QCOMPARE(canvas().connections().size(), 2u);
        // Each new box goes below the previous one.
        QVERIFY(canvas().boxes()[1]->y > canvas().boxes()[0]->y);
        QVERIFY(canvas().boxes()[2]->y > canvas().boxes()[1]->y);
    }

    void escapeCancelsPlacement() {
        view_->setEditMode(true);
        QTest::keyClick(window_, Qt::Key_1, Qt::ControlModifier);
        type("metro 100");
        QTest::keyClick(window_, Qt::Key_Escape);
        QCOMPARE(canvas().boxes().size(), 0u);
        QVERIFY(!doc_->canUndo());
    }

    void connectsByDraggingFromAnOutlet() {
        Box& f = add("f 1", 50, 50);
        Box& p = add("print", 50, 150);
        view_->setEditMode(true);
        drag(outletOf(f, 0), centerOf(p));
        QCOMPARE(canvas().connections().size(), 1u);
        QVERIFY(canvas().isConnected(f, 0, p, 0));
        doc_->undo();
        QCOMPARE(canvas().connections().size(), 0u);
    }

    void refusesSignalIntoControlInlet() {
        Box& osc = add("osc~ 1", 50, 50);
        Box& p = add("print", 50, 150);
        view_->setEditMode(true);
        drag(outletOf(osc, 0), centerOf(p));
        QCOMPARE(canvas().connections().size(), 0u);
    }

    void movesBoxesAndUndoes() {
        Box& b = add("t b b", 40, 40);
        view_->setEditMode(true);
        drag(centerOf(b), centerOf(b) + QPoint(30, 20));
        QCOMPARE(b.x, 70.0);
        QCOMPARE(b.y, 60.0);
        doc_->undo();
        QCOMPARE(b.x, 40.0);
        QCOMPARE(b.y, 40.0);
        doc_->redo();
        QCOMPARE(b.x, 70.0);
    }

    void rubberBandDeleteAndUndoRestoresConnections() {
        Box& a = add("f", 40, 40);
        Box& b = add("+ 1", 40, 100);
        Box& c = add("print", 200, 100);
        QVERIFY(doc_->connectBoxes(canvas(), a.id, 0, b.id, 0));
        QVERIFY(doc_->connectBoxes(canvas(), b.id, 0, c.id, 0));
        const BoxId bid = b.id;
        view_->setEditMode(true);
        // Select [f] and [+ 1] with a rubber band, then delete.
        drag(at(QPointF(20, 20)), at(QPointF(120, 130)));
        QCOMPARE(view_->selectionCount(), 2);
        QTest::keyClick(window_, Qt::Key_Delete);
        QCOMPARE(canvas().boxes().size(), 1u);
        QCOMPARE(canvas().connections().size(), 0u);

        doc_->undo();
        QCOMPARE(canvas().boxes().size(), 3u);
        QCOMPARE(canvas().connections().size(), 2u);
        QVERIFY(canvas().findBox(bid)); // same identity after undo
    }

    void editsAnExistingBoxKeepingConnections() {
        Box& a = add("f", 40, 40);
        Box& b = add("print one", 40, 120);
        // Undo restores boxes as new objects with the same id: hold ids,
        // not references, across undo/redo.
        const BoxId id = b.id;
        QVERIFY(doc_->connectBoxes(canvas(), a.id, 0, id, 0));
        view_->setEditMode(true);
        QTest::mouseDClick(window_, Qt::LeftButton, {}, centerOf(b));
        QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
        type("print two");
        QTest::keyClick(window_, Qt::Key_Return);
        QVERIFY(canvas().findBox(id));
        QCOMPARE(QString::fromStdString(canvas().findBox(id)->displayText()), QStringLiteral("print two"));
        QCOMPARE(canvas().connections().size(), 1u);
        doc_->undo();
        QVERIFY(canvas().findBox(id));
        QCOMPARE(QString::fromStdString(canvas().findBox(id)->displayText()), QStringLiteral("print one"));
        QCOMPARE(canvas().connections().size(), 1u);
    }

    void duplicatesSelection() {
        Box& a = add("f", 40, 40);
        Box& b = add("+ 1", 40, 100);
        QVERIFY(doc_->connectBoxes(canvas(), a.id, 0, b.id, 0));
        view_->setEditMode(true);
        QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window_, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(canvas().boxes().size(), 4u);
        QCOMPARE(canvas().connections().size(), 2u);
        QCOMPARE(view_->selectionCount(), 2);
    }

    void clickingAMessageBoxSendsIt() {
        add("; qure-ui-test 42", 40, 40, BoxKind::Message);
        Box& r = add("r qure-ui-test", 40, 120);
        Box& p = add("print got", 40, 160);
        QVERIFY(doc_->connectBoxes(canvas(), r.id, 0, p.id, 0));
        view_->setEditMode(false);
        QTest::mouseClick(window_, Qt::LeftButton, {}, centerOf(*canvas().boxes().front()));
        QTRY_VERIFY_WITH_TIMEOUT(consoleContains(QStringLiteral("got: 42")), 3000);
    }

    void toggleFrontEndRoundTripsThroughTheEngine() {
        Box& t = add("tgl 19 0 empty empty empty 17 7 0 10 #fcfcfc #000000 #000000 0 1", 60, 60);
        view_->setEditMode(false);
        QTRY_COMPARE_WITH_TIMEOUT(view_->liveWidgets(), 1, 2000);
        QTest::mouseClick(window_, Qt::LeftButton, {}, centerOf(t));
        NodeHandle* h = doc_->handleFor(t);
        QTRY_COMPARE_WITH_TIMEOUT(h->value().toDouble(), 1.0, 3000);
        QTest::mouseClick(window_, Qt::LeftButton, {}, centerOf(t));
        QTRY_COMPARE_WITH_TIMEOUT(h->value().toDouble(), 0.0, 3000);
    }

    void frontEndOnlyKeyboardSendsNotes() {
        Box& k = add("kbd 2 60", 40, 40);
        Box& p = add("print key", 40, 140);
        QVERIFY(doc_->connectBoxes(canvas(), k.id, 0, p.id, 0));
        view_->setEditMode(false);
        QTRY_COMPARE_WITH_TIMEOUT(view_->liveWidgets(), 1, 2000);
        // Lowest white key: middle C.
        const QPoint key = at(QPointF(k.x + 3, k.y + k.h - 5));
        QTest::mousePress(window_, Qt::LeftButton, {}, key);
        QTest::mouseRelease(window_, Qt::LeftButton, {}, key);
        QTRY_VERIFY_WITH_TIMEOUT(consoleContains(QStringLiteral("key: 60 100")), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(consoleContains(QStringLiteral("key: 60 0")), 3000);
    }

    void ctrlWheelZoomsAroundTheCursor() {
        const QPoint cursor(300, 200);
        const QPointF before = QPointF(cursor - view_->mapToScene({0, 0}).toPoint()) / view_->zoom() + view_->origin();
        QWheelEvent wheel(cursor, window_->mapToGlobal(cursor), {}, QPoint(0, 240), Qt::NoButton,
                          Qt::ControlModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window_, &wheel);
        QVERIFY(view_->zoom() > 1.2);
        const QPointF after = QPointF(cursor - view_->mapToScene({0, 0}).toPoint()) / view_->zoom() + view_->origin();
        QVERIFY((before - after).manhattanLength() < 1.0);
    }

    void rendersBoxesOnTheGpu() {
        add("osc~ 440", 40, 40);
        QTRY_VERIFY(window_->isExposed());
        const QImage img = window_->grabWindow();
        QVERIFY(!img.isNull());
        // The box border must differ from the plain canvas background.
        const QPoint border = view_->mapToScene(QPointF(40, 50) - view_->origin()).toPoint();
        const QPoint empty = view_->mapToScene(QPointF(400, 400) - view_->origin()).toPoint();
        const qreal dpr = img.devicePixelRatio();
        QVERIFY(img.pixelColor(border * dpr) != img.pixelColor(empty * dpr));
    }

    void savesAndReloads() {
        Box& a = add("osc~ 220", 40, 40);
        Box& b = add("dac~", 40, 100);
        QVERIFY(doc_->connectBoxes(canvas(), a.id, 0, b.id, 0));
        QTemporaryDir dir;
        const QUrl url = QUrl::fromLocalFile(dir.filePath(QStringLiteral("saved.qd")));
        QVERIFY(doc_->saveAs(url));
        QVERIFY(!doc_->modified());
        PatchDocument* reopened = session_->openPatch(url);
        QVERIFY(reopened);
        QCOMPARE(reopened, doc_); // already open: same document
        QFile f(dir.filePath(QStringLiteral("saved.qd")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(text.contains("#X obj 40 40 osc~ 220;"));
        QVERIFY(text.contains("#X connect 0 0 1 0;"));
    }
};

QTEST_MAIN(CanvasTest)
#include "test_canvas.moc"
