#include "Session.hpp"

#include "NodeHandle.hpp"
#include "PatchDocument.hpp"

#include "AudioDriver.hpp"
#include "Runtime.hpp"

#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QQmlEngine>
#include <QStandardPaths>

namespace qure::ui {

Session* Session::instance_ = nullptr;

Session* Session::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance_, QJSEngine::CppOwnership);
    return instance_;
}

Session::Session(QObject* parent, Audio audio)
    : QObject(parent), runtime_(std::make_unique<Runtime>()), audioMode_(audio) {
    instance_ = this;

    QString error;
    if (!frontends_.loadManifest(QUrl(QStringLiteral("qrc:/qt/qml/Qure/nodes/nodes.json")), &error))
        postError(error);
    // User node packs and abstractions live next to each other:
    //   ~/.local/share/QureData/nodes/<pack>/nodes.json
    //   ~/.local/share/QureData/abstractions/*.pd
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    frontends_.loadDirectory(data + QStringLiteral("/nodes"));
    frontends_.registerEngineClasses(runtime_->registry());
    runtime_->searchPaths.emplace_back((data + QStringLiteral("/abstractions")).toStdString());
    rebuildClassList();

    connect(&pump_, &QTimer::timeout, this, &Session::pump);
    pump_.setTimerType(Qt::PreciseTimer);
    pump_.start(10);

    if (audioMode_ == Audio::Device) {
        refreshDevices();
        startAudio(-1, -1);
    } else {
        startSilent();
    }
    postInfo(tr("Qure Data ready – %1 object classes").arg(classes_.size()));
}

Session::~Session() {
    pump_.stop();
    if (driver_)
        driver_->stop();
    driver_.reset();
    for (auto* d : std::as_const(documents_))
        delete d;
    documents_.clear();
    runtime_.reset();
    instance_ = nullptr;
}

QList<QObject*> Session::documents() const {
    QList<QObject*> out;
    for (auto* d : documents_)
        out << d;
    return out;
}

void Session::setDsp(bool on) { runtime_->engine().setDsp(on); }

void Session::setDarkTheme(bool dark) {
    if (dark == theme_.dark)
        return;
    theme_ = dark ? Theme::darkTheme() : Theme::light();
    emit themeChanged();
}

QVariantMap Session::themeColors() const {
    return {
        {QStringLiteral("background"), theme_.background},
        {QStringLiteral("grid"), theme_.grid},
        {QStringLiteral("boxFill"), theme_.boxFill},
        {QStringLiteral("boxBorder"), theme_.boxBorder},
        {QStringLiteral("atomFill"), theme_.atomFill},
        {QStringLiteral("text"), theme_.text},
        {QStringLiteral("commentText"), theme_.commentText},
        {QStringLiteral("selection"), theme_.selection},
        {QStringLiteral("flash"), theme_.flash},
        {QStringLiteral("signal"), theme_.signalCord},
        {QStringLiteral("dark"), theme_.dark},
    };
}

void Session::rebuildClassList() {
    classes_.clear();
    for (const auto& n : runtime_->registry().classNames())
        if (!n.starts_with('@'))
            classes_ << QString::fromStdString(n);
    classes_ << QStringLiteral("pd");
    classes_.sort();
    emit objectClassesChanged();
}

QString Session::helpFor(const QString& className) const {
    if (const auto* info = runtime_->registry().find(Symbol(className.toStdString())))
        return QString::fromStdString(info->help);
    if (const auto* fe = frontends_.find(className))
        return fe->help;
    if (className == QLatin1String("pd"))
        return tr("subpatch");
    return {};
}

// ---------------------------------------------------------------------------

PatchDocument* Session::adopt(Canvas& canvas) {
    auto* doc = new PatchDocument(*this, canvas);
    documents_ << doc;
    emit documentsChanged();
    return doc;
}

PatchDocument* Session::newPatch() {
    auto& canvas = runtime_->newPatch(QStringLiteral("Untitled-%1").arg(untitled_++).toStdString());
    commit();
    return adopt(canvas);
}

PatchDocument* Session::openPatch(const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    for (auto* d : std::as_const(documents_))
        if (d->fileUrl() == QUrl::fromLocalFile(path))
            return d;
    auto r = runtime_->openPatch(std::filesystem::path(path.toStdString()));
    if (!r) {
        postError(QString::fromStdString(r.error()));
        return nullptr;
    }
    for (const auto& w : runtime_->compileWarnings())
        console_.append(ConsoleModel::Warning, QString::fromStdString(w));
    postInfo(tr("opened %1").arg(path));
    return adopt(**r);
}

static const QString kExampleDir = QStringLiteral(":/qt/qml/Qure/examples");

QStringList Session::examples() const {
    return QDir(kExampleDir).entryList({QStringLiteral("*.pd")}, QDir::Files, QDir::Name);
}

PatchDocument* Session::openExample(const QString& name) {
    QFile f(kExampleDir + QLatin1Char('/') + name);
    if (!f.open(QIODevice::ReadOnly)) {
        postError(tr("no example named %1").arg(name));
        return nullptr;
    }
    // Examples open as untitled copies; "Save As" puts them somewhere real.
    auto r = runtime_->openPatchText(f.readAll().toStdString(), name.toStdString());
    if (!r) {
        postError(QString::fromStdString(r.error()));
        return nullptr;
    }
    return adopt(**r);
}

void Session::closeDocument(PatchDocument* doc) {
    if (!doc || !documents_.removeOne(doc))
        return;
    emit documentsChanged();
    Canvas& canvas = doc->rootCanvas();
    // Invalidate views first, then drop the patch from the engine.
    delete doc;
    runtime_->closePatch(canvas);
}

void Session::sendMessage(const QString& text) {
    auto atoms = parseAtoms(text.toStdString());
    if (atoms.empty() || !atoms[0].isSymbol())
        return;
    const Symbol target = atoms[0].asSymbol();
    if (atoms.size() == 1) {
        runtime_->engine().sendToName(target, sym::bang(), {});
        return;
    }
    const AtomSpan rest = AtomSpan(atoms).subspan(1);
    if (rest[0].isSymbol())
        runtime_->engine().sendToName(target, rest[0].asSymbol(), rest.subspan(1));
    else
        runtime_->engine().sendToName(target, rest.size() == 1 ? sym::float_() : sym::list(), rest);
}

void Session::commit() {
    runtime_->commit();
    for (const auto& w : runtime_->compileWarnings())
        console_.append(ConsoleModel::Warning, QString::fromStdString(w));
}

// ---------------------------------------------------------------------------
// Audio

void Session::refreshDevices() {
    playbackDevices_.clear();
    captureDevices_.clear();
    for (const auto& d : listPlaybackDevices())
        playbackDevices_ << QString::fromStdString(d.name);
    for (const auto& d : listCaptureDevices())
        captureDevices_ << QString::fromStdString(d.name);
    emit devicesChanged();
}

void Session::startSilent() {
    if (driver_)
        driver_->stop();
    driver_ = makeTimerDriver(runtime_->engine());
    driver_->start();
    audioStatus_ = QString::fromStdString(driver_->description());
    emit audioStatusChanged();
}

void Session::startAudio(int playback, int capture) {
    if (audioMode_ == Audio::Silent)
        return startSilent();
    if (driver_)
        driver_->stop();
    AudioSettings settings;
    settings.playbackDevice = playback;
    settings.captureDevice = capture;
    driver_ = makeDeviceDriver(runtime_->engine(), settings);
    if (!driver_->start()) {
        postError(tr("audio: %1 – running without audio device").arg(QString::fromStdString(driver_->lastError())));
        driver_ = makeTimerDriver(runtime_->engine());
        driver_->start();
    }
    audioStatus_ = QString::fromStdString(driver_->description());
    postInfo(tr("audio: %1").arg(audioStatus_));
    emit audioStatusChanged();
}

void Session::restartAudio(int playbackDevice, int captureDevice) { startAudio(playbackDevice, captureDevice); }

// ---------------------------------------------------------------------------
// Engine → GUI

void Session::pump() {
    auto& engine = runtime_->engine();
    engine.drainEvents([&](const EngineEvent& e) {
        switch (e.kind) {
        case EngineEvent::Kind::Log: {
            const auto level = static_cast<ConsoleModel::Level>(static_cast<int>(e.level));
            console_.append(level, QString::fromUtf8(e.text.data(), static_cast<qsizetype>(e.text.size())));
            break;
        }
        case EngineEvent::Kind::DspState:
            if (dsp_ != e.dspOn) {
                dsp_ = e.dspOn;
                emit dspChanged();
            }
            break;
        case EngineEvent::Kind::Notify:
            for (auto* doc : std::as_const(documents_)) {
                Box* box = doc->boxForObject(e.object);
                if (!box)
                    continue;
                if (box->kind == BoxKind::Message && e.selector == sym::set())
                    doc->textUpdatedByEngine(*box, AtomVector(e.args.begin(), e.args.end()));
                else
                    doc->handleFor(*box)->applyNotification(e.selector, e.args);
                break;
            }
            break;
        }
    });
    runtime_->collectGarbage();

    const double load = driver_ ? driver_->load() : 0.0;
    if (std::abs(load - cpuLoad_) > 0.005) {
        cpuLoad_ = load;
        emit statsChanged();
    }
    emit frame();
}

} // namespace qure::ui
