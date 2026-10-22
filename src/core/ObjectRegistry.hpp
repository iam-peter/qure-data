#pragma once

#include "Object.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace qure {

/// Maps class names (and aliases) to factories.
class ObjectRegistry {
public:
    using Factory = std::function<std::unique_ptr<Object>(const ObjectArgs&)>;

    struct ClassInfo {
        std::string name;
        Factory factory;
        std::string help; ///< one-line description for the object browser
    };

    void add(std::string_view name, Factory factory, std::string_view help = {},
             std::initializer_list<std::string_view> aliases = {});

    template <class T>
    void add(std::string_view name, std::string_view help = {}, std::initializer_list<std::string_view> aliases = {}) {
        add(name, [](const ObjectArgs& a) -> std::unique_ptr<Object> { return std::make_unique<T>(a); }, help, aliases);
    }

    [[nodiscard]] const ClassInfo* find(Symbol name) const;
    [[nodiscard]] std::vector<std::string> classNames() const;

    /// Registers every built-in object.
    void registerBuiltins();

private:
    std::vector<std::unique_ptr<ClassInfo>> classes_;
    std::unordered_map<Symbol, const ClassInfo*> byName_;
};

/// An object whose behaviour is defined entirely by its QML front end:
/// messages to any inlet are forwarded to the GUI as `in <inlet> <selector>
/// <args...>`, and the GUI emits `out <outlet> <atoms...>` to send. This is
/// what lets a new GUI node (a keyboard, an XY pad...) be written in QML
/// alone.
std::unique_ptr<Object> makeFrontendObject(const ObjectArgs& args, int inlets, int outlets);

// Implemented in objects/*.cpp
void registerControlObjects(ObjectRegistry&);
void registerMathObjects(ObjectRegistry&);
void registerTimeObjects(ObjectRegistry&);
void registerGuiObjects(ObjectRegistry&);
void registerSignalObjects(ObjectRegistry&);
void registerFilterObjects(ObjectRegistry&);
void registerTableObjects(ObjectRegistry&);
void registerIoObjects(ObjectRegistry&);

} // namespace qure
