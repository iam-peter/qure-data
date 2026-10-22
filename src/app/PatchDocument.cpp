#include "PatchDocument.hpp"

#include "BoxLayout.hpp"
#include "NodeHandle.hpp"
#include "Session.hpp"

#include "PdFile.hpp"
#include "PdText.hpp"
#include "Runtime.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QUndoCommand>

#include <format>

namespace qure::ui {

// ===========================================================================
// CanvasRef

CanvasRef::CanvasRef(PatchDocument& doc, Canvas* canvas) : QObject(&doc), doc_(&doc), canvas_(canvas) {}

QString CanvasRef::title() const {
    if (!canvas_)
        return {};
    if (canvas_->isRoot())
        return doc_->title();
    QString name = QString::fromStdString(canvas_->name());
    if (canvas_->isAbstraction())
        return name;
    return QStringLiteral("%1 — %2").arg(name.isEmpty() ? QStringLiteral("(subpatch)") : name, doc_->title());
}

bool CanvasRef::isRoot() const { return canvas_ && canvas_->isRoot(); }

void CanvasRef::invalidate() {
    if (!canvas_)
        return;
    canvas_ = nullptr;
    emit validChanged();
}

// ===========================================================================
// Undo commands

namespace {

struct ConnRecord {
    BoxId from;
    int outlet;
    BoxId to;
    int inlet;
};

std::vector<ConnRecord> connectionsTouching(const Canvas& c, const QSet<BoxId>& ids, bool onlyExternal) {
    std::vector<ConnRecord> out;
    for (const auto& conn : c.connections()) {
        const bool f = ids.contains(conn.from->id), t = ids.contains(conn.to->id);
        if ((f || t) && (!onlyExternal || f != t))
            out.push_back({conn.from->id, conn.outlet, conn.to->id, conn.inlet});
    }
    return out;
}

void reconnect(Canvas& c, const std::vector<ConnRecord>& conns) {
    for (const auto& r : conns) {
        Box* a = c.findBox(r.from);
        Box* b = c.findBox(r.to);
        if (a && b && !c.isConnected(*a, r.outlet, *b, r.inlet))
            c.connect(*a, r.outlet, *b, r.inlet);
    }
}

class DocCommand : public QUndoCommand {
public:
    DocCommand(PatchDocument& doc, const Canvas& canvas, const QString& text)
        : doc_(doc), path_(doc.pathOf(canvas)) {
        setText(text);
    }

protected:
    Canvas* canvas() const { return doc_.resolve(path_); }

    PatchDocument& doc_;
    CanvasPath path_;
};

/// Inserts boxes from Pd text. Ids are recorded on the first run so redo
/// recreates the very same boxes.
class AddBoxesCommand final : public DocCommand {
public:
    AddBoxesCommand(PatchDocument& doc, Canvas& canvas, QString text, QPointF offset, const QString& label)
        : DocCommand(doc, canvas, label), text_(std::move(text)), offset_(offset) {}

    void redo() override {
        Canvas* c = canvas();
        if (!c)
            return;
        PasteOptions opt;
        opt.dx = offset_.x();
        opt.dy = offset_.y();
        opt.ids = ids_;
        auto r = pasteBoxes(*c, text_.toStdString(), opt);
        if (r && ids_.empty())
            for (Box* b : *r)
                ids_.push_back(b->id);
        if (r)
            for (Box* b : *r)
                doc_.relayout(*b);
        doc_.structureChanged(*c);
    }
    void undo() override {
        Canvas* c = canvas();
        if (!c)
            return;
        for (BoxId id : ids_)
            if (Box* b = c->findBox(id))
                c->removeBox(*b);
        doc_.structureChanged(*c);
    }

    [[nodiscard]] const std::vector<BoxId>& ids() const { return ids_; }

private:
    QString text_;
    QPointF offset_;
    std::vector<BoxId> ids_;
};

/// Removes boxes; undo re-inserts them at their old positions in the
/// canvas list with their old ids and reconnects them.
class RemoveBoxesCommand final : public DocCommand {
public:
    RemoveBoxesCommand(PatchDocument& doc, Canvas& canvas, const QList<BoxId>& ids)
        : DocCommand(doc, canvas, ids.size() == 1 ? QObject::tr("Delete box") : QObject::tr("Delete %1 boxes").arg(ids.size())) {
        QSet<BoxId> set(ids.begin(), ids.end());
        std::vector<Box*> boxes;
        for (const auto& b : canvas.boxes())
            if (set.contains(b->id)) {
                boxes.push_back(b.get());
                ids_.push_back(b->id);
                indices_.push_back(canvas.indexOf(*b));
            }
        text_ = copyBoxes(canvas, boxes);
        external_ = connectionsTouching(canvas, set, true);
    }

    void redo() override {
        Canvas* c = canvas();
        if (!c)
            return;
        for (BoxId id : ids_)
            if (Box* b = c->findBox(id))
                c->removeBox(*b);
        doc_.structureChanged(*c);
    }
    void undo() override {
        Canvas* c = canvas();
        if (!c)
            return;
        PasteOptions opt;
        opt.ids = ids_;
        opt.indices = indices_;
        if (auto r = pasteBoxes(*c, text_, opt))
            for (Box* b : *r)
                doc_.relayout(*b);
        reconnect(*c, external_);
        doc_.structureChanged(*c);
    }

private:
    std::string text_;
    std::vector<BoxId> ids_;
    std::vector<int> indices_;
    std::vector<ConnRecord> external_;
};

class SetTextCommand final : public DocCommand {
public:
    SetTextCommand(PatchDocument& doc, Canvas& canvas, Box& box, AtomVector text)
        : DocCommand(doc, canvas, QObject::tr("Edit box")), id_(box.id), index_(canvas.indexOf(box)),
          text_(std::move(text)) {
        std::vector<Box*> one{&box};
        snapshot_ = copyBoxes(canvas, one);
        conns_ = connectionsTouching(canvas, {box.id}, false);
    }

    void redo() override {
        Canvas* c = canvas();
        Box* b = c ? c->findBox(id_) : nullptr;
        if (!b)
            return;
        c->setText(*b, text_);
        reconnect(*c, conns_); // keep what still fits
        doc_.relayout(*b);
        doc_.structureChanged(*c);
    }
    void undo() override {
        Canvas* c = canvas();
        Box* b = c ? c->findBox(id_) : nullptr;
        if (!b)
            return;
        // Restore the exact previous box (including subpatch contents).
        c->removeBox(*b);
        const std::vector<BoxId> ids{id_};
        const std::vector<int> indices{index_};
        PasteOptions opt;
        opt.ids = ids;
        opt.indices = indices;
        if (auto r = pasteBoxes(*c, snapshot_, opt))
            for (Box* nb : *r)
                doc_.relayout(*nb);
        reconnect(*c, conns_);
        doc_.structureChanged(*c);
    }

private:
    BoxId id_;
    int index_;
    AtomVector text_;
    std::string snapshot_;
    std::vector<ConnRecord> conns_;
};

class MoveCommand final : public DocCommand {
public:
    MoveCommand(PatchDocument& doc, Canvas& canvas, QList<BoxId> ids, QPointF delta, bool applied)
        : DocCommand(doc, canvas, QObject::tr("Move")), ids_(std::move(ids)), delta_(delta), skip_(applied) {}

    void redo() override {
        if (skip_) {
            skip_ = false;
            return;
        }
        apply(delta_);
    }
    void undo() override { apply(-delta_); }
    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override {
        const auto* m = dynamic_cast<const MoveCommand*>(other);
        if (!m || m->ids_ != ids_ || m->path_ != path_)
            return false;
        delta_ += m->delta_;
        return true;
    }

private:
    void apply(QPointF delta) {
        if (Canvas* c = canvas())
            doc_.moveBoxesLive(*c, ids_, delta);
    }
    QList<BoxId> ids_;
    QPointF delta_;
    bool skip_;
};

class ConnectCommand final : public DocCommand {
public:
    ConnectCommand(PatchDocument& doc, Canvas& canvas, ConnRecord r, bool connect)
        : DocCommand(doc, canvas, connect ? QObject::tr("Connect") : QObject::tr("Disconnect")), r_(r),
          connect_(connect) {}

    void redo() override { set(connect_); }
    void undo() override { set(!connect_); }

private:
    void set(bool on) {
        Canvas* c = canvas();
        if (!c)
            return;
        Box* a = c->findBox(r_.from);
        Box* b = c->findBox(r_.to);
        if (!a || !b)
            return;
        if (on)
            c->connect(*a, r_.outlet, *b, r_.inlet);
        else
            c->disconnect({a, r_.outlet, b, r_.inlet});
        if (a->broken())
            doc_.relayout(*a);
        if (b->broken())
            doc_.relayout(*b);
        doc_.structureChanged(*c);
    }
    ConnRecord r_;
    bool connect_;
};

} // namespace

// ===========================================================================
// PatchDocument

PatchDocument::PatchDocument(Session& session, Canvas& root) : QObject(&session), session_(session), root_(root) {
    connect(&undo_, &QUndoStack::cleanChanged, this, &PatchDocument::modifiedChanged);
    connect(&undo_, &QUndoStack::canUndoChanged, this, &PatchDocument::undoStateChanged);
    connect(&undo_, &QUndoStack::canRedoChanged, this, &PatchDocument::undoStateChanged);
    connect(&undo_, &QUndoStack::undoTextChanged, this, &PatchDocument::undoStateChanged);
    connect(&undo_, &QUndoStack::redoTextChanged, this, &PatchDocument::undoStateChanged);
    relayoutAll(root_);
    rebuildIndex();
}

PatchDocument::~PatchDocument() {
    for (auto* r : std::as_const(refs_))
        r->invalidate();
}

CanvasRef* PatchDocument::refFor(Canvas* canvas) {
    if (!canvas)
        return nullptr;
    auto it = refs_.find(canvas);
    if (it != refs_.end())
        return *it;
    auto* ref = new CanvasRef(*this, canvas);
    refs_.insert(canvas, ref);
    return ref;
}

QString PatchDocument::title() const {
    if (!root_.file().empty())
        return QString::fromStdString(root_.file().filename().string());
    return QString::fromStdString(root_.name());
}

QUrl PatchDocument::fileUrl() const {
    if (root_.file().empty())
        return {};
    return QUrl::fromLocalFile(QString::fromStdString(root_.file().string()));
}

bool PatchDocument::save() {
    if (root_.file().empty())
        return false;
    QSaveFile f(QString::fromStdString(root_.file().string()));
    if (!f.open(QIODevice::WriteOnly)) {
        session_.postError(tr("cannot write %1").arg(f.fileName()));
        return false;
    }
    const auto text = writePatch(root_);
    f.write(text.data(), static_cast<qint64>(text.size()));
    if (!f.commit()) {
        session_.postError(tr("cannot write %1").arg(f.fileName()));
        return false;
    }
    undo_.setClean();
    dirty_ = false;
    emit modifiedChanged();
    session_.postInfo(tr("saved to %1").arg(f.fileName()));
    return true;
}

bool PatchDocument::saveAs(const QUrl& url) {
    const auto path = url.toLocalFile();
    if (path.isEmpty())
        return false;
    root_.setFile(std::filesystem::path(path.toStdString()));
    root_.setName(QFileInfo(path).fileName().toStdString());
    emit titleChanged();
    for (auto* r : std::as_const(refs_))
        r->retitle();
    return save();
}

// ---- tree helpers -----------------------------------------------------------

Canvas* PatchDocument::resolve(const CanvasPath& path) const {
    Canvas* c = &root_;
    for (BoxId id : path) {
        Box* b = c->findBox(id);
        if (!b || !b->child)
            return nullptr;
        c = b->child.get();
    }
    return c;
}

CanvasPath PatchDocument::pathOf(const Canvas& canvas) const {
    CanvasPath path;
    for (const Canvas* c = &canvas; c && c->owner(); c = c->parent())
        path.insert(path.begin(), c->owner()->id);
    return path;
}

void PatchDocument::structureChanged(Canvas& canvas) {
    if (batch_ > 0)
        pendingCommit_ = true;
    else {
        session_.commit();
        rebuildIndex();
    }
    if (auto* r = refs_.value(&canvas))
        emit r->changed();
    // Subpatch iolets may have changed on the parent.
    if (canvas.owner() && canvas.parent()) {
        relayout(*canvas.owner());
        if (auto* pr = refs_.value(canvas.parent()))
            emit pr->changed();
    }
}

void PatchDocument::geometryChanged(Canvas& canvas) {
    if (auto* r = refs_.value(&canvas))
        emit r->changed();
}

void PatchDocument::endBatch() {
    if (--batch_ == 0 && pendingCommit_) {
        pendingCommit_ = false;
        session_.commit();
        rebuildIndex();
    }
}

void PatchDocument::markModified() {
    if (!dirty_) {
        dirty_ = true;
        emit modifiedChanged();
    }
}

void PatchDocument::rebuildIndex() {
    byObject_.clear();
    QSet<Canvas*> liveCanvases;
    QSet<BoxId> liveBoxes;
    std::function<void(Canvas&)> walk = [&](Canvas& c) {
        liveCanvases.insert(&c);
        for (const auto& b : c.boxes()) {
            liveBoxes.insert(b->id);
            if (b->object)
                byObject_.insert(b->object->id(), b.get());
            if (auto it = handles_.find(b->id); it != handles_.end() && &(*it)->box() != b.get()) {
                // The box was recreated (undo): hand out a fresh handle.
                (*it)->deleteLater();
                handles_.erase(it);
            }
            if (b->child)
                walk(*b->child);
        }
    };
    walk(root_);
    for (auto it = refs_.begin(); it != refs_.end();) {
        if (!liveCanvases.contains(it.key())) {
            (*it)->invalidate();
            it = refs_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = handles_.begin(); it != handles_.end();) {
        if (!liveBoxes.contains(it.key())) {
            (*it)->deleteLater();
            it = handles_.erase(it);
        } else {
            ++it;
        }
    }
}

Box* PatchDocument::boxForObject(ObjectId id) const { return byObject_.value(id, nullptr); }

NodeHandle* PatchDocument::handleFor(Box& box) {
    if (auto it = handles_.find(box.id); it != handles_.end())
        return *it;
    auto* h = new NodeHandle(*this, box);
    handles_.insert(box.id, h);
    return h;
}

void PatchDocument::relayout(Box& box) { layout::layoutBox(box, session_.frontends()); }

void PatchDocument::relayoutAll(Canvas& canvas) {
    for (const auto& b : canvas.boxes()) {
        relayout(*b);
        if (b->child)
            relayoutAll(*b->child);
    }
}

void PatchDocument::textUpdatedByEngine(Box& box, AtomVector text) {
    box.text = std::move(text);
    relayout(box);
    box.canvas->touch();
    geometryChanged(*box.canvas);
}

// ---- editing --------------------------------------------------------------

namespace {
const char* recordKeyword(BoxKind kind) {
    switch (kind) {
    case BoxKind::Message: return "msg";
    case BoxKind::Comment: return "text";
    case BoxKind::FloatAtom: return "floatatom";
    case BoxKind::SymbolAtom: return "symbolatom";
    case BoxKind::ListAtom: return "listbox";
    default: return "obj";
    }
}
} // namespace

Box* PatchDocument::createBox(Canvas& canvas, BoxKind kind, QPointF pos, const QString& text) {
    const auto atoms = parseAtoms(text.toStdString());
    auto record = std::format("#X {} {} {}", recordKeyword(kind), formatFloat(std::round(pos.x())),
                              formatFloat(std::round(pos.y())));
    if (!atoms.empty())
        record += ' ' + formatAtoms(atoms, TextStyle::File);
    record += ";\n";
    auto* cmd = new AddBoxesCommand(*this, canvas, QString::fromStdString(record), {}, tr("Add box"));
    undo_.push(cmd);
    return cmd->ids().empty() ? nullptr : canvas.findBox(cmd->ids().front());
}

void PatchDocument::setBoxText(Canvas& canvas, BoxId id, const QString& text) {
    Box* box = canvas.findBox(id);
    if (!box)
        return;
    auto atoms = parseAtoms(text.toStdString());
    if (atoms == box->text)
        return;
    undo_.push(new SetTextCommand(*this, canvas, *box, std::move(atoms)));
}

void PatchDocument::removeBoxes(Canvas& canvas, const QList<BoxId>& ids) {
    if (!ids.isEmpty())
        undo_.push(new RemoveBoxesCommand(*this, canvas, ids));
}

void PatchDocument::moveBoxesLive(Canvas& canvas, const QList<BoxId>& ids, QPointF delta) {
    bool iolet = false;
    for (BoxId id : ids)
        if (Box* b = canvas.findBox(id)) {
            canvas.moveBox(*b, b->x + delta.x(), b->y + delta.y());
            iolet = iolet || (b->object && b->object->role() != ObjectRole::Normal);
        }
    if (iolet)
        structureChanged(canvas); // inlet order of the subpatch may change
    else
        geometryChanged(canvas);
}

void PatchDocument::moveBoxes(Canvas& canvas, const QList<BoxId>& ids, QPointF delta) {
    if (ids.isEmpty() || delta.isNull())
        return;
    // The move has already been applied live while dragging.
    undo_.push(new MoveCommand(*this, canvas, ids, delta, true));
}

bool PatchDocument::connectBoxes(Canvas& canvas, BoxId from, int outlet, BoxId to, int inlet) {
    Box* a = canvas.findBox(from);
    Box* b = canvas.findBox(to);
    if (!a || !b || !canvas.canConnect(*a, outlet, *b, inlet))
        return false;
    undo_.push(new ConnectCommand(*this, canvas, {from, outlet, to, inlet}, true));
    return true;
}

void PatchDocument::disconnect(Canvas& canvas, const Connection& c) {
    undo_.push(new ConnectCommand(*this, canvas, {c.from->id, c.outlet, c.to->id, c.inlet}, false));
}

QString PatchDocument::copy(Canvas& canvas, const QList<BoxId>& ids) const {
    std::vector<Box*> boxes;
    for (BoxId id : ids)
        if (Box* b = canvas.findBox(id))
            boxes.push_back(b);
    return QString::fromStdString(copyBoxes(canvas, boxes));
}

QList<BoxId> PatchDocument::paste(Canvas& canvas, const QString& text, QPointF offset) {
    auto* cmd = new AddBoxesCommand(*this, canvas, text, offset, tr("Paste"));
    undo_.push(cmd);
    return QList<BoxId>(cmd->ids().begin(), cmd->ids().end());
}

} // namespace qure::ui
