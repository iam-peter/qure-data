// Scene graph side of CanvasView: two layers of batched geometry.
//
//   Boxes layer:   box backgrounds (SDF shader), then text (QSGTextNode)
//   [QML widgets]  live GUI front ends, between the two layers
//   Overlay layer: cords, iolets, selection outlines, rubber band
//
// Geometry is in canvas units under one transform node, so panning and
// zooming only change a matrix. Geometry is rebuilt when the view's render
// revision changes (model edit, selection...), in chunks so no single
// buffer gets huge.

#include "CanvasView.hpp"

#include "BoxLayout.hpp"
#include "SceneGraph.hpp"
#include "Session.hpp"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextNode>
#include <QTextLayout>

namespace qure::ui {
namespace {

constexpr std::size_t kBoxChunk = 4096;
constexpr std::size_t kCordChunk = 8192;
constexpr qreal kTextMinZoom = 0.35;
constexpr qreal kIoletMinZoom = 0.25;

QPointF outletPoint(const Box& b, int outlet) {
    const QRectF r = layout::outletRect(b, outlet);
    return {r.center().x(), r.bottom()};
}
QPointF inletPoint(const Box& b, int inlet) {
    const QRectF r = layout::inletRect(b, inlet);
    return {r.center().x(), r.top()};
}

QMatrix4x4 viewMatrix(const CanvasView& v) {
    QMatrix4x4 m;
    m.scale(static_cast<float>(v.zoom()), static_cast<float>(v.zoom()));
    m.translate(static_cast<float>(-v.origin().x()), static_cast<float>(-v.origin().y()));
    return m;
}

/// Keeps `nodes` at exactly `count` geometry nodes under `parent` (before
/// `before`, if given), sharing `material`.
void resizeChunks(std::vector<QSGGeometryNode*>& nodes, std::size_t count, QSGNode* parent, QSGMaterial* material,
                  QSGNode* before = nullptr) {
    while (nodes.size() > count) {
        parent->removeChildNode(nodes.back());
        delete nodes.back();
        nodes.pop_back();
    }
    while (nodes.size() < count) {
        auto* n = new QSGGeometryNode;
        n->setMaterial(material);
        n->setFlag(QSGNode::OwnsGeometry);
        if (before)
            parent->insertChildNodeBefore(n, before);
        else
            parent->appendChildNode(n);
        nodes.push_back(n);
    }
}

template <class Batch>
void setGeometry(QSGGeometryNode* node, const Batch& batch) {
    QSGGeometry* g = batch.build(node->geometry());
    if (g != node->geometry())
        node->setGeometry(g);
    node->markDirty(QSGNode::DirtyGeometry);
}

struct TextEntry {
    QSGTextNode* node = nullptr;
    QString text;
    QRgb color = 0;
    int chars = 0;
    bool used = false;
};

class BoxesRoot final : public QSGTransformNode {
public:
    BoxesRoot() { appendChildNode(texts); }
    ~BoxesRoot() override { removeAllChildNodes(); qDeleteAll(chunks); delete texts; }

    sg::BoxMaterial material;
    std::vector<QSGGeometryNode*> chunks;
    QSGNode* texts = new QSGNode;
    QHash<BoxId, TextEntry> entries;
    quint64 revision = 0;
    sg::BoxBatch batch;
};

class OverlayRoot final : public QSGTransformNode {
public:
    OverlayRoot() {
        for (auto* n : {iolets, extras}) {
            n->setFlag(QSGNode::OwnsGeometry);
            n->setMaterial(&boxMaterial);
        }
        tempCord->setFlag(QSGNode::OwnsGeometry);
        tempCord->setMaterial(&cordMaterial);
        appendChildNode(cordGroup);
        appendChildNode(iolets);
        appendChildNode(extras);
        appendChildNode(tempCord);
    }

    sg::CordMaterial cordMaterial;
    sg::BoxMaterial boxMaterial;
    QSGNode* cordGroup = new QSGNode;
    std::vector<QSGGeometryNode*> cords;
    QSGGeometryNode* iolets = new QSGGeometryNode;
    QSGGeometryNode* extras = new QSGGeometryNode;
    QSGGeometryNode* tempCord = new QSGGeometryNode;
    quint64 revision = 0;
    QRectF ioletArea; // canvas rect the iolet geometry covers
    sg::CordBatch cordBatch;
    sg::BoxBatch boxBatch;
};

void appendBox(sg::BoxBatch& batch, const Box& b, const Theme& th, bool selected, bool flashing) {
    if (b.w <= 0 || b.kind == BoxKind::Comment)
        return;
    QColor fill = th.boxFill;
    QColor stroke = th.boxBorder;
    float border = 1.0f;
    auto shape = sg::Shape::Rect;
    float notch = 4.0f;
    switch (b.kind) {
    case BoxKind::Message:
        fill = th.messageFill;
        shape = sg::Shape::Message;
        notch = std::min(4.0f, b.h / 3);
        break;
    case BoxKind::FloatAtom:
    case BoxKind::SymbolAtom:
    case BoxKind::ListAtom:
        fill = th.atomFill;
        shape = sg::Shape::Atom;
        notch = std::min(5.0f, b.h / 2);
        break;
    default: break;
    }
    if (b.broken()) {
        shape = sg::Shape::Broken;
        stroke = th.brokenBorder;
    }
    if (selected) {
        stroke = th.selection;
        border = 1.6f;
    }
    if (flashing) {
        stroke = th.flash;
        border = 3.0f;
    }
    batch.add(layout::boxRect(b), fill, stroke, border, shape, 2.0f, notch);
}

} // namespace

CanvasLayer::CanvasLayer(CanvasView* view, Role role) : QQuickItem(view), view_(view), role_(role) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::NoButton);
}

QSGNode* CanvasLayer::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    return role_ == Role::Boxes ? updateBoxes(old) : updateOverlay(old);
}

// ---------------------------------------------------------------------------

QSGNode* CanvasLayer::updateBoxes(QSGNode* old) {
    Canvas* canvas = view_->model();
    Session* session = Session::instance();
    if (!canvas || !session) {
        delete old;
        return nullptr;
    }
    auto* root = old ? static_cast<BoxesRoot*>(old) : new BoxesRoot;
    root->setMatrix(viewMatrix(*view_));

    const auto scale = static_cast<float>(view_->zoom() * window()->effectiveDevicePixelRatio());
    if (root->material.scale != scale) {
        root->material.scale = scale;
        for (auto* n : root->chunks)
            n->markDirty(QSGNode::DirtyMaterial);
    }
    const Theme& th = session->theme();
    const auto& boxes = canvas->boxes();
    const bool rebuild = root->revision != view_->renderRevision();

    if (rebuild) {
        const std::size_t chunks = (boxes.size() + kBoxChunk - 1) / kBoxChunk;
        resizeChunks(root->chunks, chunks, root, &root->material, root->texts);
        for (std::size_t c = 0; c < chunks; ++c) {
            root->batch.clear();
            const auto end = std::min(boxes.size(), (c + 1) * kBoxChunk);
            for (std::size_t i = c * kBoxChunk; i < end; ++i) {
                const Box& b = *boxes[i];
                appendBox(root->batch, b, th, view_->isSelected(b.id), view_->isFlashing(b.id));
            }
            setGeometry(root->chunks[c], root->batch);
        }
    }

    // Text for visible text boxes only.
    for (auto& e : root->entries)
        e.used = false;
    if (view_->zoom() >= kTextMinZoom) {
        const QRectF visible = view_->visibleCanvasRect();
        for (const auto& bp : boxes) {
            const Box& b = *bp;
            if (b.w <= 0 || b.id == view_->editingBox() || !visible.intersects(layout::boxRect(b)))
                continue;
            if (b.kind != BoxKind::Object && b.kind != BoxKind::Message && b.kind != BoxKind::Comment)
                continue;
            if (b.kind == BoxKind::Object && !b.broken() && view_->hasWidgetFrontend(b))
                continue;
            auto& e = root->entries[b.id];
            e.used = true;
            const QColor color = view_->isSelected(b.id)        ? th.selection
                                 : b.kind == BoxKind::Comment ? th.commentText
                                                              : th.text;
            if (!e.node || rebuild) {
                const QString text = QString::fromStdString(b.displayText());
                const int chars = layout::boxChars(b);
                if (!e.node || text != e.text || chars != e.chars || color.rgba() != e.color) {
                    if (!e.node) {
                        e.node = window()->createTextNode();
                        root->texts->appendChildNode(e.node);
                    } else {
                        e.node->clear();
                    }
                    e.text = text;
                    e.chars = chars;
                    e.color = color.rgba();
                    e.node->setColor(color);
                    auto tl = layout::textLayout(layout::wrapText(text, chars));
                    e.node->addTextLayout(QPointF(0, 0), tl.get());
                }
            }
            QMatrix4x4 m;
            const double padX = b.kind == BoxKind::Comment ? 1.0 : layout::kPadX;
            m.translate(static_cast<float>(b.x + padX), static_cast<float>(b.y + layout::kPadY));
            e.node->setMatrix(m);
        }
    }
    for (auto it = root->entries.begin(); it != root->entries.end();) {
        if (!it->used) {
            root->texts->removeChildNode(it->node);
            delete it->node;
            it = root->entries.erase(it);
        } else {
            ++it;
        }
    }
    root->revision = view_->renderRevision();
    return root;
}

// ---------------------------------------------------------------------------

QSGNode* CanvasLayer::updateOverlay(QSGNode* old) {
    Canvas* canvas = view_->model();
    Session* session = Session::instance();
    if (!canvas || !session) {
        delete old;
        return nullptr;
    }
    auto* root = old ? static_cast<OverlayRoot*>(old) : new OverlayRoot;
    root->setMatrix(viewMatrix(*view_));
    const auto scale = static_cast<float>(view_->zoom() * window()->effectiveDevicePixelRatio());
    if (root->cordMaterial.scale != scale) {
        root->cordMaterial.scale = scale;
        root->boxMaterial.scale = scale;
        for (auto* n : root->cords)
            n->markDirty(QSGNode::DirtyMaterial);
        for (auto* n : {root->iolets, root->extras, root->tempCord})
            n->markDirty(QSGNode::DirtyMaterial);
    }
    const Theme& th = session->theme();
    const bool rebuild = root->revision != view_->renderRevision();

    // Cords: all of them, chunked.
    if (rebuild) {
        const auto& conns = canvas->connections();
        const auto& sel = view_->selectedCord();
        const std::size_t chunks = (conns.size() + kCordChunk - 1) / kCordChunk;
        resizeChunks(root->cords, chunks, root->cordGroup, &root->cordMaterial);
        for (std::size_t c = 0; c < chunks; ++c) {
            root->cordBatch.clear();
            const auto end = std::min(conns.size(), (c + 1) * kCordChunk);
            for (std::size_t i = c * kCordChunk; i < end; ++i) {
                const auto& conn = conns[i];
                const bool signal = conn.isSignal();
                const bool selected = sel && sel->from == conn.from->id && sel->outlet == conn.outlet &&
                                      sel->to == conn.to->id && sel->inlet == conn.inlet;
                root->cordBatch.add(outletPoint(*conn.from, conn.outlet), inletPoint(*conn.to, conn.inlet),
                                    selected ? th.selection : (signal ? th.signalCord : th.controlCord),
                                    signal ? 2.0f : 1.0f);
            }
            setGeometry(root->cords[c], root->cordBatch);
        }
    }

    // Iolets: only around the visible area, rebuilt when it moves away.
    const QRectF visible = view_->visibleCanvasRect();
    if (view_->zoom() < kIoletMinZoom) {
        root->boxBatch.clear();
        setGeometry(root->iolets, root->boxBatch);
        root->ioletArea = {};
    } else if (rebuild || !root->ioletArea.contains(visible)) {
        const double mx = visible.width() * 0.5, my = visible.height() * 0.5;
        root->ioletArea = visible.adjusted(-mx, -my, mx, my);
        root->boxBatch.clear();
        for (const auto& bp : canvas->boxes()) {
            const Box& b = *bp;
            if (b.w <= 0 || !root->ioletArea.intersects(layout::boxRect(b)))
                continue;
            for (int i = 0; i < b.numInlets(); ++i) {
                const bool sig = b.inlets[static_cast<std::size_t>(i)];
                root->boxBatch.add(layout::inletRect(b, i), sig ? th.signalIolet : th.boxFill,
                                   sig ? th.signalIolet : th.controlIolet, 1.0f, sg::Shape::Rect, 0.5f);
            }
            for (int o = 0; o < b.numOutlets(); ++o) {
                const bool sig = b.outlets[static_cast<std::size_t>(o)];
                root->boxBatch.add(layout::outletRect(b, o), sig ? th.signalIolet : th.boxFill,
                                   sig ? th.signalIolet : th.controlIolet, 1.0f, sg::Shape::Rect, 0.5f);
            }
        }
        setGeometry(root->iolets, root->boxBatch);
    }

    // Per-frame extras: selection outlines over GUI nodes, rubber band,
    // highlighted target inlet, the cord being dragged.
    root->boxBatch.clear();
    for (const auto& bp : canvas->boxes()) {
        const Box& b = *bp;
        if (b.w > 0 && view_->isSelected(b.id) && view_->hasWidgetFrontend(b) && visible.intersects(layout::boxRect(b)))
            root->boxBatch.add(layout::boxRect(b).adjusted(-1, -1, 1, 1), Qt::transparent, th.selection, 2.0f);
    }
    const auto t = view_->transient();
    if (t.rubber)
        root->boxBatch.add(t.rubberRect, th.rubberBand, th.selection, 1.0f, sg::Shape::Rect, 0.0f);
    if (t.targetValid)
        root->boxBatch.add(t.targetInlet.adjusted(-1, -1, 1, 1), th.selection, th.selection, 1.0f);
    setGeometry(root->extras, root->boxBatch);

    root->cordBatch.clear();
    if (t.connecting)
        root->cordBatch.add(t.from, t.to, th.selection, 1.5f);
    setGeometry(root->tempCord, root->cordBatch);

    root->revision = view_->renderRevision();
    return root;
}

} // namespace qure::ui
