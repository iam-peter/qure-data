// The example projects as a user meets them: opened from the Examples menu
// path (copied out of the binary into the data directory, with their own
// node packs), shown in a PatchWindow, and played with the mouse.
//
// Silent audio and a private display, like test_canvas.cpp.

#include "CanvasView.hpp"
#include "ConsoleModel.hpp"
#include "PatchDocument.hpp"
#include "Session.hpp"

#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(QurePlugin)

using namespace qure;
using namespace qure::ui;

class ExamplesTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<Session> session_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    PatchDocument* doc_ = nullptr;
    QQuickWindow* window_ = nullptr;
    CanvasView* view_ = nullptr;

    void open(const QString& path) {
        doc_ = session_->openExample(path);
        QVERIFY2(doc_, qPrintable(session_->console()->allText()));
        engine_ = std::make_unique<QQmlApplicationEngine>();
        engine_->setInitialProperties({{QStringLiteral("canvasRef"), QVariant::fromValue(doc_->root())}});
        engine_->loadFromModule("Qure", "PatchWindow");
        QVERIFY(!engine_->rootObjects().isEmpty());
        window_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
        window_->show();
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        view_ = window_->findChild<CanvasView*>();
        QVERIFY(view_);
    }

    Box* boxOf(const char* cls, int nth = 0) const {
        for (const auto& b : doc_->rootCanvas().boxes())
            if (b->className().str() == cls && nth-- == 0)
                return b.get();
        return nullptr;
    }

    QPoint at(QPointF canvasPos) const {
        return view_->mapToScene((canvasPos - view_->origin()) * view_->zoom()).toPoint();
    }

    double arg(const Box& b, int index) const {
        return b.text[static_cast<std::size_t>(index) + 1].asFloat();
    }

    void drag(QPoint from, QPoint to) {
        QTest::mousePress(window_, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(window_, from + (to - from) * i / 8);
        QTest::mouseRelease(window_, Qt::LeftButton, {}, to);
    }

private slots:
    void initTestCase() {
        // Examples are copied to the data directory: use a test location.
        QStandardPaths::setTestModeEnabled(true);
        session_ = std::make_unique<Session>(nullptr, Session::Audio::Silent);
        // Any QML error from a node must fail the test, not just print.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("(qrc|file):.*\\.qml")));
    }

    void cleanup() {
        engine_.reset();
        if (doc_)
            session_->closeDocument(doc_);
        doc_ = nullptr;
    }

    void cleanupTestCase() {
        session_.reset();
        // Remove the copies (this is Qt's test location, ~/.qttest/...).
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/examples"))
            .removeRecursively();
    }

    // ---------------------------------------------------------------------

    void opensCleanly_data() {
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("widgets");
        QTest::addColumn<bool>("nodePack");
        QTest::newRow("drum machine") << QStringLiteral("drum-machine/main.qd") << 5 << true;
        QTest::newRow("wavetable synth") << QStringLiteral("wavetable-synth/main.qd") << 3 << true;
        QTest::newRow("euclidean ambient") << QStringLiteral("euclidean-ambient/main.qd") << 4 << true;
        QTest::newRow("audio visuals") << QStringLiteral("audio-visuals/main.qd") << 3 << false;
        QTest::newRow("vj mixer") << QStringLiteral("vj-mixer/main.qd") << 6 << false;
    }

    void opensCleanly() {
        QFETCH(QString, path);
        QFETCH(int, widgets);
        QFETCH(bool, nodePack);
        session_->console()->clear();
        open(path);
        // The project's nodes came from its own folder, and every box made it.
        std::function<void(const Canvas&)> check = [&](const Canvas& c) {
            for (const auto& b : c.boxes()) {
                QVERIFY2(!b->broken(), b->displayText().c_str());
                if (b->child)
                    check(*b->child);
            }
        };
        check(doc_->rootCanvas());
        QTRY_VERIFY(view_->liveWidgets() >= widgets);
        const QString log = session_->console()->allText();
        QVERIFY2(!log.contains(QStringLiteral("front end")), qPrintable(log));
        if (nodePack)
            QVERIFY2(log.contains(QStringLiteral("loaded")), qPrintable(log));
        QVERIFY2(!log.contains(QStringLiteral("shader")), qPrintable(log)); // no effect failed to compile
        // The copy is a real project on disk.
        QVERIFY(QFileInfo(doc_->fileUrl().toLocalFile()).isFile());
    }

    void stepGridClickChangesThePattern() {
        open(QStringLiteral("drum-machine/main.qd"));
        QTRY_VERIFY(view_->liveWidgets() > 0);
        Box* grid = boxOf("stepgrid");
        QVERIFY(grid);
        QCOMPARE(arg(*grid, 3), 32896.0); // clap row
        // Row 3, step 0.
        QTest::mouseClick(window_, Qt::LeftButton, {}, at(QPointF(grid->x + grid->w / 32, grid->y + grid->h * 7 / 8)));
        QCOMPARE(arg(*grid, 3), 32897.0);
        QVERIFY(doc_->modified());
    }

    void waveEditorWritesTheArray() {
        open(QStringLiteral("wavetable-synth/main.qd"));
        QTRY_VERIFY(view_->liveWidgets() > 0);
        const QList<float> before = session_->readArray(QStringLiteral("wavetable"));
        QCOMPARE(before.size(), 35);
        Box* draw = boxOf("wavedraw");
        QVERIFY(draw);
        // Paint a flat line near the top across the whole cycle.
        drag(at(QPointF(draw->x + 2, draw->y + 6)), at(QPointF(draw->x + draw->w - 2, draw->y + 6)));
        // The engine republishes the array at most every 30 ms, so wait for
        // the whole stroke rather than the first point.
        auto allHigh = [&] {
            const auto a = session_->readArray(QStringLiteral("wavetable"));
            return a.size() == 35 && std::all_of(a.begin(), a.end(), [](float v) { return v > 0.8f; });
        };
        QTRY_VERIFY_WITH_TIMEOUT(allHigh(), 3000);
    }

    void adsrHandleDragChangesAttack() {
        open(QStringLiteral("wavetable-synth/main.qd"));
        QTRY_VERIFY(view_->liveWidgets() > 0);
        Box* adsr = boxOf("adsr");
        QVERIFY(adsr);
        const double attack = arg(*adsr, 0);
        // The attack handle sits at x = (w/4) * sqrt(attack / 3000), top of the curve.
        const double hx = adsr->x + adsr->w / 4 * std::sqrt(attack / 3000.0);
        const QPoint handle = at(QPointF(hx, adsr->y + 14));
        drag(handle, handle + QPoint(30, 0));
        QVERIFY(arg(*adsr, 0) > attack + 50);
    }

    void euclidRingDragAddsHits() {
        open(QStringLiteral("euclidean-ambient/main.qd"));
        QTRY_VERIFY(view_->liveWidgets() > 0);
        Box* ring = boxOf("euclid");
        QVERIFY(ring);
        QCOMPARE(arg(*ring, 1), 5.0);
        const QPoint c = at(QPointF(ring->x + ring->w / 2, ring->y + ring->h / 2));
        drag(c, c - QPoint(0, 24)); // 8 px per hit
        QCOMPARE(arg(*ring, 1), 8.0);
    }
};

QTEST_MAIN(ExamplesTest)
#include "test_examples.moc"
