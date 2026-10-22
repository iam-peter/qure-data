#include "CanvasView.hpp"

#include "BoxLayout.hpp"
#include "FrontendRegistry.hpp"
#include "NodeHandle.hpp"
#include "Session.hpp"

#include "Runtime.hpp"

#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTimer>

#include <cmath>

namespace qure::ui {

namespace {
constexpr qreal kMinZoom = 0.05;
constexpr qreal kMaxZoom = 8.0;
constexpr qreal kWidgetMinZoom = 0.4; ///< below: GUI nodes are drawn as plain boxes
constexpr int kMaxLiveWidgets = 600;  ///< above: same, to keep panning smooth
constexpr qint64 kFlashMs = 140;

double distanceToSegment(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    double t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF d = p - (a + t * ab);
    return std::hypot(d.x(), d.y());
}

QPointF outletPoint(const Box& b, int outlet) {
    const QRectF r = layout::outletRect(b, outlet);
    return {r.center().x(), r.bottom()};
}
QPointF inletPoint(const Box& b, int inlet) {
    const QRectF r = layout::inletRect(b, inlet);
    return {r.center().x(), r.top()};
}
} // namespace

CanvasView::CanvasView(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, false);
    setFlag(ItemIsFocusScope, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(true);
    setClip(true);
    clock_.start();

    boxesLayer_ = new CanvasLayer(this, CanvasLayer::Role::Boxes);
    widgets_ = new QQuickItem(this);
    widgets_->setTransformOrigin(QQuickItem::TopLeft);
    widgets_->setZ(1);
    overlayLayer_ = new CanvasLayer(this, CanvasLayer::Role::Overlay);
    overlayLayer_->setZ(2);

    if (auto* s = Session::instance()) {
        frameConnection_ = connect(s, &Session::frame, this, &CanvasView::pollSnapshots);
        connect(s, &Session::themeChanged, this, &CanvasView::invalidate);
    }
}

CanvasView::~CanvasView() { releaseDelegates(); }

void CanvasView::setCanvas(CanvasRef* ref) {
    if (ref_ == ref)
        return;
    disconnect(refConnection_);
    disconnect(refValidConnection_);
    releaseDelegates();
    ref_ = ref;
    selection_.clear();
    selectedCord_.reset();
    if (ref_) {
        refConnection_ = connect(ref_, &CanvasRef::changed, this, &CanvasView::modelChanged);
        refValidConnection_ = connect(ref_, &CanvasRef::validChanged, this, [this] {
            releaseDelegates();
            invalidate();
        });
    }
    emit canvasChanged();
    emit selectionChanged();
    modelChanged();
}

void CanvasView::setEditMode(bool on) {
    if (editMode_ == on)
        return;
    editMode_ = on;
    widgets_->setEnabled(!on);
    for (auto& dg : delegates_)
        dg.handle->setEditMode(on);
    if (!on)
        cancelEdit();
    emit editModeChanged();
    invalidate();
}

void CanvasView::setZoom(qreal z) {
    z = std::clamp(z, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(z, zoom_))
        return;
    zoom_ = z;
    applyView();
}

void CanvasView::setOrigin(QPointF o) {
    if (o == origin_)
        return;
    origin_ = o;
    applyView();
}

int CanvasView::boxCount() const { return model() ? static_cast<int>(model()->boxes().size()) : 0; }

QRectF CanvasView::visibleCanvasRect() const { return {origin_, QSizeF(width() / zoom_, height() / zoom_)}; }

bool CanvasView::isFlashing(BoxId id) const {
    auto it = flashes_.constFind(id);
    return it != flashes_.constEnd() && *it > clock_.elapsed();
}

bool CanvasView::hasWidgetFrontend(const Box& box) const {
    return Session::instance() && Session::instance()->frontends().find(box) != nullptr;
}

void CanvasView::applyView() {
    widgets_->setScale(zoom_);
    widgets_->setPosition(-origin_ * zoom_);
    boxesLayer_->update();
    overlayLayer_->update();
    polish();
    emit viewChanged();
}

void CanvasView::invalidate() {
    ++renderRevision_;
    boxesLayer_->update();
    overlayLayer_->update();
    polish();
}

void CanvasView::modelChanged() {
    if (Canvas* c = model()) {
        // Drop selection entries for boxes that no longer exist.
        QSet<BoxId> live;
        for (const auto& b : c->boxes())
            live.insert(b->id);
        const auto before = selection_.size();
        selection_.intersect(live);
        if (selection_.size() != before)
            emit selectionChanged();
        if (selectedCord_) {
            const auto& k = *selectedCord_;
            Box* a = c->findBox(k.from);
            Box* b = c->findBox(k.to);
            if (!a || !b || !c->isConnected(*a, k.outlet, *b, k.inlet))
                selectedCord_.reset();
        }
    }
    emit statsChanged();
    invalidate();
}

void CanvasView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    for (auto* layer : {static_cast<QQuickItem*>(boxesLayer_), static_cast<QQuickItem*>(overlayLayer_)}) {
        layer->setSize(newGeometry.size());
        layer->update();
    }
    polish();
}

void CanvasView::setSelection(QSet<BoxId> ids) {
    if (ids == selection_)
        return;
    selection_ = std::move(ids);
    for (auto it = delegates_.begin(); it != delegates_.end(); ++it)
        it->handle->setSelected(selection_.contains(it.key()));
    emit selectionChanged();
    invalidate();
}

CanvasView::Transient CanvasView::transient() const {
    Transient t;
    Canvas* c = model();
    if (drag_ == Drag::Connect && c) {
        if (Box* from = c->findBox(connectFrom_)) {
            t.connecting = true;
            t.from = outletPoint(*from, connectOutlet_);
            t.to = connectEnd_;
            if (connectTarget_)
                if (Box* to = c->findBox(connectTarget_->first)) {
                    t.targetValid = true;
                    t.targetInlet = layout::inletRect(*to, connectTarget_->second);
                    t.to = inletPoint(*to, connectTarget_->second);
                }
        }
    }
    if (drag_ == Drag::Rubber) {
        t.rubber = true;
        t.rubberRect = rubber_;
    }
    return t;
}

// ---------------------------------------------------------------------------
// Hit testing

CanvasView::Hit CanvasView::hitTest(QPointF p) const {
    Hit hit;
    Canvas* c = model();
    if (!c)
        return hit;
    const auto& boxes = c->boxes();
    for (auto it = boxes.rbegin(); it != boxes.rend(); ++it) {
        Box* b = it->get();
        if (b->w <= 0)
            continue;
        const QRectF r = layout::boxRect(*b).adjusted(-1, -1, 1, 2);
        if (!r.contains(p))
            continue;
        hit.box = b;
        hit.part = Hit::Part::Body;
        if (editMode_ && b->numOutlets() > 0 && p.y() >= b->y + b->h - layout::kIoletHotspot) {
            // Nearest outlet, like Pd's canvas_doclick.
            const int n = b->numOutlets();
            const int idx = n > 1 ? static_cast<int>(((p.x() - b->x) * (n - 1) + b->w / 2) / b->w) : 0;
            const int clamped = std::clamp(idx, 0, n - 1);
            const QRectF orect = layout::outletRect(*b, clamped).adjusted(-3, -4, 3, 3);
            if (orect.contains(p)) {
                hit.part = Hit::Part::Outlet;
                hit.index = clamped;
            }
        }
        return hit;
    }
    // Cords: within a few screen pixels.
    const double tolerance = 4.0 / zoom_;
    for (const auto& conn : c->connections()) {
        const QPointF a = outletPoint(*conn.from, conn.outlet);
        const QPointF b = inletPoint(*conn.to, conn.inlet);
        if (distanceToSegment(p, a, b) <= tolerance) {
            hit.cord = conn;
            return hit;
        }
    }
    return hit;
}

int CanvasView::nearestInlet(const Box& box, QPointF p) const {
    const int n = box.numInlets();
    if (n <= 1)
        return n == 1 ? 0 : -1;
    const int idx = static_cast<int>(((p.x() - box.x) * (n - 1) + box.w / 2) / box.w);
    return std::clamp(idx, 0, n - 1);
}

// ---------------------------------------------------------------------------
// Mouse

void CanvasView::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus(Qt::MouseFocusReason);
    const QPointF pos = e->position();
    const QPointF p = toCanvas(pos);
    lastItem_ = pos;
    pressCanvas_ = lastCanvas_ = p;
    Canvas* c = model();
    if (!c) {
        e->ignore();
        return;
    }
    if (editing_ || placing_)
        cancelEdit();

    if (e->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    const Hit hit = hitTest(p);
    if (e->button() == Qt::RightButton) {
        if (hit.box && !selection_.contains(hit.box->id))
            setSelection({hit.box->id});
        emit contextMenuRequested(pos, hit.box != nullptr);
        return;
    }

    const bool runClickInEdit = editMode_ && (e->modifiers() & Qt::ControlModifier);
    if (!editMode_ || runClickInEdit) {
        if (hit.box)
            runClick(*hit.box);
        else
            drag_ = Drag::Pan; // drag the empty canvas around in run mode
        return;
    }

    if (hit.box && hit.part == Hit::Part::Outlet) {
        drag_ = Drag::Connect;
        connectFrom_ = hit.box->id;
        connectOutlet_ = hit.index;
        connectEnd_ = p;
        connectTarget_.reset();
        invalidate();
        return;
    }
    if (hit.box) {
        selectedCord_.reset();
        if (e->modifiers() & Qt::ShiftModifier) {
            auto sel = selection_;
            if (sel.contains(hit.box->id))
                sel.remove(hit.box->id);
            else
                sel.insert(hit.box->id);
            setSelection(sel);
        } else if (!selection_.contains(hit.box->id)) {
            setSelection({hit.box->id});
        }
        drag_ = Drag::Move;
        moveTotal_ = {};
        return;
    }
    if (hit.cord) {
        selectedCord_ = CordKey{hit.cord->from->id, hit.cord->outlet, hit.cord->to->id, hit.cord->inlet};
        setSelection({});
        invalidate();
        return;
    }
    selectedCord_.reset();
    rubberBase_ = (e->modifiers() & Qt::ShiftModifier) ? selection_ : QSet<BoxId>{};
    if (rubberBase_.isEmpty())
        setSelection({});
    drag_ = Drag::Rubber;
    rubber_ = QRectF(p, p);
    invalidate();
}

void CanvasView::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    const QPointF p = toCanvas(pos);
    mouseCanvas_ = p;
    Canvas* c = model();
    switch (drag_) {
    case Drag::Pan: setOrigin(origin_ - (pos - lastItem_) / zoom_); break;
    case Drag::Move:
        if (c && document()) {
            // Whole canvas units, so boxes stay on Pd's pixel grid.
            const QPointF raw = p - pressCanvas_ - moveTotal_;
            const QPointF step(std::round(raw.x()), std::round(raw.y()));
            if (!step.isNull()) {
                document()->moveBoxesLive(*c, selectionList(), step);
                moveTotal_ += step;
            }
        }
        break;
    case Drag::Rubber: {
        rubber_ = QRectF(pressCanvas_, p).normalized();
        QSet<BoxId> sel = rubberBase_;
        if (c)
            for (const auto& b : c->boxes())
                if (b->w > 0 && rubber_.intersects(layout::boxRect(*b)))
                    sel.insert(b->id);
        if (sel != selection_)
            setSelection(sel);
        else
            invalidate();
        break;
    }
    case Drag::Connect: {
        connectEnd_ = p;
        connectTarget_.reset();
        const Hit hit = hitTest(p);
        if (hit.box && c && hit.box->id != connectFrom_)
            if (Box* from = c->findBox(connectFrom_)) {
                const int inlet = nearestInlet(*hit.box, p);
                if (inlet >= 0 && c->canConnect(*from, connectOutlet_, *hit.box, inlet))
                    connectTarget_ = std::make_pair(hit.box->id, inlet);
            }
        invalidate();
        break;
    }
    case Drag::None: break;
    }
    lastItem_ = pos;
    lastCanvas_ = p;
}

void CanvasView::mouseReleaseEvent(QMouseEvent*) {
    Canvas* c = model();
    switch (drag_) {
    case Drag::Move:
        if (c && document() && !moveTotal_.isNull())
            document()->moveBoxes(*c, selectionList(), moveTotal_);
        break;
    case Drag::Connect:
        if (c && document() && connectTarget_)
            document()->connectBoxes(*c, connectFrom_, connectOutlet_, connectTarget_->first, connectTarget_->second);
        break;
    default: break;
    }
    drag_ = Drag::None;
    connectTarget_.reset();
    unsetCursor();
    invalidate();
}

void CanvasView::mouseDoubleClickEvent(QMouseEvent* e) {
    const QPointF p = toCanvas(e->position());
    const Hit hit = hitTest(p);
    if (!hit.box)
        return;
    if (editMode_ && !(e->modifiers() & Qt::ControlModifier)) {
        if (hit.box->kind == BoxKind::Object || hit.box->kind == BoxKind::Message ||
            hit.box->kind == BoxKind::Comment)
            beginEdit(*hit.box);
    } else if (hit.box->child && document()) {
        Session::instance()->openCanvas(document()->refFor(hit.box->child.get()));
    }
}

void CanvasView::runClick(Box& box) {
    if (box.kind == BoxKind::Message && box.object) {
        Session::instance()->runtime().engine().sendUiEvent(box.object->id(), Symbol("click"), {});
        flashes_[box.id] = clock_.elapsed() + kFlashMs;
        invalidate();
        QTimer::singleShot(kFlashMs + 10, this, &CanvasView::invalidate);
    } else if (box.child && document() && !hasWidgetFrontend(box)) {
        Session::instance()->openCanvas(document()->refFor(box.child.get()));
    }
}

void CanvasView::hoverMoveEvent(QHoverEvent* e) {
    mouseInside_ = true;
    mouseCanvas_ = toCanvas(e->position());
    updateHover(mouseCanvas_);
}

void CanvasView::hoverLeaveEvent(QHoverEvent*) { mouseInside_ = false; }

void CanvasView::updateHover(QPointF p) {
    const Hit hit = hitTest(p);
    QString text;
    if (hit.box) {
        const QString cls = QString::fromUtf8(hit.box->className().str());
        if (hit.box->broken())
            text = QStringLiteral("%1: %2").arg(cls, QString::fromStdString(hit.box->error));
        else if (hit.box->kind == BoxKind::Object && Session::instance())
            text = QStringLiteral("%1 – %2").arg(cls, Session::instance()->helpFor(cls));
        if (hit.part == Hit::Part::Outlet)
            text += tr("  (outlet %1: drag to connect)").arg(hit.index);
    }
    if (editMode_)
        setCursor(hit.part == Hit::Part::Outlet ? Qt::CrossCursor : (hit.box ? Qt::ArrowCursor : Qt::ArrowCursor));
    else
        setCursor(hit.box && (hit.box->kind == BoxKind::Message || hit.box->child) ? Qt::PointingHandCursor
                                                                                   : Qt::ArrowCursor);
    if (text != hoverText_) {
        hoverText_ = text;
        emit hoverTextChanged();
    }
}

void CanvasView::wheelEvent(QWheelEvent* e) {
    const QPointF pos = e->position();
    if (e->modifiers() & Qt::ControlModifier) {
        const double steps = e->angleDelta().y() / 120.0;
        const QPointF anchor = toCanvas(pos);
        zoom_ = std::clamp(zoom_ * std::pow(1.15, steps), kMinZoom, kMaxZoom);
        origin_ = anchor - pos / zoom_;
        applyView();
        return;
    }
    QPointF delta = !e->pixelDelta().isNull() ? QPointF(e->pixelDelta()) : QPointF(e->angleDelta()) / 2.0;
    if (e->modifiers() & Qt::ShiftModifier)
        delta = QPointF(delta.y(), delta.x());
    setOrigin(origin_ - delta / zoom_);
}

void CanvasView::keyPressEvent(QKeyEvent* e) {
    Canvas* c = model();
    if (!c || !document()) {
        e->ignore();
        return;
    }
    switch (e->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (editMode_) {
            if (selectedCord_) {
                Box* a = c->findBox(selectedCord_->from);
                Box* b = c->findBox(selectedCord_->to);
                if (a && b)
                    document()->disconnect(*c, {a, selectedCord_->outlet, b, selectedCord_->inlet});
                selectedCord_.reset();
            } else {
                deleteSelection();
            }
        }
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (!editMode_ || selection_.isEmpty()) {
            e->ignore();
            return;
        }
        const double step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        const QPointF delta = e->key() == Qt::Key_Left    ? QPointF(-step, 0)
                          : e->key() == Qt::Key_Right ? QPointF(step, 0)
                          : e->key() == Qt::Key_Up    ? QPointF(0, -step)
                                                      : QPointF(0, step);
        document()->moveBoxesLive(*c, selectionList(), delta);
        document()->moveBoxes(*c, selectionList(), delta);
        return;
    }
    case Qt::Key_Escape:
        if (drag_ != Drag::None) {
            drag_ = Drag::None;
            invalidate();
        } else {
            setSelection({});
            selectedCord_.reset();
        }
        return;
    default: e->ignore();
    }
}

// ---------------------------------------------------------------------------
// Editing

void CanvasView::beginEdit(Box& box) {
    editing_ = box.id;
    placing_.reset();
    setSelection({box.id});
    const QRectF r(toItem(QPointF(box.x, box.y)), QSizeF(std::max<double>(box.w, 60) * zoom_, box.h * zoom_));
    const char* kind = box.kind == BoxKind::Message ? "message" : box.kind == BoxKind::Comment ? "comment" : "object";
    emit editRequested(r, QString::fromStdString(box.displayText()), QString::fromLatin1(kind));
    invalidate();
}

void CanvasView::placeBox(const QString& kind, const QString& text, bool immediate) {
    Canvas* c = model();
    if (!c || !document())
        return;
    if (!editMode_)
        setEditMode(true);
    cancelEdit();

    // Where the mouse is: from hover events, or – when none has arrived yet,
    // e.g. a shortcut right after the window got focus – from the cursor.
    QPointF pos = visibleCanvasRect().center();
    if (mouseInside_ && visibleCanvasRect().contains(mouseCanvas_))
        pos = mouseCanvas_;
    else if (const QPointF local = mapFromGlobal(QCursor::pos()); contains(local))
        pos = toCanvas(local);
    autopatchFrom_ = 0;
    if (selection_.size() == 1) {
        // Pd's autopatching: place below the selected box and connect.
        if (Box* sel = c->findBox(*selection_.begin())) {
            pos = QPointF(sel->x, sel->y + sel->h + 12);
            if (sel->numOutlets() > 0)
                autopatchFrom_ = sel->id;
        }
    }
    pos = QPointF(std::round(pos.x()), std::round(pos.y()));

    const BoxKind bk = kind == QLatin1String("message")      ? BoxKind::Message
                       : kind == QLatin1String("comment")    ? BoxKind::Comment
                       : kind == QLatin1String("floatatom")  ? BoxKind::FloatAtom
                       : kind == QLatin1String("symbolatom") ? BoxKind::SymbolAtom
                       : kind == QLatin1String("listbox")    ? BoxKind::ListAtom
                                                             : BoxKind::Object;
    if (immediate || bk == BoxKind::FloatAtom || bk == BoxKind::SymbolAtom || bk == BoxKind::ListAtom) {
        QString t = text;
        if (t.isEmpty() && bk == BoxKind::FloatAtom)
            t = QStringLiteral("5 0 0 0 - - - 0");
        else if (t.isEmpty() && bk == BoxKind::SymbolAtom)
            t = QStringLiteral("10 0 0 0 - - - 0");
        else if (t.isEmpty() && bk == BoxKind::ListAtom)
            t = QStringLiteral("20 0 0 0 - - - 0");
        document()->beginBatch();
        Box* b = document()->createBox(*c, bk, pos, t);
        if (b && autopatchFrom_)
            if (Box* from = c->findBox(autopatchFrom_); from && b->numInlets() > 0)
                document()->connectBoxes(*c, from->id, 0, b->id, 0);
        document()->endBatch();
        if (b)
            setSelection({b->id});
        return;
    }
    placing_ = pos;
    placeKind_ = kind;
    const QRectF r(toItem(pos), QSizeF(80 * zoom_, layout::lineHeight() * zoom_ + 2 * layout::kPadY * zoom_));
    emit editRequested(r, text, kind);
}

void CanvasView::commitEdit(const QString& text) {
    Canvas* c = model();
    auto* doc = document();
    const QString trimmed = text.trimmed();
    if (c && doc) {
        if (placing_) {
            if (!trimmed.isEmpty()) {
                const BoxKind bk = placeKind_ == QLatin1String("message")   ? BoxKind::Message
                                   : placeKind_ == QLatin1String("comment") ? BoxKind::Comment
                                                                            : BoxKind::Object;
                doc->beginBatch();
                Box* b = doc->createBox(*c, bk, *placing_, trimmed);
                if (b && autopatchFrom_)
                    if (Box* from = c->findBox(autopatchFrom_); from && b->numInlets() > 0)
                        doc->connectBoxes(*c, from->id, 0, b->id, 0);
                doc->endBatch();
                if (b)
                    setSelection({b->id});
            }
        } else if (editing_) {
            if (trimmed.isEmpty() && c->findBox(editing_) &&
                c->findBox(editing_)->kind != BoxKind::Object)
                doc->removeBoxes(*c, {editing_});
            else
                doc->setBoxText(*c, editing_, trimmed);
        }
    }
    placing_.reset();
    editing_ = 0;
    autopatchFrom_ = 0;
    emit editClosed();
    invalidate();
    forceActiveFocus();
}

void CanvasView::cancelEdit() {
    if (!placing_ && !editing_)
        return;
    placing_.reset();
    editing_ = 0;
    emit editClosed();
    invalidate();
}

void CanvasView::deleteSelection() {
    if (Canvas* c = model(); c && document() && !selection_.isEmpty()) {
        document()->removeBoxes(*c, selectionList());
        setSelection({});
    }
}

void CanvasView::selectAll() {
    if (!editMode_)
        setEditMode(true);
    QSet<BoxId> all;
    if (Canvas* c = model())
        for (const auto& b : c->boxes())
            if (b->w > 0)
                all.insert(b->id);
    setSelection(all);
}

void CanvasView::copySelection() {
    if (Canvas* c = model(); c && document() && !selection_.isEmpty())
        QGuiApplication::clipboard()->setText(document()->copy(*c, selectionList()));
}

void CanvasView::cutSelection() {
    copySelection();
    deleteSelection();
}

void CanvasView::paste() {
    Canvas* c = model();
    if (!c || !document())
        return;
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.trimmed().startsWith(QLatin1String("#X")) && !text.trimmed().startsWith(QLatin1String("#N"))) {
        // Plain text: make it an object box (handy for pasting object names).
        if (!text.trimmed().isEmpty())
            placeBox(QStringLiteral("object"), text.trimmed(), true);
        return;
    }
    if (!editMode_)
        setEditMode(true);
    const auto ids = document()->paste(*c, text, QPointF(10, 10));
    setSelection(QSet<BoxId>(ids.begin(), ids.end()));
}

void CanvasView::duplicateSelection() {
    Canvas* c = model();
    if (!c || !document() || selection_.isEmpty())
        return;
    const auto ids = document()->paste(*c, document()->copy(*c, selectionList()), QPointF(10, 10));
    setSelection(QSet<BoxId>(ids.begin(), ids.end()));
}

void CanvasView::zoomBy(qreal factor) {
    const QPointF center(width() / 2, height() / 2);
    const QPointF anchor = toCanvas(center);
    zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    origin_ = anchor - center / zoom_;
    applyView();
}

void CanvasView::resetZoom() { zoomBy(1.0 / zoom_); }

void CanvasView::fitToView() {
    Canvas* c = model();
    if (!c || c->boxes().empty() || width() <= 0)
        return;
    QRectF bounds;
    for (const auto& b : c->boxes())
        if (b->w > 0)
            bounds = bounds.isNull() ? layout::boxRect(*b) : bounds.united(layout::boxRect(*b));
    bounds.adjust(-20, -20, 20, 20);
    zoom_ = std::clamp(std::min(width() / bounds.width(), height() / bounds.height()), kMinZoom, 2.0);
    origin_ = bounds.center() - QPointF(width(), height()) / (2 * zoom_);
    applyView();
}

void CanvasView::openSelected() {
    Canvas* c = model();
    if (!c || !document())
        return;
    for (BoxId id : std::as_const(selection_))
        if (Box* b = c->findBox(id); b && b->child)
            Session::instance()->openCanvas(document()->refFor(b->child.get()));
}

QString CanvasView::selectedClass() const {
    Canvas* c = model();
    if (!c || selection_.size() != 1)
        return {};
    Box* b = c->findBox(*selection_.begin());
    return b ? QString::fromUtf8(b->className().str()) : QString();
}

// ---------------------------------------------------------------------------
// QML front ends

QQmlComponent* CanvasView::componentFor(const FrontendInfo& info) {
    if (auto it = components_.find(info.qml); it != components_.end())
        return *it;
    QQmlEngine* engine = qmlEngine(this);
    if (!engine)
        return nullptr;
    auto* comp = new QQmlComponent(engine, info.qml, QQmlComponent::PreferSynchronous, this);
    components_.insert(info.qml, comp);
    if (comp->isLoading())
        connect(comp, &QQmlComponent::statusChanged, this, [this] { polish(); });
    return comp;
}

void CanvasView::releaseDelegates() {
    // Synchronously: the node handles they bind to may be about to die.
    for (auto& dg : delegates_)
        delete dg.item;
    delegates_.clear();
    emit statsChanged();
}

void CanvasView::updatePolish() { syncDelegates(); }

void CanvasView::syncDelegates() {
    Canvas* c = model();
    auto* doc = document();
    auto* session = Session::instance();
    if (!c || !doc || !session) {
        if (!delegates_.isEmpty())
            releaseDelegates();
        return;
    }
    const QRectF visible = visibleCanvasRect().adjusted(-50, -50, 50, 50);
    QHash<BoxId, std::pair<Box*, const FrontendInfo*>> wanted;
    if (zoom_ >= kWidgetMinZoom) {
        for (const auto& b : c->boxes()) {
            if (b->w <= 0 || !visible.intersects(layout::boxRect(*b)))
                continue;
            if (const auto* fe = session->frontends().find(*b)) {
                wanted.insert(b->id, {b.get(), fe});
                if (wanted.size() > kMaxLiveWidgets) {
                    wanted.clear(); // too many: fall back to plain boxes
                    break;
                }
            }
        }
    }
    bool changed = false;
    for (auto it = delegates_.begin(); it != delegates_.end();) {
        auto w = wanted.constFind(it.key());
        if (w == wanted.constEnd() || w->first != it->box || w->second->className != it->key) {
            if (it->item)
                it->item->deleteLater();
            it = delegates_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it) {
        if (delegates_.contains(it.key()))
            continue;
        Box* box = it->first;
        const FrontendInfo* fe = it->second;
        QQmlComponent* comp = componentFor(*fe);
        if (comp && comp->isLoading())
            continue; // retried when the component finishes loading
        Delegate dg{nullptr, fe->className, box, doc->handleFor(*box), fe->snapshots};
        if (comp && comp->isReady()) {
            QObject* obj = comp->createWithInitialProperties({{QStringLiteral("node"), QVariant::fromValue(dg.handle)}},
                                                             qmlContext(this));
            dg.item = qobject_cast<QQuickItem*>(obj);
            if (dg.item) {
                dg.item->setParentItem(widgets_);
                dg.handle->setEditMode(editMode_);
                dg.handle->setSelected(selection_.contains(box->id));
            } else {
                delete obj;
            }
        } else if (comp && comp->isError() && !reportedErrors_.contains(fe->qml)) {
            reportedErrors_.insert(fe->qml);
            session->postError(tr("front end %1: %2").arg(fe->qml.toString(), comp->errorString()));
        }
        delegates_.insert(it.key(), dg);
        changed = true;
    }
    for (auto& dg : delegates_) {
        if (!dg.item)
            continue;
        dg.item->setPosition(QPointF(dg.box->x, dg.box->y));
        dg.item->setSize(QSizeF(dg.box->w, dg.box->h));
    }
    if (changed)
        emit statsChanged();
}

void CanvasView::pollSnapshots() {
    for (auto& dg : delegates_)
        if (dg.snapshots && dg.handle)
            dg.handle->pollSamples();
}

} // namespace qure::ui
