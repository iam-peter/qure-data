// The main window and the patch windows together, loaded like main() does:
// closing the main window quits (asking about unsaved patches first), and
// Window → Main Window brings it back.
//
// Silent audio and a private display, like test_canvas.cpp.

#include "PatchDocument.hpp"
#include "Session.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QtTest>

Q_IMPORT_QML_PLUGIN(QurePlugin)

using namespace qure::ui;

class WindowsTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<Session> session_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* main_ = nullptr;

    /// Loads Main.qml; it opens a window for every document already open.
    void loadMain() {
        engine_ = std::make_unique<QQmlApplicationEngine>();
        engine_->loadFromModule("Qure", "Main");
        QVERIFY(!engine_->rootObjects().isEmpty());
        main_ = qobject_cast<QQuickWindow*>(engine_->rootObjects().first());
        QVERIFY(main_);
        QVERIFY(QTest::qWaitForWindowExposed(main_));
    }

    [[nodiscard]] QList<QQuickWindow*> patchWindows() const {
        QList<QQuickWindow*> out;
        for (auto* w : QGuiApplication::topLevelWindows())
            if (auto* q = qobject_cast<QQuickWindow*>(w); q && q != main_ && q->isVisible() && q->inherits("QQuickApplicationWindow"))
                out << q;
        return out;
    }

private slots:
    void initTestCase() { session_ = std::make_unique<Session>(nullptr, Session::Audio::Silent); }

    void cleanup() {
        engine_.reset();
        main_ = nullptr;
        for (auto* d : session_->documents())
            session_->closeDocument(qobject_cast<PatchDocument*>(d));
    }

    void cleanupTestCase() { session_.reset(); }

    // ---------------------------------------------------------------------

    void closingTheMainWindowQuits() {
        session_->newPatch();
        loadMain();
        QTRY_COMPARE(patchWindows().size(), 1);
        QSignalSpy quit(engine_.get(), &QQmlEngine::quit);
        main_->close();
        QCOMPARE(quit.size(), 1);
        QTRY_COMPARE(patchWindows().size(), 0);
    }

    void unsavedPatchesStopTheQuit() {
        session_->newPatch()->markModified();
        loadMain();
        QSignalSpy quit(engine_.get(), &QQmlEngine::quit);
        main_->close();
        QCOMPARE(quit.size(), 0);
        QVERIFY(main_->isVisible());
        QCOMPARE(patchWindows().size(), 1);
        auto* question = main_->findChild<QObject*>(QStringLiteral("quitQuestion"));
        QVERIFY(question);
        QTRY_VERIFY(question->property("visible").toBool());
        QMetaObject::invokeMethod(question, "close");
    }

    void ctrlRBringsTheMainWindowBack() {
        session_->newPatch();
        loadMain();
        QTRY_COMPARE(patchWindows().size(), 1);
        QQuickWindow* patch = patchWindows().first();
        QVERIFY(QTest::qWaitForWindowExposed(patch));
        main_->hide();
        QVERIFY(!main_->isVisible());
        patch->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(patch));
        QTest::keyClick(patch, Qt::Key_R, Qt::ControlModifier);
        QTRY_VERIFY(main_->isVisible());
    }
};

QTEST_MAIN(WindowsTest)
#include "test_windows.moc"
