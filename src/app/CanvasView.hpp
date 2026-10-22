#pragma once

#include "Patch.hpp"
#include "PatchDocument.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QSet>
#include <QtQml/qqmlregistration.h>

#include <optional>

class QQmlComponent;

namespace qure::ui {

class CanvasLayer;
class NodeHandle;
struct FrontendInfo;

/// Displays and edits one canvas.
///
/// Rendering is split by cost:
///  * every box background and every cord is drawn by two batched scene
///    graph layers with custom SDF / line shaders (a few draw calls no
///    matter how many boxes), text with QSGTextNode for visible boxes only;
///  * GUI nodes (sliders, toggles, keyboards...) are real QML components,
///    instantiated only while visible and zoomed in enough to be usable.
/// Zoomed out, a patch with 100k boxes is still just a few GPU batches.
class CanvasView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(CanvasRef* canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(QPointF origin READ origin WRITE setOrigin NOTIFY viewChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(int boxCount READ boxCount NOTIFY statsChanged)
    Q_PROPERTY(int liveWidgets READ liveWidgets NOTIFY statsChanged)
    Q_PROPERTY(QString hoverText READ hoverText NOTIFY hoverTextChanged)

public:
    explicit CanvasView(QQuickItem* parent = nullptr);
    ~CanvasView() override;

    [[nodiscard]] CanvasRef* canvas() const { return ref_; }
    void setCanvas(CanvasRef* ref);
    [[nodiscard]] bool editMode() const { return editMode_; }
    void setEditMode(bool on);
    [[nodiscard]] qreal zoom() const { return zoom_; }
    void setZoom(qreal z);
    [[nodiscard]] QPointF origin() const { return origin_; }
    void setOrigin(QPointF o);
    [[nodiscard]] int selectionCount() const { return static_cast<int>(selection_.size()); }
    [[nodiscard]] int boxCount() const;
    [[nodiscard]] int liveWidgets() const { return static_cast<int>(delegates_.size()); }
    [[nodiscard]] QString hoverText() const { return hoverText_; }

    // ---- actions (menus / shortcuts) ----
    /// kind: "object" | "message" | "comment" | "floatatom" | "symbolatom" |
    /// "listbox"; with `immediate`, `text` is created right away (GUI
    /// objects), otherwise an editor opens.
    Q_INVOKABLE void placeBox(const QString& kind, const QString& text = {}, bool immediate = false);
    Q_INVOKABLE void commitEdit(const QString& text);
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void duplicateSelection();
    Q_INVOKABLE void zoomBy(qreal factor);
    Q_INVOKABLE void resetZoom();
    Q_INVOKABLE void fitToView();
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE QString selectedClass() const;

    // ---- for the render layers (render thread, GUI blocked) ----
    [[nodiscard]] Canvas* model() const { return ref_ ? ref_->canvas() : nullptr; }
    [[nodiscard]] quint64 renderRevision() const { return renderRevision_; }
    [[nodiscard]] QRectF visibleCanvasRect() const;
    [[nodiscard]] bool isSelected(BoxId id) const { return selection_.contains(id); }
    [[nodiscard]] bool isFlashing(BoxId id) const;
    [[nodiscard]] BoxId editingBox() const { return editing_; }
    [[nodiscard]] bool hasWidgetFrontend(const Box& box) const;
    struct CordKey {
        BoxId from;
        int outlet;
        BoxId to;
        int inlet;
        friend bool operator==(const CordKey&, const CordKey&) = default;
    };
    [[nodiscard]] const std::optional<CordKey>& selectedCord() const { return selectedCord_; }
    struct Transient {
        bool connecting = false;
        QPointF from, to;
        bool targetValid = false;
        QRectF targetInlet;
        bool rubber = false;
        QRectF rubberRect;
    };
    [[nodiscard]] Transient transient() const;

signals:
    void canvasChanged();
    void editModeChanged();
    void viewChanged();
    void selectionChanged();
    void statsChanged();
    void hoverTextChanged();
    /// Show an in-place text editor over `rect` (item coordinates).
    void editRequested(QRectF rect, QString text, QString kind);
    void editClosed();
    void contextMenuRequested(QPointF position, bool onBox);

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void updatePolish() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void hoverMoveEvent(QHoverEvent* e) override;
    void hoverLeaveEvent(QHoverEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    struct Hit {
        Box* box = nullptr;
        enum class Part { None, Body, Outlet } part = Part::None;
        int index = -1;
        std::optional<Connection> cord;
    };

    [[nodiscard]] QPointF toCanvas(QPointF item) const { return item / zoom_ + origin_; }
    [[nodiscard]] QPointF toItem(QPointF canvas) const { return (canvas - origin_) * zoom_; }
    [[nodiscard]] Hit hitTest(QPointF canvasPos) const;
    [[nodiscard]] int nearestInlet(const Box& box, QPointF canvasPos) const;
    [[nodiscard]] PatchDocument* document() const { return ref_ ? ref_->document() : nullptr; }
    [[nodiscard]] QList<BoxId> selectionList() const { return QList<BoxId>(selection_.begin(), selection_.end()); }

    void modelChanged();
    void invalidate();
    void applyView();
    void setSelection(QSet<BoxId> ids);
    void beginEdit(Box& box);
    void runClick(Box& box);
    void updateHover(QPointF canvasPos);
    void syncDelegates();
    void releaseDelegates();
    void pollSnapshots();
    QQmlComponent* componentFor(const FrontendInfo& info);

    QPointer<CanvasRef> ref_;
    bool editMode_ = false;
    qreal zoom_ = 1.0;
    QPointF origin_{0, 0};
    QSet<BoxId> selection_;
    std::optional<CordKey> selectedCord_;
    quint64 renderRevision_ = 1;
    QString hoverText_;

    enum class Drag { None, Move, Rubber, Connect, Pan };
    Drag drag_ = Drag::None;
    QPointF pressCanvas_, lastCanvas_, lastItem_, moveTotal_;
    QPointF mouseCanvas_;
    bool mouseInside_ = false;
    BoxId connectFrom_ = 0;
    int connectOutlet_ = 0;
    QPointF connectEnd_;
    std::optional<std::pair<BoxId, int>> connectTarget_;
    QRectF rubber_;
    QSet<BoxId> rubberBase_;

    BoxId editing_ = 0;
    std::optional<QPointF> placing_;
    QString placeKind_;
    BoxId autopatchFrom_ = 0;

    QHash<BoxId, qint64> flashes_;
    QElapsedTimer clock_;

    struct Delegate {
        QQuickItem* item = nullptr;
        QString key;
        Box* box = nullptr;
        NodeHandle* handle = nullptr;
        bool snapshots = false;
    };
    QHash<BoxId, Delegate> delegates_;
    QHash<QUrl, QQmlComponent*> components_;
    QSet<QUrl> reportedErrors_;

    QQuickItem* widgets_ = nullptr;
    CanvasLayer* boxesLayer_ = nullptr;
    CanvasLayer* overlayLayer_ = nullptr;
    QMetaObject::Connection refConnection_, refValidConnection_, frameConnection_;
};

/// One of the two scene graph layers of a CanvasView.
class CanvasLayer : public QQuickItem {
public:
    enum class Role { Boxes, Overlay };
    CanvasLayer(CanvasView* view, Role role);

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    QSGNode* updateBoxes(QSGNode* old);
    QSGNode* updateOverlay(QSGNode* old);

    CanvasView* view_;
    Role role_;
};

} // namespace qure::ui
