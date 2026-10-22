#pragma once

#include "Atom.hpp"
#include "Object.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qure {

class Canvas;
class Runtime;

using BoxId = std::uint64_t;

/// The kinds of boxes a Pd canvas can contain.
enum class BoxKind : unsigned char {
    Object,     ///< `#X obj` – includes subpatches, abstractions, graphs and GUI objects
    Message,    ///< `#X msg`
    FloatAtom,  ///< `#X floatatom`
    SymbolAtom, ///< `#X symbolatom`
    ListAtom,   ///< `#X listbox`
    Comment,    ///< `#X text`
    Array,      ///< `#X array` inside a graph
    Raw,        ///< any other `#X` record that occupies a slot (`declare`, `scalar`) – kept verbatim
};

/// One box on a canvas: the document-level view of an object.
struct Box {
    BoxId id = 0;
    BoxKind kind = BoxKind::Object;
    Canvas* canvas = nullptr;

    double x = 0, y = 0;
    int width = 0;   ///< fixed width in characters (`, f N`), 0 = automatic
    AtomVector text; ///< object: class + args; message: contents; atoms: properties

    std::shared_ptr<Object> object; ///< engine instance (null for comments, subpatches, broken boxes)
    std::unique_ptr<Canvas> child;  ///< subpatch, abstraction instance or graph
    std::string error;              ///< non-empty: the box failed to create

    /// Signal flag per inlet/outlet. Derived from the object, from the
    /// child's [inlet]/[outlet] boxes, or (for broken boxes) from the
    /// connections that use it.
    std::vector<bool> inlets;
    std::vector<bool> outlets;

    /// Size in canvas units, maintained by the GUI (it knows the font).
    float w = 0, h = 0;

    /// Opaque per-box GUI state (e.g. a NodeHandle), owned by the GUI layer.
    void* uiData = nullptr;

    [[nodiscard]] bool broken() const noexcept { return !error.empty(); }
    [[nodiscard]] Symbol className() const noexcept;
    [[nodiscard]] bool isSubpatch() const noexcept { return child != nullptr; }
    [[nodiscard]] int numInlets() const noexcept { return static_cast<int>(inlets.size()); }
    [[nodiscard]] int numOutlets() const noexcept { return static_cast<int>(outlets.size()); }
    /// Text shown inside the box.
    [[nodiscard]] std::string displayText() const;
};

struct Connection {
    Box* from;
    int outlet;
    Box* to;
    int inlet;

    friend bool operator==(const Connection&, const Connection&) = default;
    [[nodiscard]] bool isSignal() const noexcept { return from->outlets[static_cast<std::size_t>(outlet)]; }
};

/// Window/graph properties carried through load and save.
struct CanvasProps {
    int windowX = 50, windowY = 50, windowW = 640, windowH = 480;
    int font = 12;
    bool openOnLoad = false; ///< subpatch window "vis" flag
    // Graph-on-parent (`#X coords`)
    bool hasCoords = false;
    double x1 = 0, y1 = 0, x2 = 1, y2 = 1; ///< value range
    int pixW = 200, pixH = 140;            ///< size on parent
    int gop = 0;                           ///< 0 none, 1 graph-on-parent, 2 + hide name
    int marginX = 0, marginY = 0;
};

/// A patch window's contents. Canvases form a tree: the root is a document
/// (a .pd file), children are subpatches and abstraction instances.
///
/// All methods run on the control thread. They create and destroy engine
/// objects but do not publish anything; call Runtime::commit() afterwards
/// (once per user action, so a paste of 1000 boxes compiles once).
class Canvas {
public:
    Canvas(Runtime& runtime, Canvas* parent);
    ~Canvas();
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    [[nodiscard]] Runtime& runtime() const noexcept { return runtime_; }
    [[nodiscard]] Canvas* parent() const noexcept { return parent_; }
    [[nodiscard]] Box* owner() const noexcept { return owner_; }
    [[nodiscard]] Canvas& root() noexcept;
    /// The canvas defining `$0` and `$1...`: the nearest root or abstraction.
    [[nodiscard]] const Canvas& environment() const noexcept;
    [[nodiscard]] bool isAbstraction() const noexcept { return abstraction_; }
    [[nodiscard]] bool isRoot() const noexcept { return parent_ == nullptr; }
    [[nodiscard]] int dollarZero() const noexcept { return environment().dollarZero_; }
    [[nodiscard]] AtomSpan arguments() const noexcept { return environment().args_; }
    /// Directory used to resolve abstractions and files.
    [[nodiscard]] std::filesystem::path directory() const;

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }
    void setFile(std::filesystem::path f) { file_ = std::move(f); }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string n) { name_ = std::move(n); }

    CanvasProps props;
    /// `#X` records we do not interpret but keep for saving (in order).
    std::vector<AtomVector> extraRecords;

    // ---- contents ----
    [[nodiscard]] const std::vector<std::unique_ptr<Box>>& boxes() const noexcept { return boxes_; }
    [[nodiscard]] const std::vector<Connection>& connections() const noexcept { return connections_; }
    [[nodiscard]] Box* findBox(BoxId id) const noexcept;
    [[nodiscard]] int indexOf(const Box& box) const noexcept;
    /// Bumped on every structural change; GUIs use it to refresh caches.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    void touch() noexcept { ++revision_; }

    struct BoxSpec {
        BoxKind kind = BoxKind::Object;
        double x = 0, y = 0;
        AtomVector text;
        int width = 0;
        std::unique_ptr<Canvas> child;  ///< pre-built child (from a file)
        std::optional<std::size_t> index; ///< insert position (default: append)
        BoxId id = 0;                   ///< reuse an id (undo); 0 = allocate
    };
    Box& addBox(BoxSpec spec);
    Box& addBox(BoxKind kind, double x, double y, AtomVector text);

    /// Changes a box's text and re-creates its object. Connections that no
    /// longer fit are dropped.
    void setText(Box& box, AtomVector text);
    /// Removes a box and all its connections.
    void removeBox(Box& box);
    /// Moves a box. Recomputes subpatch iolets if an [inlet]/[outlet] moved.
    void moveBox(Box& box, double x, double y);

    [[nodiscard]] bool canConnect(const Box& from, int outlet, const Box& to, int inlet) const;
    bool connect(Box& from, int outlet, Box& to, int inlet);
    bool disconnect(const Connection& c);
    [[nodiscard]] bool isConnected(const Box& from, int outlet, const Box& to, int inlet) const;

    /// The [inlet]/[inlet~] (or [outlet]/[outlet~]) boxes in x order: they
    /// define the iolets of the box that owns this canvas.
    [[nodiscard]] std::vector<Box*> ioletBoxes(bool inlets) const;

private:
    friend class Runtime;
    friend class PatchReader;

    void refreshIolets(Box& box);
    void refreshOwnerIolets();
    void dropInvalidConnections(Box& box);
    void destroyContents(Box& box);

    Runtime& runtime_;
    Canvas* parent_;
    Box* owner_ = nullptr;
    bool abstraction_ = false;
    int dollarZero_ = 0;
    AtomVector args_;
    std::filesystem::path file_;
    std::string name_;
    std::vector<std::unique_ptr<Box>> boxes_;
    std::vector<Connection> connections_;
    std::uint64_t revision_ = 0;
};

} // namespace qure
