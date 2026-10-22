#include "Patch.hpp"
#include "Runtime.hpp"

#include <algorithm>

namespace qure {

// ---------------------------------------------------------------------------
// Box

Symbol Box::className() const noexcept {
    if (text.empty())
        return {};
    return text.front().asSymbol();
}

std::string Box::displayText() const {
    switch (kind) {
    case BoxKind::Object:
    case BoxKind::Comment: return atomsToString(text);
    case BoxKind::Message: return atomsToString(text, true);
    case BoxKind::Array: return text.empty() ? std::string("array") : "array " + text.front().toString();
    case BoxKind::Raw: return atomsToString(text);
    case BoxKind::FloatAtom:
    case BoxKind::SymbolAtom:
    case BoxKind::ListAtom: return {};
    }
    return {};
}

// ---------------------------------------------------------------------------
// Canvas

Canvas::Canvas(Runtime& runtime, Canvas* parent) : runtime_(runtime), parent_(parent) {
    if (!parent_)
        dollarZero_ = runtime_.allocateDollarZero();
}

Canvas::~Canvas() {
    for (auto& b : boxes_)
        destroyContents(*b);
}

Canvas& Canvas::root() noexcept {
    Canvas* c = this;
    while (c->parent_)
        c = c->parent_;
    return *c;
}

const Canvas& Canvas::environment() const noexcept {
    const Canvas* c = this;
    while (c->parent_ && !c->abstraction_)
        c = c->parent_;
    return *c;
}

std::filesystem::path Canvas::directory() const {
    for (const Canvas* c = &environment(); c; c = c->parent_ ? &c->parent_->environment() : nullptr)
        if (!c->file_.empty())
            return c->file_.parent_path();
    return {};
}

Box* Canvas::findBox(BoxId id) const noexcept {
    for (const auto& b : boxes_)
        if (b->id == id)
            return b.get();
    return nullptr;
}

int Canvas::indexOf(const Box& box) const noexcept {
    for (std::size_t i = 0; i < boxes_.size(); ++i)
        if (boxes_[i].get() == &box)
            return static_cast<int>(i);
    return -1;
}

static bool isIoletRole(const Box& b) {
    if (!b.object)
        return false;
    const auto r = b.object->role();
    return r != ObjectRole::Normal;
}

Box& Canvas::addBox(BoxSpec spec) {
    auto box = std::make_unique<Box>();
    box->id = spec.id ? spec.id : runtime_.allocateBoxId();
    box->kind = spec.kind;
    box->canvas = this;
    box->x = spec.x;
    box->y = spec.y;
    box->width = spec.width;
    box->text = std::move(spec.text);
    box->child = std::move(spec.child);
    if (box->child) {
        box->child->parent_ = this;
        box->child->owner_ = box.get();
    }
    Box& ref = *box;
    if (spec.index && *spec.index < boxes_.size())
        boxes_.insert(boxes_.begin() + static_cast<std::ptrdiff_t>(*spec.index), std::move(box));
    else
        boxes_.push_back(std::move(box));

    runtime_.instantiate(ref);
    refreshIolets(ref);
    if (isIoletRole(ref))
        refreshOwnerIolets();
    touch();
    return ref;
}

Box& Canvas::addBox(BoxKind kind, double x, double y, AtomVector text) {
    BoxSpec spec;
    spec.kind = kind;
    spec.x = x;
    spec.y = y;
    spec.text = std::move(text);
    return addBox(std::move(spec));
}

void Canvas::destroyContents(Box& box) {
    if (box.object)
        runtime_.engine().objectRemoved(std::move(box.object));
    box.object.reset();
    box.child.reset();
}

void Canvas::setText(Box& box, AtomVector text) {
    const bool wasIolet = isIoletRole(box);

    // Renaming a subpatch keeps its contents, as in Pd.
    const Symbol pd = sym::pd();
    const bool keepSubpatch = box.kind == BoxKind::Object && box.child && !box.child->isAbstraction() &&
                              box.className() == pd && !text.empty() && text.front().asSymbol() == pd;
    if (keepSubpatch) {
        box.text = std::move(text);
        box.child->setName(box.text.size() > 1 ? box.text[1].toString() : std::string());
        touch();
        return;
    }

    destroyContents(box);
    box.text = std::move(text);
    box.error.clear();
    runtime_.instantiate(box);
    refreshIolets(box);
    dropInvalidConnections(box);
    if (wasIolet || isIoletRole(box))
        refreshOwnerIolets();
    touch();
}

void Canvas::removeBox(Box& box) {
    const bool wasIolet = isIoletRole(box);
    std::erase_if(connections_, [&](const Connection& c) { return c.from == &box || c.to == &box; });
    destroyContents(box);
    std::erase_if(boxes_, [&](const auto& b) { return b.get() == &box; });
    if (wasIolet)
        refreshOwnerIolets();
    touch();
}

void Canvas::moveBox(Box& box, double x, double y) {
    box.x = x;
    box.y = y;
    if (isIoletRole(box))
        refreshOwnerIolets();
    touch();
}

std::vector<Box*> Canvas::ioletBoxes(bool inlets) const {
    std::vector<Box*> out;
    for (const auto& b : boxes_) {
        if (!b->object)
            continue;
        const auto r = b->object->role();
        if (inlets ? (r == ObjectRole::Inlet || r == ObjectRole::SignalInlet)
                   : (r == ObjectRole::Outlet || r == ObjectRole::SignalOutlet))
            out.push_back(b.get());
    }
    std::ranges::stable_sort(out, {}, [](const Box* b) { return b->x; });
    return out;
}

void Canvas::refreshIolets(Box& box) {
    if (box.object) {
        box.inlets.resize(static_cast<std::size_t>(box.object->numInlets()));
        for (int i = 0; i < box.object->numInlets(); ++i)
            box.inlets[static_cast<std::size_t>(i)] = box.object->isSignalInlet(i);
        box.outlets.resize(static_cast<std::size_t>(box.object->numOutlets()));
        for (int o = 0; o < box.object->numOutlets(); ++o)
            box.outlets[static_cast<std::size_t>(o)] = box.object->isSignalOutlet(o);
    } else if (box.child) {
        box.inlets.clear();
        for (const Box* b : box.child->ioletBoxes(true))
            box.inlets.push_back(b->object->role() == ObjectRole::SignalInlet);
        box.outlets.clear();
        for (const Box* b : box.child->ioletBoxes(false))
            box.outlets.push_back(b->object->role() == ObjectRole::SignalOutlet);
    } else if (box.broken()) {
        // Keep whatever iolets the existing connections need.
        std::size_t ins = 0, outs = 0;
        for (const auto& c : connections_) {
            if (c.to == &box)
                ins = std::max(ins, static_cast<std::size_t>(c.inlet) + 1);
            if (c.from == &box)
                outs = std::max(outs, static_cast<std::size_t>(c.outlet) + 1);
        }
        box.inlets.resize(ins, false);
        box.outlets.resize(outs, false);
    } else {
        box.inlets.clear();
        box.outlets.clear();
    }
}

void Canvas::refreshOwnerIolets() {
    if (!owner_ || !parent_)
        return;
    parent_->refreshIolets(*owner_);
    parent_->dropInvalidConnections(*owner_);
    parent_->touch();
}

void Canvas::dropInvalidConnections(Box& box) {
    std::erase_if(connections_, [&](const Connection& c) {
        if (c.from == &box && c.outlet >= box.numOutlets())
            return true;
        if (c.to == &box && c.inlet >= box.numInlets())
            return true;
        if (c.from == &box || c.to == &box)
            return c.from->outlets[static_cast<std::size_t>(c.outlet)] && !c.to->broken() &&
                   !c.to->inlets[static_cast<std::size_t>(c.inlet)];
        return false;
    });
}

bool Canvas::isConnected(const Box& from, int outlet, const Box& to, int inlet) const {
    return std::ranges::any_of(connections_, [&](const Connection& c) {
        return c.from == &from && c.outlet == outlet && c.to == &to && c.inlet == inlet;
    });
}

bool Canvas::canConnect(const Box& from, int outlet, const Box& to, int inlet) const {
    if (&from == &to || outlet < 0 || inlet < 0)
        return false;
    const bool fromOk = outlet < from.numOutlets() || from.broken();
    const bool toOk = inlet < to.numInlets() || to.broken();
    if (!fromOk || !toOk)
        return false;
    if (outlet < from.numOutlets() && from.outlets[static_cast<std::size_t>(outlet)] && !to.broken() &&
        !to.inlets[static_cast<std::size_t>(inlet)])
        return false; // signal outlet into control inlet
    return !isConnected(from, outlet, to, inlet);
}

bool Canvas::connect(Box& from, int outlet, Box& to, int inlet) {
    if (!canConnect(from, outlet, to, inlet))
        return false;
    if (from.broken() && outlet >= from.numOutlets())
        from.outlets.resize(static_cast<std::size_t>(outlet) + 1, false);
    if (to.broken() && inlet >= to.numInlets())
        to.inlets.resize(static_cast<std::size_t>(inlet) + 1, false);
    connections_.push_back({&from, outlet, &to, inlet});
    touch();
    return true;
}

bool Canvas::disconnect(const Connection& c) {
    const auto n = std::erase(connections_, c);
    if (n)
        touch();
    return n > 0;
}

} // namespace qure
