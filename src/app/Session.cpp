#include "Session.hpp"

#include "NodeHandle.hpp"
#include "PatchDocument.hpp"
#include "Visuals.hpp"

#include "AudioDriver.hpp"
#include "Runtime.hpp"
#include "Table.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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
    //   ~/.local/share/QureData/abstractions/*.qd (or *.pd)
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    frontends_.loadDirectory(data + QStringLiteral("/nodes"));
    frontends_.registerEngineClasses(runtime_->registry());
    runtime_->searchPaths.emplace_back((data + QStringLiteral("/abstractions")).toStdString());
    rebuildClassList();

    visuals_ = new VisualEngine(*this);

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
    delete visuals_; // its windows show items bound to the documents' nodes
    visuals_ = nullptr;
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
    visuals_->scheduleRebuild();
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
    // A project folder may bring its own QML nodes; they must be known
    // before the patch's boxes are created.
    loadProjectNodes(QFileInfo(path).absolutePath());
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

void Session::loadProjectNodes(const QString& dir) {
    const QString manifest = dir + QStringLiteral("/nodes/nodes.json");
    if (!QFileInfo::exists(manifest))
        return;
    const auto before = frontends_.size();
    QString error;
    if (!frontends_.loadManifest(QUrl::fromLocalFile(manifest), &error)) {
        postError(error);
        return;
    }
    if (frontends_.size() == before)
        return;
    frontends_.registerEngineClasses(runtime_->registry(), before);
    rebuildClassList();
    postInfo(tr("loaded %n node(s) from %1", nullptr, static_cast<int>(frontends_.size() - before)).arg(manifest));
}

static const QString kExampleDir = QStringLiteral(":/qt/qml/Qure/examples");

static QString prettyName(QString name) {
    name.remove(QRegularExpression(QStringLiteral("^\\d+-")));
    name.remove(QRegularExpression(QStringLiteral("\\.(qd|pd)$")));
    name.replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    for (qsizetype i = 1; i < name.size(); ++i)
        if (name[i - 1] == QLatin1Char(' '))
            name[i] = name[i].toUpper();
    return name;
}

QVariantList Session::examples() const {
    QVariantList out;
    const QDir root(kExampleDir);
    for (const auto& dir : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir d(root.filePath(dir));
        if (d.exists(QStringLiteral("main.qd"))) {
            // A project: one entry for its main patch.
            out << QVariantMap{{QStringLiteral("title"), prettyName(dir)},
                               {QStringLiteral("path"), dir + QStringLiteral("/main.qd")},
                               {QStringLiteral("group"), QStringLiteral("projects")}};
            continue;
        }
        for (const auto& f : d.entryList({QStringLiteral("*.qd")}, QDir::Files, QDir::Name))
            out << QVariantMap{{QStringLiteral("title"), prettyName(f)},
                               {QStringLiteral("path"), dir + QLatin1Char('/') + f},
                               {QStringLiteral("group"), dir}};
    }
    return out;
}

/// Copies a qrc directory tree to `target`, overwriting.
static bool copyTree(const QString& from, const QString& target) {
    if (!QDir().mkpath(target))
        return false;
    const QDir src(from);
    for (const auto& e : src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dest = target + QLatin1Char('/') + e.fileName();
        if (e.isDir()) {
            if (!copyTree(e.filePath(), dest))
                return false;
            continue;
        }
        QFile::remove(dest);
        if (!QFile::copy(e.filePath(), dest))
            return false;
        // Resources are read-only; the copy is the user's to edit.
        QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    }
    return true;
}

PatchDocument* Session::openExample(const QString& path) {
    const QString folder = path.section(QLatin1Char('/'), 0, 0);
    const QString source = kExampleDir + QLatin1Char('/') + folder;
    if (folder.isEmpty() || !QFileInfo(source).isDir()) {
        postError(tr("no example named %1").arg(path));
        return nullptr;
    }
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/examples");
    const QString file = root + QLatin1Char('/') + path;
    for (auto* d : std::as_const(documents_))
        if (d->fileUrl() == QUrl::fromLocalFile(file))
            return d; // open already: don't overwrite it underneath
    if (!copyTree(source, root + QLatin1Char('/') + folder)) {
        postError(tr("cannot copy example to %1").arg(root));
        return nullptr;
    }
    return openPatch(QUrl::fromLocalFile(file));
}

QList<float> Session::readArray(const QString& name) const {
    const std::string wanted = name.toStdString();
    // The name is in the box text: `#X array NAME`, [table NAME], or
    // [array define (-flags) NAME].
    auto nameOf = [](const Box& b) -> std::optional<Symbol> {
        const Canvas& env = b.canvas->environment();
        auto expanded = [&](std::size_t i) -> std::optional<Symbol> {
            if (i >= b.text.size() || b.text[i].isFloat())
                return std::nullopt;
            return expandDollarSymbol(b.text[i].asSymbol(), env.dollarZero(), env.arguments());
        };
        if (b.kind == BoxKind::Array)
            return expanded(0);
        const auto cls = b.className().str();
        if (cls == "table")
            return expanded(1);
        if (cls == "array" && b.text.size() > 1 && b.text[1].asSymbol().str() == "define") {
            std::size_t i = 2;
            while (i < b.text.size() && b.text[i].isSymbol() && b.text[i].asSymbol().str().starts_with('-'))
                i += b.text[i].asSymbol().str() == "-k" ? 1 : 3;
            return expanded(i);
        }
        return std::nullopt;
    };
    std::function<const TableStorage*(const Canvas&)> find = [&](const Canvas& c) -> const TableStorage* {
        for (const auto& b : c.boxes()) {
            if (auto* t = dynamic_cast<const TableStorage*>(b->object.get()))
                if (auto n = nameOf(*b); n && n->str() == wanted)
                    return t;
            if (b->child)
                if (auto* t = find(*b->child))
                    return t;
        }
        return nullptr;
    };
    for (const auto* d : documents_)
        if (const auto* t = find(d->rootCanvas())) {
            const auto data = t->tableSnapshot();
            return QList<float>(data.begin(), data.end());
        }
    return {};
}

void Session::writeArray(const QString& name, int offset, const QList<float>& values) {
    AtomVector atoms;
    atoms.reserve(static_cast<std::size_t>(values.size()) + 1);
    atoms.emplace_back(static_cast<double>(offset));
    for (float v : values)
        atoms.emplace_back(static_cast<double>(v));
    runtime_->engine().sendToName(Symbol(name.toStdString()), sym::list(), atoms);
}

void Session::closeDocument(PatchDocument* doc) {
    if (!doc || !documents_.removeOne(doc))
        return;
    emit documentsChanged();
    Canvas& canvas = doc->rootCanvas();
    // Invalidate views first, then drop the patch from the engine.
    visuals_->release(doc);
    delete doc;
    visuals_->scheduleRebuild();
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
    if (visuals_)
        visuals_->scheduleRebuild();
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
