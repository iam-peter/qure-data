#pragma once

#include "ConsoleModel.hpp"
#include "FrontendRegistry.hpp"
#include "Theme.hpp"

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace qure {
class AudioDriver;
class Canvas;
class Runtime;
} // namespace qure

namespace qure::ui {

class CanvasRef;
class PatchDocument;
class VisualEngine;

/// Application-wide state, exposed to QML as the `Qure` singleton: open
/// documents, the console, DSP and audio, and the engine event pump.
class Session : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Qure)
    QML_SINGLETON

    Q_PROPERTY(bool dsp READ dsp WRITE setDsp NOTIFY dspChanged)
    Q_PROPERTY(QString audioStatus READ audioStatus NOTIFY audioStatusChanged)
    Q_PROPERTY(ConsoleModel* console READ console CONSTANT)
    Q_PROPERTY(QList<QObject*> documents READ documents NOTIFY documentsChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY themeChanged)
    Q_PROPERTY(QVariantMap theme READ themeColors NOTIFY themeChanged)
    Q_PROPERTY(QStringList objectClasses READ objectClasses NOTIFY objectClassesChanged)
    Q_PROPERTY(QStringList playbackDevices READ playbackDevices NOTIFY devicesChanged)
    Q_PROPERTY(QStringList captureDevices READ captureDevices NOTIFY devicesChanged)
    Q_PROPERTY(double cpuLoad READ cpuLoad NOTIFY statsChanged)
    /// Shipped examples: [{ title, path, group }], path relative to the
    /// examples root, e.g. "drum-machine/main.qd".
    Q_PROPERTY(QVariantList examples READ examples CONSTANT)

public:
    enum class Audio {
        Device, ///< open the system's audio device (falls back to Silent)
        Silent, ///< never touch an audio device; a timer thread runs the engine
    };

    // Deliberately not default-constructible: QML must use create(), which
    // returns the instance main() made, instead of building its own.
    explicit Session(QObject* parent, Audio audio = Audio::Device);
    ~Session() override;

    static Session* instance() { return instance_; }
    static Session* create(QQmlEngine*, QJSEngine*);

    [[nodiscard]] Runtime& runtime() { return *runtime_; }
    [[nodiscard]] const FrontendRegistry& frontends() const { return frontends_; }
    [[nodiscard]] const Theme& theme() const { return theme_; }
    /// The visuals module's output windows ([gfx.window]).
    [[nodiscard]] VisualEngine& visuals() { return *visuals_; }

    [[nodiscard]] bool dsp() const { return dsp_; }
    void setDsp(bool on);
    [[nodiscard]] QString audioStatus() const { return audioStatus_; }
    [[nodiscard]] ConsoleModel* console() { return &console_; }
    [[nodiscard]] QList<QObject*> documents() const;
    [[nodiscard]] bool darkTheme() const { return theme_.dark; }
    void setDarkTheme(bool dark);
    [[nodiscard]] QVariantMap themeColors() const;
    [[nodiscard]] QStringList objectClasses() const { return classes_; }
    [[nodiscard]] QStringList playbackDevices() const { return playbackDevices_; }
    [[nodiscard]] QStringList captureDevices() const { return captureDevices_; }
    [[nodiscard]] double cpuLoad() const { return cpuLoad_; }
    [[nodiscard]] QVariantList examples() const;

    Q_INVOKABLE PatchDocument* newPatch();
    Q_INVOKABLE PatchDocument* openPatch(const QUrl& url);
    /// Copies the example's project folder to the user's data directory
    /// (refreshing it from the shipped version) and opens it from there, so
    /// its abstractions and node pack resolve like in any project.
    Q_INVOKABLE PatchDocument* openExample(const QString& path);
    /// Front ends can read and write arrays ([table], [array define],
    /// graph arrays) by name. Reads see the latest snapshot the engine
    /// published; writes go to the engine like `; name offset values...`.
    Q_INVOKABLE QList<float> readArray(const QString& name) const;
    Q_INVOKABLE void writeArray(const QString& name, int offset, const QList<float>& values);
    Q_INVOKABLE void closeDocument(PatchDocument* doc);
    /// Sends a message typed in the console, e.g. "pd dsp 1" or "foo 42".
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void restartAudio(int playbackDevice = -1, int captureDevice = -1);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE QString helpFor(const QString& className) const;
    /// Asks QML to show a window for `canvas` (subpatch double-click).
    Q_INVOKABLE void openCanvas(CanvasRef* canvas) { emit canvasOpenRequested(canvas); }

    void commit();
    void postInfo(const QString& text) { console_.append(ConsoleModel::Info, text); }
    Q_INVOKABLE void postError(const QString& text) { console_.append(ConsoleModel::Error, text); }

signals:
    void dspChanged();
    void audioStatusChanged();
    void documentsChanged();
    void themeChanged();
    void objectClassesChanged();
    void devicesChanged();
    void statsChanged();
    void canvasOpenRequested(CanvasRef* canvas);
    /// Fired once per pump tick; views use it to poll snapshots.
    void frame();

private:
    void pump();
    void startAudio(int playback, int capture);
    void startSilent();
    /// Loads `<dir>/nodes/nodes.json` (a project's own node pack), if any.
    void loadProjectNodes(const QString& dir);
    void rebuildClassList();
    PatchDocument* adopt(Canvas& canvas);

    static Session* instance_;
    std::unique_ptr<Runtime> runtime_;
    std::unique_ptr<AudioDriver> driver_;
    VisualEngine* visuals_ = nullptr;
    FrontendRegistry frontends_;
    ConsoleModel console_;
    Theme theme_ = Theme::light();
    QList<PatchDocument*> documents_;
    QTimer pump_;
    QStringList classes_, playbackDevices_, captureDevices_;
    QString audioStatus_;
    bool dsp_ = false;
    double cpuLoad_ = 0;
    Audio audioMode_;
    int untitled_ = 1;
};

} // namespace qure::ui
