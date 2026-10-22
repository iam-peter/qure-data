#pragma once

#include "Patch.hpp"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

class QQmlEngine;
class QQuickWindow;

namespace qure::ui {

class NodeHandle;
class PatchDocument;
class Session;

/// Compiles an *effect* – a GLSL function `vec4 effect(vec2 uv)` – into a
/// shader for a QML ShaderEffect, at runtime and for every graphics API
/// Qt can render with. The effect sees:
///
///   uniform float time;            // seconds
///   uniform vec2  resolution;      // pixels
///   uniform float p0 ... p7;       // parameters from the patch
///   uniform sampler2D source0, source1;  // the texture inlets
///
/// Local files are watched and recompiled when they change, so a shader
/// can be edited while the patch runs.
class EffectCompiler : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QUrl shader READ shader NOTIFY shaderChanged)
    Q_PROPERTY(QString error READ error NOTIFY shaderChanged)

public:
    explicit EffectCompiler(QObject* parent = nullptr);

    [[nodiscard]] QUrl source() const { return source_; }
    void setSource(const QUrl& url);
    [[nodiscard]] QUrl shader() const { return shader_; }
    [[nodiscard]] QString error() const { return error_; }

    /// Wraps and bakes effect code; returns the .qsb file (cached by
    /// content) or an empty URL and the compiler's message.
    static QUrl compile(const QByteArray& effect, QString* error);

signals:
    void sourceChanged();
    void shaderChanged();

private:
    void rebuild();

    QUrl source_, shader_;
    QString error_;
    QFileSystemWatcher watcher_;
};

/// Draws a list of samples as a texture one pixel high: red is the
/// waveform (0.5 = silence), green the level. Behind [gfx.audio~].
class SampleTexture : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QList<float> samples READ samples WRITE setSamples NOTIFY samplesChanged)
    Q_PROPERTY(qreal level READ level NOTIFY samplesChanged)

public:
    explicit SampleTexture(QQuickItem* parent = nullptr);
    [[nodiscard]] QList<float> samples() const { return samples_; }
    void setSamples(const QList<float>& samples);
    [[nodiscard]] qreal level() const { return level_; }

signals:
    void samplesChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    QList<float> samples_;
    qreal level_ = 0;
};

/// Turns the gfx boxes of all open patches into running visuals: for each
/// [gfx.window] a window, holding one QML item per box upstream of it,
/// joined as their texture cords say. Rebuilt when the patches change.
///
/// It uses its own QQmlEngine, so its windows live exactly as long as it
/// does, independent of the editor's engine.
class VisualEngine : public QObject {
    Q_OBJECT

public:
    explicit VisualEngine(Session& session);
    ~VisualEngine() override;

    /// Coalesces into one rebuild on the next event loop pass.
    void scheduleRebuild();
    /// Closes a document's windows now, before the document is deleted.
    void release(PatchDocument* doc);
    /// The open output windows (tests).
    [[nodiscard]] QList<QQuickWindow*> windows() const;

private:
    struct Output {
        PatchDocument* doc = nullptr;
        Box* box = nullptr;
        QString signature;
        QPointer<QQuickWindow> window;
        std::vector<QPointer<QQuickItem>> items;
        std::vector<QPointer<NodeHandle>> audio; ///< handles to poll every frame
        bool closedByUser = false;
    };

    void rebuild();
    void build(Output& out);
    QQuickItem* buildNode(Output& out, Box& box, QQuickItem* stage, QHash<const Box*, QQuickItem*>& made,
                          QSet<const Box*>& visiting);
    QString signatureOf(const Box& window) const;
    void pollAudio();
    QQmlEngine& engine();

    Session& session_;
    std::unique_ptr<QQmlEngine> engine_;
    QHash<BoxId, Output> outputs_;
    bool scheduled_ = false;
};

/// The box feeding texture inlet `inlet` of `box`, if any.
Box* textureSource(const Box& box, int inlet);

} // namespace qure::ui
