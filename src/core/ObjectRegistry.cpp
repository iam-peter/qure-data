#include "ObjectRegistry.hpp"

#include <algorithm>

namespace qure {

void ObjectRegistry::add(std::string_view name, Factory factory, std::string_view help,
                         std::initializer_list<std::string_view> aliases) {
    auto info = std::make_unique<ClassInfo>(ClassInfo{std::string(name), std::move(factory), std::string(help)});
    byName_[Symbol(name)] = info.get();
    for (auto alias : aliases)
        byName_[Symbol(alias)] = info.get();
    classes_.push_back(std::move(info));
}

const ObjectRegistry::ClassInfo* ObjectRegistry::find(Symbol name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

std::vector<std::string> ObjectRegistry::classNames() const {
    std::vector<std::string> names;
    names.reserve(byName_.size());
    for (const auto& [sym, info] : byName_)
        names.emplace_back(sym.str());
    std::ranges::sort(names);
    return names;
}

void ObjectRegistry::registerBuiltins() {
    registerControlObjects(*this);
    registerMathObjects(*this);
    registerTimeObjects(*this);
    registerGuiObjects(*this);
    registerSignalObjects(*this);
    registerFilterObjects(*this);
    registerTableObjects(*this);
    registerIoObjects(*this);
}

} // namespace qure
