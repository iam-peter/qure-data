#pragma once

#include "Patch.hpp"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QUndoStack>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace qure::ui {

class NodeHandle;
class PatchDocument;
class Session;

/// Locates a canvas inside a document by the ids of the boxes that own it,
/// so undo commands survive the canvas being deleted and restored.
using CanvasPath = std::vector<BoxId>;

/// A QML-visible reference to one canvas (the root or a subpatch). Patch
/// windows hold one; it becomes invalid when the subpatch is deleted.
class CanvasRef : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("obtained from a PatchDocument")
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY validChanged)
    Q_PROPERTY(bool isRoot READ isRoot CONSTANT)
    Q_PROPERTY(PatchDocument* document READ document CONSTANT)

public:
    CanvasRef(PatchDocument& doc, Canvas* canvas);

    [[nodiscard]] Canvas* canvas() const { return canvas_; }
    [[nodiscard]] PatchDocument* document() const { return doc_; }
    [[nodiscard]] QString title() const;
    [[nodiscard]] bool valid() const { return canvas_ != nullptr; }
    [[nodiscard]] bool isRoot() const;
    void invalidate();
    void retitle() { emit titleChanged(); }

signals:
    void titleChanged();
    void validChanged();
    /// Structure, geometry or text changed; views repaint.
    void changed();

private:
    PatchDocument* doc_;
    Canvas* canvas_;
};

/// A patch file open in the editor: the undo stack, file I/O, and the
/// editing operations views use. Every edit goes through here so that it
/// is undoable and recompiled exactly once.
class PatchDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("created by Session")
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY titleChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged)
    Q_PROPERTY(CanvasRef* root READ root CONSTANT)

public:
    PatchDocument(Session& session, Canvas& root);
    ~PatchDocument() override;

    [[nodiscard]] Session& session() const { return session_; }
    [[nodiscard]] Canvas& rootCanvas() const { return root_; }
    [[nodiscard]] CanvasRef* root() { return refFor(&root_); }
    [[nodiscard]] CanvasRef* refFor(Canvas* canvas);

    [[nodiscard]] QString title() const;
    [[nodiscard]] QUrl fileUrl() const;
    [[nodiscard]] bool modified() const { return !undo_.isClean() || dirty_; }
    [[nodiscard]] bool canUndo() const { return undo_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return undo_.canRedo(); }
    [[nodiscard]] QString undoText() const { return undo_.undoText(); }
    [[nodiscard]] QString redoText() const { return undo_.redoText(); }

    Q_INVOKABLE void undo() { undo_.undo(); }
    Q_INVOKABLE void redo() { undo_.redo(); }
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);

    // ---- editing (undoable) ----
    Box* createBox(Canvas& canvas, BoxKind kind, QPointF pos, const QString& text);
    void setBoxText(Canvas& canvas, BoxId id, const QString& text);
    void removeBoxes(Canvas& canvas, const QList<BoxId>& ids);
    void moveBoxes(Canvas& canvas, const QList<BoxId>& ids, QPointF delta);
    bool connectBoxes(Canvas& canvas, BoxId from, int outlet, BoxId to, int inlet);
    void disconnect(Canvas& canvas, const Connection& c);
    QList<BoxId> paste(Canvas& canvas, const QString& text, QPointF offset);
    [[nodiscard]] QString copy(Canvas& canvas, const QList<BoxId>& ids) const;

    /// Moves without creating an undo step (live dragging); finish with
    /// moveBoxes(..., totalDelta) which records it.
    void moveBoxesLive(Canvas& canvas, const QList<BoxId>& ids, QPointF delta);

    // ---- used by commands and the session ----
    [[nodiscard]] Canvas* resolve(const CanvasPath& path) const;
    [[nodiscard]] CanvasPath pathOf(const Canvas& canvas) const;
    /// Recompiles (unless inside a batch) and notifies views of `canvas`.
    void structureChanged(Canvas& canvas);
    void geometryChanged(Canvas& canvas);
    void beginBatch() { ++batch_; }
    void endBatch();
    void markModified();

    /// Box for an engine object id (after the last commit).
    [[nodiscard]] Box* boxForObject(ObjectId id) const;
    /// Node handle for boxes with a QML front end (created on demand).
    NodeHandle* handleFor(Box& box);
    void relayout(Box& box);
    void relayoutAll(Canvas& canvas);
    /// A box's text changed from the engine side (message box "set").
    void textUpdatedByEngine(Box& box, AtomVector text);

signals:
    void titleChanged();
    void modifiedChanged();
    void undoStateChanged();
    void requestOpenCanvas(CanvasRef* canvas);

private:
    void rebuildIndex();

    Session& session_;
    Canvas& root_;
    QUndoStack undo_;
    QHash<Canvas*, CanvasRef*> refs_;
    QHash<BoxId, NodeHandle*> handles_;
    QHash<ObjectId, Box*> byObject_;
    int batch_ = 0;
    bool pendingCommit_ = false;
    bool dirty_ = false;
};

} // namespace qure::ui
