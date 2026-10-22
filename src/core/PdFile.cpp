#include "PdFile.hpp"
#include "PdText.hpp"
#include "Runtime.hpp"
#include "Table.hpp"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace qure {
namespace {

/// One `;`-terminated record. `attrs` holds what follows an unescaped
/// comma, e.g. `f 12` in `#X obj 10 10 osc~, f 12;`.
struct Record {
    AtomVector atoms;
    AtomVector attrs;
    bool inAttrs = false;
};

std::vector<Record> splitRecords(std::string_view text) {
    std::vector<Record> records;
    Record current;
    tokenize(text, [&](const Token& t) {
        if (t.separator && t.atom.type() == Atom::Type::Semi) {
            if (!current.atoms.empty())
                records.push_back(std::move(current));
            current = {};
        } else if (t.separator && t.atom.type() == Atom::Type::Comma) {
            if (current.inAttrs)
                current.attrs.push_back(t.atom);
            current.inAttrs = true;
        } else if (current.inAttrs) {
            current.attrs.push_back(t.atom);
        } else {
            current.atoms.push_back(t.atom);
        }
    });
    if (!current.atoms.empty())
        records.push_back(std::move(current));
    return records;
}

double num(const AtomVector& a, std::size_t i, double fallback = 0) {
    return i < a.size() ? a[i].asFloat(fallback) : fallback;
}

AtomVector tail(const AtomVector& a, std::size_t from) {
    if (from >= a.size())
        return {};
    return AtomVector(a.begin() + static_cast<std::ptrdiff_t>(from), a.end());
}

int widthAttr(const Record& r) {
    if (r.attrs.size() >= 2 && r.attrs[0].asSymbol() == Symbol("f"))
        return static_cast<int>(r.attrs[1].asFloat());
    return 0;
}

} // namespace

/// Turns records into boxes. Shared by file loading and pasting.
class PatchReader {
public:
    PatchReader(Canvas& target, bool expectHeader, const PasteOptions* paste)
        : runtime_(target.runtime()), paste_(paste) {
        frames_.push_back(Frame{&target, nullptr, {}});
        expectHeader_ = expectHeader;
    }

    std::expected<std::vector<Box*>, std::string> run(std::string_view text) {
        auto records = splitRecords(text);
        if (expectHeader_) {
            if (records.empty() || records[0].atoms.size() < 2 || records[0].atoms[0].asSymbol() != Symbol("#N") ||
                records[0].atoms[1].asSymbol() != Symbol("canvas"))
                return std::unexpected("not a Pd patch (missing '#N canvas' header)");
            applyRootHeader(records[0].atoms);
        }
        for (std::size_t i = expectHeader_ ? 1 : 0; i < records.size(); ++i)
            if (auto r = handle(records[i]); !r)
                return std::unexpected(r.error());
        if (frames_.size() != 1)
            return std::unexpected("unbalanced '#N canvas' / '#X restore'");
        return frames_.front().created;
    }

private:
    struct Frame {
        Canvas* canvas;
        std::unique_ptr<Canvas> owned; // child awaiting its #X restore
        std::vector<Box*> created;
    };

    Frame& top() { return frames_.back(); }
    bool atPasteLevel() const { return frames_.size() == 1 && paste_; }

    void applyRootHeader(const AtomVector& a) {
        auto& p = frames_.front().canvas->props;
        p.windowX = static_cast<int>(num(a, 2, 50));
        p.windowY = static_cast<int>(num(a, 3, 50));
        p.windowW = static_cast<int>(num(a, 4, 640));
        p.windowH = static_cast<int>(num(a, 5, 480));
        p.font = static_cast<int>(num(a, 6, 12));
    }

    Box& add(Canvas::BoxSpec spec) {
        if (atPasteLevel()) {
            const auto n = top().created.size();
            spec.x += paste_->dx;
            spec.y += paste_->dy;
            if (n < paste_->ids.size())
                spec.id = paste_->ids[n];
            if (n < paste_->indices.size())
                spec.index = static_cast<std::size_t>(paste_->indices[n]);
        }
        Box& b = top().canvas->addBox(std::move(spec));
        top().created.push_back(&b);
        return b;
    }

    Canvas::BoxSpec spec(BoxKind kind, const Record& r, std::size_t textFrom = 4) {
        Canvas::BoxSpec s;
        s.kind = kind;
        s.x = num(r.atoms, 2);
        s.y = num(r.atoms, 3);
        s.text = tail(r.atoms, textFrom);
        s.width = widthAttr(r);
        return s;
    }

    std::expected<void, std::string> handle(const Record& r) {
        const auto& a = r.atoms;
        const Symbol head = a[0].asSymbol();
        if (head == Symbol("#N")) {
            if (a.size() >= 2 && a[1].asSymbol() == Symbol("canvas")) {
                auto child = std::make_unique<Canvas>(runtime_, top().canvas);
                auto& p = child->props;
                p.windowX = static_cast<int>(num(a, 2, 0));
                p.windowY = static_cast<int>(num(a, 3, 50));
                p.windowW = static_cast<int>(num(a, 4, 450));
                p.windowH = static_cast<int>(num(a, 5, 300));
                if (a.size() > 6) {
                    const auto name = a[6].toString();
                    child->setName(name == "(subpatch)" ? std::string() : name);
                }
                p.openOnLoad = num(a, 7) != 0;
                Canvas* raw = child.get();
                frames_.push_back(Frame{raw, std::move(child), {}});
            }
            // #N struct etc.: data structures are not supported yet.
            return {};
        }
        if (head == Symbol("#A")) {
            if (lastArray_ && a.size() >= 2 && a[1].isFloat())
                if (auto* t = dynamic_cast<TableStorage*>(lastArray_->object.get())) {
                    std::vector<float> values;
                    values.reserve(a.size() - 2);
                    for (std::size_t i = 2; i < a.size(); ++i)
                        values.push_back(static_cast<float>(a[i].asFloat()));
                    t->tableLoad(static_cast<std::size_t>(a[1].asFloat()), values);
                }
            return {};
        }
        if (head != Symbol("#X") || a.size() < 2)
            return {};

        const auto kind = a[1].asSymbol().str();
        if (kind == "obj") {
            add(spec(BoxKind::Object, r));
        } else if (kind == "msg") {
            add(spec(BoxKind::Message, r));
        } else if (kind == "text") {
            add(spec(BoxKind::Comment, r));
        } else if (kind == "floatatom") {
            add(spec(BoxKind::FloatAtom, r));
        } else if (kind == "symbolatom") {
            add(spec(BoxKind::SymbolAtom, r));
        } else if (kind == "listbox") {
            add(spec(BoxKind::ListAtom, r));
        } else if (kind == "restore") {
            if (frames_.size() < 2)
                return std::unexpected("'#X restore' without '#N canvas'");
            auto child = std::move(top().owned);
            frames_.pop_back();
            auto s = spec(BoxKind::Object, r);
            if (s.text.empty())
                s.text.emplace_back(sym::pd());
            s.child = std::move(child);
            add(std::move(s));
        } else if (kind == "connect") {
            const auto& created = top().created;
            const auto from = static_cast<std::size_t>(num(a, 2, -1));
            const auto outlet = static_cast<int>(num(a, 3));
            const auto to = static_cast<std::size_t>(num(a, 4, -1));
            const auto inlet = static_cast<int>(num(a, 5));
            if (from < created.size() && to < created.size())
                top().canvas->connect(*created[from], outlet, *created[to], inlet);
        } else if (kind == "coords") {
            auto& p = top().canvas->props;
            p.hasCoords = true;
            p.x1 = num(a, 2);
            p.y1 = num(a, 3);
            p.x2 = num(a, 4, 1);
            p.y2 = num(a, 5, 1);
            p.pixW = static_cast<int>(num(a, 6, 200));
            p.pixH = static_cast<int>(num(a, 7, 140));
            p.gop = static_cast<int>(num(a, 8));
            p.marginX = static_cast<int>(num(a, 9));
            p.marginY = static_cast<int>(num(a, 10));
        } else if (kind == "f") {
            if (!top().created.empty())
                top().created.back()->width = static_cast<int>(num(a, 2));
        } else if (kind == "array") {
            Canvas::BoxSpec s;
            s.kind = BoxKind::Array;
            s.text = tail(a, 2);
            lastArray_ = &add(std::move(s));
        } else if (kind == "declare" || kind == "scalar") {
            Canvas::BoxSpec s;
            s.kind = BoxKind::Raw;
            s.text = tail(a, 1);
            add(std::move(s));
        } else {
            top().canvas->extraRecords.push_back(tail(a, 1));
        }
        return {};
    }

    Runtime& runtime_;
    const PasteOptions* paste_;
    bool expectHeader_ = false;
    std::vector<Frame> frames_;
    Box* lastArray_ = nullptr;
};

std::expected<void, std::string> readPatch(Canvas& target, std::string_view text) {
    PatchReader reader(target, true, nullptr);
    auto r = reader.run(text);
    if (!r)
        return std::unexpected(r.error());
    return {};
}

std::expected<std::vector<Box*>, std::string> pasteBoxes(Canvas& canvas, std::string_view text,
                                                         const PasteOptions& options) {
    PatchReader reader(canvas, false, &options);
    return reader.run(text);
}

// ---------------------------------------------------------------------------
// Writing

namespace {

std::string fileText(AtomSpan atoms) { return formatAtoms(atoms, TextStyle::File); }

void writeCanvas(const Canvas& c, std::string& out, bool asRoot);

void writeBox(const Box& b, std::string& out) {
    const auto pos = std::format("{} {}", formatFloat(b.x), formatFloat(b.y));
    const auto width = b.width > 0 ? std::format(", f {}", b.width) : std::string();
    auto withText = [&](std::string_view kw) {
        out += std::format("#X {} {}", kw, pos);
        if (!b.text.empty())
            out += ' ' + fileText(b.text);
        out += width + ";\n";
    };
    switch (b.kind) {
    case BoxKind::Object:
        if (b.child && !b.child->isAbstraction()) {
            writeCanvas(*b.child, out, false);
            out += std::format("#X restore {}", pos);
            if (!b.text.empty())
                out += ' ' + fileText(b.text);
            out += ";\n";
            if (b.width > 0)
                out += std::format("#X f {};\n", b.width);
        } else {
            withText("obj");
        }
        break;
    case BoxKind::Message: withText("msg"); break;
    case BoxKind::Comment: withText("text"); break;
    case BoxKind::FloatAtom: out += std::format("#X floatatom {} {};\n", pos, fileText(b.text)); break;
    case BoxKind::SymbolAtom: out += std::format("#X symbolatom {} {};\n", pos, fileText(b.text)); break;
    case BoxKind::ListAtom: out += std::format("#X listbox {} {};\n", pos, fileText(b.text)); break;
    case BoxKind::Raw: out += std::format("#X {};\n", fileText(b.text)); break;
    case BoxKind::Array: {
        out += std::format("#X array {};\n", fileText(b.text));
        if (auto* t = dynamic_cast<const TableStorage*>(b.object.get()); t && t->tableSaveContents()) {
            const auto data = t->tableSnapshot();
            constexpr std::size_t chunk = 1000;
            for (std::size_t i = 0; i < data.size(); i += chunk) {
                out += std::format("#A {}", i);
                for (std::size_t j = i; j < std::min(data.size(), i + chunk); ++j)
                    out += ' ' + formatFloat(data[j]);
                out += ";\n";
            }
        }
        break;
    }
    }
}

void writeConnections(const Canvas& c, std::span<Box* const> boxes, std::string& out) {
    std::unordered_map<const Box*, std::size_t> index;
    for (std::size_t i = 0; i < boxes.size(); ++i)
        index[boxes[i]] = i;
    for (const auto& conn : c.connections()) {
        auto f = index.find(conn.from);
        auto t = index.find(conn.to);
        if (f != index.end() && t != index.end())
            out += std::format("#X connect {} {} {} {};\n", f->second, conn.outlet, t->second, conn.inlet);
    }
}

void writeCanvas(const Canvas& c, std::string& out, bool asRoot) {
    const auto& p = c.props;
    if (asRoot)
        out += std::format("#N canvas {} {} {} {} {};\n", p.windowX, p.windowY, p.windowW, p.windowH, p.font);
    else
        out += std::format("#N canvas {} {} {} {} {} {};\n", p.windowX, p.windowY, p.windowW, p.windowH,
                           c.name().empty() ? std::string("(subpatch)") : c.name(), p.openOnLoad ? 1 : 0);
    std::vector<Box*> all;
    all.reserve(c.boxes().size());
    for (const auto& b : c.boxes()) {
        writeBox(*b, out);
        all.push_back(b.get());
    }
    writeConnections(c, all, out);
    if (p.hasCoords) {
        out += std::format("#X coords {} {} {} {} {} {} {}", formatFloat(p.x1), formatFloat(p.y1), formatFloat(p.x2),
                           formatFloat(p.y2), p.pixW, p.pixH, p.gop);
        if (p.gop)
            out += std::format(" {} {}", p.marginX, p.marginY);
        out += ";\n";
    }
    for (const auto& rec : c.extraRecords)
        out += std::format("#X {};\n", fileText(rec));
}

} // namespace

std::string writePatch(const Canvas& canvas) {
    std::string out;
    writeCanvas(canvas, out, true);
    return out;
}

std::string copyBoxes(const Canvas& canvas, std::span<Box* const> boxes) {
    // Keep canvas order so connections and subpatch inlet order survive.
    std::vector<Box*> ordered(boxes.begin(), boxes.end());
    std::ranges::sort(ordered, {}, [&](const Box* b) { return canvas.indexOf(*b); });
    std::string out;
    for (const Box* b : ordered)
        writeBox(*b, out);
    writeConnections(canvas, ordered, out);
    return out;
}

} // namespace qure
