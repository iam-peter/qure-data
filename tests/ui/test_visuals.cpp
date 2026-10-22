// Rendering of the visuals module, checked by pixel: patches with gfx boxes
// are opened from files (so scenes and shaders resolve next to them), and
// the output windows are grabbed.
//
// Silent audio and a private display, like the other UI tests.

#include "ConsoleModel.hpp"
#include "PatchDocument.hpp"
#include "Session.hpp"
#include "Visuals.hpp"

#include "Runtime.hpp"

#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(QurePlugin)

using namespace qure;
using namespace qure::ui;

class VisualsTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<Session> session_;
    std::unique_ptr<QTemporaryDir> dir_;
    PatchDocument* doc_ = nullptr;

    void write(const QString& name, const QByteArray& text) {
        QFile f(dir_->filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }

    /// Opens `patch` (saved as main.qd) and waits for its output window.
    QQuickWindow* open(const QByteArray& patch) {
        write(QStringLiteral("main.qd"), patch);
        doc_ = session_->openPatch(QUrl::fromLocalFile(dir_->filePath(QStringLiteral("main.qd"))));
        if (!doc_)
            return nullptr;
        if (!QTest::qWaitFor([&] { return !session_->visuals().windows().isEmpty(); }, 3000))
            return nullptr;
        QQuickWindow* w = session_->visuals().windows().first();
        if (!QTest::qWaitForWindowExposed(w))
            return nullptr;
        return w;
    }

    static QColor pixel(QQuickWindow* w, qreal fx, qreal fy) {
        const QImage img = w->grabWindow();
        return img.pixelColor(static_cast<int>(fx * (img.width() - 1)), static_cast<int>(fy * (img.height() - 1)));
    }

    static bool near(const QColor& c, int r, int g, int b, int tolerance = 40) {
        return std::abs(c.red() - r) <= tolerance && std::abs(c.green() - g) <= tolerance &&
               std::abs(c.blue() - b) <= tolerance;
    }

    Object* objectOf(int index) const { return doc_->rootCanvas().boxes()[static_cast<std::size_t>(index)]->object.get(); }

    static constexpr const char* kRed = "import QtQuick\nRectangle { property var node; color: \"#ff0000\" }\n";
    static constexpr const char* kBlue = "import QtQuick\nRectangle { property var node; color: \"#0000ff\" }\n";

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true); // shader cache in a test location
        session_ = std::make_unique<Session>(nullptr, Session::Audio::Silent);
        QTest::failOnWarning(QRegularExpression(QStringLiteral("(qrc|file):.*\\.qml")));
    }

    void init() { dir_ = std::make_unique<QTemporaryDir>(); }

    void cleanup() {
        if (doc_)
            session_->closeDocument(doc_);
        doc_ = nullptr;
        dir_.reset();
    }

    void cleanupTestCase() {
        session_.reset();
        QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).removeRecursively();
    }

    // ---------------------------------------------------------------------

    void builtInShaderRenders() {
        QQuickWindow* w = open("#N canvas 0 0 400 300 12;\n#X obj 10 10 gfx.shader plasma;\n"
                               "#X obj 10 50 gfx.window 160 90;\n#X connect 0 0 1 0;\n");
        QVERIFY(w);
        QCOMPARE(w->width(), 160);
        // A colourful field: corners differ, nothing is black.
        QTRY_VERIFY(pixel(w, 0.1, 0.1) != pixel(w, 0.9, 0.9));
        QVERIFY(pixel(w, 0.5, 0.5).value() > 40);
    }

    void qmlSceneAndShaderChain() {
        write(QStringLiteral("Red.qml"), kRed);
        QQuickWindow* w = open("#N canvas 0 0 400 300 12;\n#X obj 10 10 gfx.qml Red.qml;\n"
                               "#X obj 10 50 gfx.shader invert;\n#X obj 10 90 gfx.window 120 80;\n"
                               "#X connect 0 0 1 0;\n#X connect 1 0 2 0;\n");
        QVERIFY(w);
        QTRY_VERIFY2(near(pixel(w, 0.5, 0.5), 0, 255, 255), "red, inverted, is cyan");
    }

    void mixFollowsItsInlet() {
        write(QStringLiteral("Red.qml"), kRed);
        write(QStringLiteral("Blue.qml"), kBlue);
        QQuickWindow* w = open("#N canvas 0 0 400 300 12;\n#X obj 10 10 gfx.qml Red.qml;\n"
                               "#X obj 120 10 gfx.qml Blue.qml;\n#X obj 10 50 gfx.mix 0;\n"
                               "#X obj 10 90 gfx.window 120 80;\n#X connect 0 0 2 0;\n#X connect 1 0 2 1;\n"
                               "#X connect 2 0 3 0;\n");
        QVERIFY(w);
        QTRY_VERIFY(near(pixel(w, 0.5, 0.5), 255, 0, 0));
        const Atom one(1.0);
        session_->runtime().engine().sendToObject(objectOf(2)->id(), 2, sym::float_(), AtomSpan(&one, 1));
        QTRY_VERIFY(near(pixel(w, 0.5, 0.5), 0, 0, 255));
    }

    void userShaderReloadsWhenSaved() {
        write(QStringLiteral("mine.frag"), "vec4 effect(vec2 uv) { return vec4(0.0, 1.0, 0.0, 1.0); }\n");
        QQuickWindow* w = open("#N canvas 0 0 400 300 12;\n#X obj 10 10 gfx.shader mine.frag;\n"
                               "#X obj 10 50 gfx.window 120 80;\n#X connect 0 0 1 0;\n");
        QVERIFY(w);
        QTRY_VERIFY(near(pixel(w, 0.5, 0.5), 0, 255, 0));
        // Edit the file: the effect is recompiled and swapped in.
        write(QStringLiteral("mine.frag"), "vec4 effect(vec2 uv) { return vec4(0.0, 0.0, 1.0, 1.0) * p0; }\n");
        QTRY_VERIFY(near(pixel(w, 0.5, 0.5), 0, 0, 0)); // p0 is 0 so far
        // A parameter message.
        const Atom half(1.0);
        session_->runtime().engine().sendToObject(objectOf(0)->id(), 0, Symbol("p0"), AtomSpan(&half, 1));
        QTRY_VERIFY(near(pixel(w, 0.5, 0.5), 0, 0, 255));
    }

    void brokenShaderReportsAndKeepsRunning() {
        session_->console()->clear();
        write(QStringLiteral("bad.frag"), "vec4 effect(vec2 uv) { return oops; }\n");
        QQuickWindow* w = open("#N canvas 0 0 400 300 12;\n#X obj 10 10 gfx.shader bad.frag;\n"
                               "#X obj 10 50 gfx.window 120 80;\n#X connect 0 0 1 0;\n");
        QVERIFY(w);
        QTRY_VERIFY(session_->console()->allText().contains(QStringLiteral("bad.frag")));
    }

    void audioTextureDrivesTheScope() {
        QQuickWindow* w = open("#N canvas 0 0 400 300 12;\n#X obj 10 10 sig~ 0.8;\n#X obj 10 40 gfx.audio~ 64;\n"
                               "#X obj 10 70 gfx.shader scope;\n#X obj 10 110 gfx.window 200 200;\n"
                               "#X connect 0 0 1 0;\n#X connect 1 0 2 0;\n#X connect 2 0 3 0;\n");
        QVERIFY(w);
        session_->setDsp(true);
        // A constant 0.8 draws a line at y = 0.5 - 0.8 * 0.45 = 0.14.
        QTRY_VERIFY_WITH_TIMEOUT(pixel(w, 0.5, 0.14).value() > 150, 5000);
        QVERIFY(pixel(w, 0.5, 0.8).value() < 60);
        session_->setDsp(false);
    }

    void closingThePatchClosesItsWindow() {
        QQuickWindow* w = open("#N canvas 0 0 400 300 12;\n#X obj 10 10 gfx.shader plasma;\n"
                               "#X obj 10 50 gfx.window 120 80;\n#X connect 0 0 1 0;\n");
        QVERIFY(w);
        session_->closeDocument(doc_);
        doc_ = nullptr;
        QVERIFY(session_->visuals().windows().isEmpty());
    }

    void deletingTheWindowBoxClosesIt() {
        QVERIFY(open("#N canvas 0 0 400 300 12;\n#X obj 10 10 gfx.shader plasma;\n"
                     "#X obj 10 50 gfx.window 120 80;\n#X connect 0 0 1 0;\n"));
        doc_->removeBoxes(doc_->rootCanvas(), {doc_->rootCanvas().boxes()[1]->id});
        QTRY_VERIFY(session_->visuals().windows().isEmpty());
        doc_->undo();
        QTRY_COMPARE(session_->visuals().windows().size(), 1);
    }
};

QTEST_MAIN(VisualsTest)
#include "test_visuals.moc"
