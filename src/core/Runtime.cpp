#include "Runtime.hpp"
#include "Compiler.hpp"
#include "PdFile.hpp"
#include "Plan.hpp"

#include <fstream>
#include <sstream>

namespace qure {

Runtime::Runtime(EngineConfig config) : engine_(config) { registry_.registerBuiltins(); }

Runtime::~Runtime() { patches_.clear(); }

Canvas& Runtime::newPatch(std::string name) {
    auto canvas = std::make_unique<Canvas>(*this, nullptr);
    canvas->setName(std::move(name));
    patches_.push_back(std::move(canvas));
    return *patches_.back();
}

static std::expected<std::string, std::string> readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::unexpected("cannot open " + file.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::expected<Canvas*, std::string> Runtime::openPatch(const std::filesystem::path& file) {
    auto text = readFile(file);
    if (!text)
        return std::unexpected(text.error());
    const auto absolute = std::filesystem::absolute(file);
    auto result = openPatchText(*text, absolute.filename().string(), absolute.parent_path());
    if (result)
        (*result)->setFile(absolute);
    return result;
}

std::expected<Canvas*, std::string> Runtime::openPatchText(std::string_view text, std::string name,
                                                           std::filesystem::path directory) {
    auto canvas = std::make_unique<Canvas>(*this, nullptr);
    canvas->setName(std::move(name));
    if (!directory.empty())
        canvas->setFile(directory / canvas->name());
    if (auto r = readPatch(*canvas, text); !r)
        return std::unexpected(r.error());
    Canvas* raw = canvas.get();
    patches_.push_back(std::move(canvas));
    commit();
    loadbang(*raw);
    return raw;
}

void Runtime::closePatch(Canvas& patch) {
    std::erase_if(patches_, [&](const auto& p) { return p.get() == &patch; });
    commit();
}

void Runtime::commit() {
    warnings_.clear();
    engine_.publish(compilePlan(patches_, warnings_, engine_.config().blockSize));
    engine_.reclaim();
}

static void collectLoadbangs(const Canvas& canvas, std::vector<ObjectId>& out) {
    for (const auto& b : canvas.boxes())
        if (b->child)
            collectLoadbangs(*b->child, out);
    for (const auto& b : canvas.boxes())
        if (b->object && b->object->wantsLoadbang())
            out.push_back(b->object->id());
}

void Runtime::loadbang(Canvas& canvas) {
    std::vector<ObjectId> ids;
    collectLoadbangs(canvas, ids);
    if (!ids.empty())
        engine_.requestLoadbang(ids);
}

std::filesystem::path Runtime::findAbstraction(const Canvas& from, std::string_view name) const {
    // Qure Data's own extension first, then Pd's, in each directory.
    auto lookIn = [&](const std::filesystem::path& dir) -> std::filesystem::path {
        std::error_code ec;
        for (const char* ext : {kPatchExtension, kPdExtension})
            if (auto p = dir / (std::string(name) + ext); std::filesystem::is_regular_file(p, ec))
                return p;
        return {};
    };
    if (auto dir = from.directory(); !dir.empty())
        if (auto p = lookIn(dir); !p.empty())
            return p;
    for (const auto& dir : searchPaths)
        if (auto p = lookIn(dir); !p.empty())
            return p;
    return {};
}

std::unique_ptr<Canvas> Runtime::loadAbstraction(Canvas& parent, const std::filesystem::path& file, AtomSpan args,
                                                 std::string& error) {
    if (abstractionDepth_ > 64) {
        error = "abstractions nested too deeply (recursive abstraction?)";
        return nullptr;
    }
    auto text = readFile(file);
    if (!text) {
        error = text.error();
        return nullptr;
    }
    auto canvas = std::make_unique<Canvas>(*this, &parent);
    canvas->abstraction_ = true;
    canvas->dollarZero_ = allocateDollarZero();
    canvas->args_.assign(args.begin(), args.end());
    canvas->file_ = file;
    canvas->name_ = file.filename().string();

    ++abstractionDepth_;
    auto r = readPatch(*canvas, *text);
    --abstractionDepth_;
    if (!r) {
        error = r.error();
        return nullptr;
    }
    return canvas;
}

void Runtime::instantiate(Box& box) {
    box.error.clear();
    const Canvas& env = box.canvas->environment();

    auto create = [&](Symbol cls, AtomSpan args, AtomSpan raw) {
        const auto* info = registry_.find(cls);
        if (!info) {
            box.error = "couldn't create";
            return;
        }
        try {
            const ObjectArgs oa{engine_, cls, args, raw, env.dollarZero_};
            std::shared_ptr<Object> obj = info->factory(oa);
            obj->prepare(engine_.dspSetup());
            engine_.objectCreated(obj);
            box.object = std::move(obj);
        } catch (const std::exception& e) {
            box.error = e.what();
        }
    };

    switch (box.kind) {
    case BoxKind::Comment:
    case BoxKind::Raw: return;
    case BoxKind::Message: create(Symbol("@msg"), {}, box.text); return;
    case BoxKind::FloatAtom:
    case BoxKind::SymbolAtom:
    case BoxKind::ListAtom: {
        const auto expanded = expandDollars(box.text, env.dollarZero_, env.args_);
        const char* cls = box.kind == BoxKind::FloatAtom    ? "@floatatom"
                          : box.kind == BoxKind::SymbolAtom ? "@symbolatom"
                                                            : "@listbox";
        create(Symbol(cls), expanded, box.text);
        return;
    }
    case BoxKind::Array: {
        const auto expanded = expandDollars(box.text, env.dollarZero_, env.args_);
        create(Symbol("@array"), expanded, box.text);
        return;
    }
    case BoxKind::Object: instantiateObjectBox(box); return;
    }
}

void Runtime::instantiateObjectBox(Box& box) {
    if (box.text.empty()) {
        box.child.reset();
        return; // an empty box is legal and inert
    }
    const Canvas& env = box.canvas->environment();
    const auto expanded = expandDollars(box.text, env.dollarZero_, env.args_);
    if (!expanded.front().isSymbol()) {
        box.error = "couldn't create (bad class name)";
        return;
    }
    const Symbol cls = expanded.front().asSymbol();
    const AtomSpan args = AtomSpan(expanded).subspan(1);

    if (cls == sym::pd()) {
        if (!box.child) {
            box.child = std::make_unique<Canvas>(*this, box.canvas);
            box.child->owner_ = &box;
        }
        box.child->setName(args.empty() ? std::string() : args.front().toString());
        return;
    }
    if (cls == Symbol("graph") && box.child)
        return;
    box.child.reset();

    if (registry_.find(cls)) {
        const auto* info = registry_.find(cls);
        try {
            const ObjectArgs oa{engine_, cls, args, AtomSpan(box.text).subspan(1), env.dollarZero_};
            std::shared_ptr<Object> obj = info->factory(oa);
            obj->prepare(engine_.dspSetup());
            engine_.objectCreated(obj);
            box.object = std::move(obj);
        } catch (const std::exception& e) {
            box.error = e.what();
        }
        return;
    }

    if (auto file = findAbstraction(*box.canvas, cls.str()); !file.empty()) {
        std::string error;
        box.child = loadAbstraction(*box.canvas, file, args, error);
        if (box.child)
            box.child->owner_ = &box;
        else
            box.error = error;
        return;
    }
    box.error = "couldn't create";
}

} // namespace qure
