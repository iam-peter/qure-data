#include "Visuals.hpp"

#include "NodeHandle.hpp"
#include "PatchDocument.hpp"
#include "Session.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <rhi/qshaderbaker.h>

#include <cmath>
#include <functional>

namespace qure::ui {

// ===========================================================================
// EffectCompiler

namespace {

// The uniform block and samplers every effect gets. The layout follows
// Qt's ShaderEffect conventions (block at binding 0, samplers from 1);
// ShaderEffect fills the uniforms from QML properties of the same names.
constexpr const char* kPrologue = R"(#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float p0;
    float p1;
    float p2;
    float p3;
    float p4;
    float p5;
    float p6;
    float p7;
    vec2 resolution;
};
layout(binding = 1) uniform sampler2D source0;
layout(binding = 2) uniform sampler2D source1;
#line 1
)";

constexpr const char* kEpilogue = R"(
void main() { fragColor = effect(qt_TexCoord0) * qt_Opacity; }
)";

QString readSource(const QUrl& url, QString* error) {
    const QString path = url.scheme() == QLatin1String("qrc") ? QLatin1Char(':') + url.path() : url.toLocalFile();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("cannot read %1").arg(path);
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

} // namespace

EffectCompiler::EffectCompiler(QObject* parent) : QObject(parent) {
    // Editors often replace the file instead of writing it: watch again.
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (!watcher_.files().contains(path))
            watcher_.addPath(path);
        rebuild();
    });
}

void EffectCompiler::setSource(const QUrl& url) {
    if (url == source_)
        return;
    if (!watcher_.files().isEmpty())
        watcher_.removePaths(watcher_.files());
    source_ = url;
    if (url.isLocalFile())
        watcher_.addPath(url.toLocalFile());
    emit sourceChanged();
    rebuild();
}

void EffectCompiler::rebuild() {
    QString error;
    QUrl shader;
    if (!source_.isEmpty()) {
        const QString code = readSource(source_, &error);
        if (error.isEmpty())
            shader = compile(code.toUtf8(), &error);
    }
    if (!error.isEmpty() && Session::instance())
        Session::instance()->postError(tr("shader %1: %2").arg(source_.fileName(), error.trimmed()));
    // Keep the last working shader while the file has an error; with none
    // yet, draw nothing (rather than ShaderEffect's default shader).
    if (!shader.isEmpty())
        shader_ = shader;
    else if (shader_.isEmpty())
        shader_ = compile("vec4 effect(vec2 uv) { return vec4(0.0); }\n", nullptr);
    error_ = error;
    emit shaderChanged();
}

QUrl EffectCompiler::compile(const QByteArray& effect, QString* error) {
    const QByteArray source = QByteArray(kPrologue) + effect + kEpilogue;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/shaders");
    const QString file = dir + QLatin1Char('/') +
                         QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha1).toHex()) +
                         QStringLiteral(".qsb");
    if (QFile::exists(file))
        return QUrl::fromLocalFile(file);

    QShaderBaker baker;
    baker.setGeneratedShaderVariants({QShader::StandardShader});
    baker.setGeneratedShaders({
        {QShader::SpirvShader, QShaderVersion(100)},
        {QShader::GlslShader, QShaderVersion(100, QShaderVersion::GlslEs)},
        {QShader::GlslShader, QShaderVersion(120)},
        {QShader::GlslShader, QShaderVersion(150)},
        {QShader::HlslShader, QShaderVersion(50)},
        {QShader::MslShader, QShaderVersion(12)},
    });
    baker.setSourceString(source, QShader::FragmentStage);
    const QShader shader = baker.bake();
    if (!shader.isValid()) {
        if (error)
            *error = baker.errorMessage();
        return {};
    }
    QDir().mkpath(dir);
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("cannot write %1").arg(file);
        return {};
    }
    out.write(shader.serialized());
    out.commit();
    return QUrl::fromLocalFile(file);
}

// ===========================================================================
// SampleTexture

SampleTexture::SampleTexture(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

void SampleTexture::setSamples(const QList<float>& samples) {
    samples_ = samples;
    double sum = 0;
    for (float s : samples_)
        sum += double(s) * s;
    level_ = samples_.isEmpty() ? 0.0 : std::sqrt(sum / samples_.size());
    emit samplesChanged();
    update();
}

QSGNode* SampleTexture::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    const int n = std::max<int>(1, static_cast<int>(samples_.size()));
    QImage img(n, 1, QImage::Format_RGBA8888);
    const int level = std::clamp(static_cast<int>(level_ * 2 * 255), 0, 255);
    for (int i = 0; i < n; ++i) {
        const float v = samples_.isEmpty() ? 0.0f : std::clamp(samples_[i], -1.0f, 1.0f);
        img.setPixelColor(i, 0, QColor(static_cast<int>((v * 0.5f + 0.5f) * 255), level, 0, 255));
    }
    node->setTexture(window()->createTextureFromImage(img));
    node->setRect(boundingRect());
    return node;
}

// ===========================================================================
// VisualEngine

namespace {

/// QML component per gfx class (qml/visuals/*.qml).
QString componentFor(std::string_view cls) {
    if (cls == "gfx.qml")
        return QStringLiteral("QmlScene");
    if (cls == "gfx.shader")
        return QStringLiteral("Shader");
    if (cls == "gfx.mix")
        return QStringLiteral("Mix");
    if (cls == "gfx.feedback")
        return QStringLiteral("Feedback");
    if (cls == "gfx.audio~")
        return QStringLiteral("AudioTexture");
    return {};
}

bool isGfx(const Box& b) { return b.object && !b.broken() && b.className().str().starts_with("gfx."); }

} // namespace

Box* textureSource(const Box& box, int inlet) {
    for (const auto& c : box.canvas->connections())
        if (c.to == &box && c.inlet == inlet && c.isTexture())
            return c.from;
    return nullptr;
}

VisualEngine::VisualEngine(Session& session) : QObject(&session), session_(session) {
    connect(&session_, &Session::frame, this, &VisualEngine::pollAudio);
}

VisualEngine::~VisualEngine() {
    // Windows and their items before the engine that created them.
    for (auto& out : outputs_) {
        for (auto& item : out.items)
            delete item.data();
        delete out.window.data();
    }
    outputs_.clear();
}

QQmlEngine& VisualEngine::engine() {
    if (!engine_)
        engine_ = std::make_unique<QQmlEngine>();
    return *engine_;
}

void VisualEngine::scheduleRebuild() {
    if (scheduled_)
        return;
    scheduled_ = true;
    QTimer::singleShot(0, this, [this] {
        scheduled_ = false;
        rebuild();
    });
}

void VisualEngine::release(PatchDocument* doc) {
    for (auto it = outputs_.begin(); it != outputs_.end();) {
        if (it->doc == doc) {
            for (auto& item : it->items)
                delete item.data();
            delete it->window.data();
            it = outputs_.erase(it);
        } else {
            ++it;
        }
    }
}

QList<QQuickWindow*> VisualEngine::windows() const {
    QList<QQuickWindow*> out;
    for (const auto& o : outputs_)
        if (o.window)
            out << o.window.data();
    return out;
}

QString VisualEngine::signatureOf(const Box& window) const {
    // Everything that shapes the window's graph: the boxes upstream, their
    // text and which inlet each feeds. Parameter messages are not part of
    // it – they reach the items without a rebuild.
    QString sig;
    QSet<const Box*> seen;
    std::function<void(const Box&)> walk = [&](const Box& b) {
        // The address too: undo recreates boxes with the same id and text.
        sig += QStringLiteral("%1@%2:").arg(b.id).arg(reinterpret_cast<quintptr>(&b)) +
               QString::fromStdString(b.displayText()) + QLatin1Char('|');
        if (seen.contains(&b))
            return;
        seen.insert(&b);
        for (int i = 0; i < b.numInlets(); ++i)
            if (b.inlets[static_cast<std::size_t>(i)] == IoletKind::Texture)
                if (const Box* src = textureSource(b, i)) {
                    sig += QStringLiteral("<%1:").arg(i);
                    walk(*src);
                    sig += QLatin1Char('>');
                }
    };
    walk(window);
    return sig;
}

void VisualEngine::rebuild() {
    QSet<BoxId> live;
    for (QObject* o : session_.documents()) {
        auto* doc = qobject_cast<PatchDocument*>(o);
        std::function<void(Canvas&)> scan = [&](Canvas& c) {
            for (const auto& b : c.boxes()) {
                if (isGfx(*b) && b->className().str() == "gfx.window") {
                    live.insert(b->id);
                    Output& out = outputs_[b->id];
                    const QString sig = signatureOf(*b);
                    if (out.box != b.get() || out.signature != sig || (!out.window && !out.closedByUser)) {
                        out.doc = doc;
                        out.box = b.get();
                        out.signature = sig;
                        out.closedByUser = false;
                        build(out);
                    }
                }
                if (b->child)
                    scan(*b->child);
            }
        };
        scan(doc->rootCanvas());
    }
    for (auto it = outputs_.begin(); it != outputs_.end();) {
        if (!live.contains(it.key())) {
            for (auto& item : it->items)
                delete item.data();
            delete it->window.data();
            it = outputs_.erase(it);
        } else {
            ++it;
        }
    }
}

void VisualEngine::build(Output& out) {
    for (auto& item : out.items)
        delete item.data();
    out.items.clear();
    out.audio.clear();

    QQmlEngine& qml = engine();
    NodeHandle* handle = out.doc->handleFor(*out.box);
    if (out.window)
        out.window->setProperty("node", QVariant::fromValue(handle)); // the box may have been recreated
    if (!out.window) {
        QQmlComponent comp(&qml, QUrl(QStringLiteral("qrc:/qt/qml/Qure/visuals/OutputWindow.qml")));
        QObject* obj = comp.createWithInitialProperties({{QStringLiteral("node"), QVariant::fromValue(handle)}});
        out.window = qobject_cast<QQuickWindow*>(obj);
        if (!out.window) {
            session_.postError(tr("visuals: %1").arg(comp.errorString()));
            delete obj;
            return;
        }
        const BoxId id = out.box->id;
        // Closing the window keeps it closed until the graph changes.
        connect(out.window, &QQuickWindow::visibleChanged, this, [this, id] {
            auto it = outputs_.find(id);
            if (it != outputs_.end() && it->window && !it->window->isVisible())
                it->closedByUser = true;
        });
    }
    auto* stage = out.window->property("stage").value<QQuickItem*>();
    QHash<const Box*, QQuickItem*> made;
    QSet<const Box*> visiting;
    QQuickItem* content = nullptr;
    if (Box* src = textureSource(*out.box, 0))
        content = buildNode(out, *src, stage, made, visiting);
    out.window->setProperty("content", QVariant::fromValue(content));
    out.window->show();
}

QQuickItem* VisualEngine::buildNode(Output& out, Box& box, QQuickItem* stage, QHash<const Box*, QQuickItem*>& made,
                                    QSet<const Box*>& visiting) {
    if (auto it = made.find(&box); it != made.end())
        return *it;
    if (!isGfx(box) || visiting.contains(&box)) // not a visual box, or a cycle
        return nullptr;
    const QString name = componentFor(box.className().str());
    if (name.isEmpty())
        return nullptr;
    visiting.insert(&box);

    QVariantMap props{{QStringLiteral("node"), QVariant::fromValue(out.doc->handleFor(box))}};
    for (int i = 0; i < std::min(2, box.numInlets()); ++i)
        if (box.inlets[static_cast<std::size_t>(i)] == IoletKind::Texture)
            if (Box* src = textureSource(box, i))
                if (QQuickItem* in = buildNode(out, *src, stage, made, visiting))
                    props.insert(QStringLiteral("input%1").arg(i), QVariant::fromValue(in));
    visiting.remove(&box);

    QQmlComponent comp(&engine(), QUrl(QStringLiteral("qrc:/qt/qml/Qure/visuals/%1.qml").arg(name)));
    QObject* obj = comp.createWithInitialProperties(props);
    auto* item = qobject_cast<QQuickItem*>(obj);
    if (!item) {
        session_.postError(tr("visuals: %1").arg(comp.errorString()));
        delete obj;
        return nullptr;
    }
    item->setParentItem(stage);
    out.items.emplace_back(item);
    if (box.className().str() == "gfx.audio~")
        out.audio.emplace_back(out.doc->handleFor(box));
    made.insert(&box, item);
    return item;
}

void VisualEngine::pollAudio() {
    for (auto& out : outputs_)
        if (out.window && out.window->isVisible())
            for (auto& h : out.audio)
                if (h)
                    h->pollSamples();
}

} // namespace qure::ui
