#pragma once

#include "Atom.hpp"
#include "Clock.hpp"
#include "Symbol.hpp"

#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace qure {

class Engine;
class NameBinding;
class Object;
class SnapshotBuffer;

using ObjectId = std::uint64_t;

/// Everything an object constructor gets to see. `args` are already
/// dollar-expanded for the canvas the box lives in; `rawArgs` are not (a
/// message box needs its `$1` to expand per message instead).
struct ObjectArgs {
    Engine& engine;
    Symbol className;
    AtomSpan args;
    AtomSpan rawArgs;
    int dollarZero = 0;
};

/// Constructors throw this for unusable arguments; the box is then shown as
/// broken with the message, like a failed creation in Pd.
struct ObjectError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct DspSetup {
    double sampleRate = 48000.0;
    int blockSize = 64;
};

/// Signal buffers handed to Object::process. Inputs and outputs never alias,
/// so kernels can be written as straightforward loops.
class DspIO {
public:
    DspIO(int frames, const float* const* ins, int numIns, float* const* outs, int numOuts) noexcept
        : frames_(frames), numIns_(numIns), numOuts_(numOuts), ins_(ins), outs_(outs) {}

    [[nodiscard]] int frames() const noexcept { return frames_; }
    [[nodiscard]] int numInputs() const noexcept { return numIns_; }
    [[nodiscard]] int numOutputs() const noexcept { return numOuts_; }
    [[nodiscard]] std::span<const float> in(int k) const noexcept {
        return {ins_[k], static_cast<std::size_t>(frames_)};
    }
    [[nodiscard]] std::span<float> out(int k) const noexcept {
        return {outs_[k], static_cast<std::size_t>(frames_)};
    }

private:
    int frames_, numIns_, numOuts_;
    const float* const* ins_;
    float* const* outs_;
};

/// Where a control connection leads. Plans store these in flat arrays.
struct Target {
    Object* object;
    int inlet;
};
struct OutletTargets {
    const Target* begin = nullptr;
    std::uint32_t count = 0;
};

/// How the plan compiler treats an object. Inlets and outlets of subpatches
/// are resolved away at compile time: messages and signals go straight to
/// their final destination, without Pd's per-hop forwarding.
enum class ObjectRole : unsigned char { Normal, Inlet, Outlet, SignalInlet, SignalOutlet };

/// An implicit ordering edge between DSP objects that communicate by name
/// ([send~]→[receive~], [throw~]→[catch~]): the provider is scheduled
/// before the consumer when that does not create a cycle, which removes the
/// one-block delay Pd would otherwise introduce.
struct DspLink {
    enum class Side : unsigned char { None, Provides, Consumes };
    Side side = Side::None;
    Binding::Kind kind = Binding::Kind::SignalSend;
    Symbol name;
};

enum class LogLevel : unsigned char { Error, Warning, Info, Debug };

/// Base class of every object in a patch.
///
/// Threading contract:
///  * Constructor, prepare() and the destructor run on the control (UI)
///    thread while the object is not visible to the engine thread.
///  * Every other virtual runs on the engine thread.
///  * Objects never touch each other's state except through messages.
class Object {
public:
    explicit Object(const ObjectArgs& args);
    virtual ~Object();
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    [[nodiscard]] ObjectId id() const noexcept { return id_; }
    [[nodiscard]] Symbol className() const noexcept { return className_; }
    [[nodiscard]] Engine& engine() const noexcept { return engine_; }

    // ---- iolets ----
    [[nodiscard]] int numInlets() const noexcept { return static_cast<int>(inlets_.size()); }
    [[nodiscard]] int numOutlets() const noexcept { return static_cast<int>(outlets_.size()); }
    [[nodiscard]] bool isSignalInlet(int i) const noexcept { return inlets_[i].signal; }
    [[nodiscard]] bool isSignalOutlet(int o) const noexcept { return outlets_[o].signal; }
    /// Index among signal inlets/outlets only (what DspIO::in/out use).
    [[nodiscard]] int signalInletIndex(int inlet) const noexcept { return inlets_[inlet].signalIndex; }
    [[nodiscard]] int signalOutletIndex(int outlet) const noexcept { return outlets_[outlet].signalIndex; }
    [[nodiscard]] int numSignalInlets() const noexcept { return numSignalIn_; }
    [[nodiscard]] int numSignalOutlets() const noexcept { return numSignalOut_; }

    // ---- messages (engine thread) ----
    /// Entry point for every message: dispatches on the selector.
    void receive(int inlet, Symbol selector, AtomSpan args);
    /// Interaction from the object's GUI front end (a click on a toggle, a
    /// key on a keyboard). Default: treat it as a message to inlet 0.
    virtual void uiEvent(Symbol selector, AtomSpan args) { receive(0, selector, args); }
    /// Called after a patch finished loading, for objects that registered
    /// interest (see wantsLoadbang).
    virtual void loadbang() {}
    [[nodiscard]] virtual bool wantsLoadbang() const { return false; }
    /// A message sent to a name this object is bound to as a receiver.
    virtual void receiveNamed(Symbol selector, AtomSpan args) { receive(0, selector, args); }

    // ---- lifecycle (engine thread) ----
    /// Called when the first plan containing the object is installed; bind
    /// receive names here. Idempotent wrapper around onActivate().
    void activate();
    /// Called when the object was removed from the patch; unbind names,
    /// stop clocks. Idempotent wrapper around onDeactivate().
    void deactivate();

    // ---- DSP ----
    [[nodiscard]] virtual bool wantsDsp() const { return numSignalIn_ > 0 || numSignalOut_ > 0; }
    /// Control thread, before the object is published or while the engine
    /// is stopped. May allocate (delay lines, tables...).
    virtual void prepare(const DspSetup&) {}
    /// Engine thread, once per block, in dependency order.
    virtual void process(const DspIO&) {}
    [[nodiscard]] virtual DspLink dspLink() const { return {}; }

    [[nodiscard]] virtual ObjectRole role() const { return ObjectRole::Normal; }
    /// Bulk data for the GUI (scope traces, array contents), if any.
    [[nodiscard]] virtual const SnapshotBuffer* snapshot() const { return nullptr; }

    /// Engine thread: current scalar of a signal inlet with no connection.
    [[nodiscard]] const float* signalScalarPtr(int signalInlet) const noexcept { return &scalars_[signalInlet]; }

protected:
    int addInlet() { return addIolet(inlets_, false, numSignalIn_); }
    int addSignalInlet(float initialScalar = 0.0f);
    int addOutlet() { return addIolet(outlets_, false, numSignalOut_); }
    int addSignalOutlet() { return addIolet(outlets_, true, numSignalOut_); }

    // Message handlers. Defaults mimic Pd: a list is spread over the inlets,
    // a float into a signal inlet sets its scalar, everything else is an
    // error posted to the console.
    virtual void onBang(int inlet);
    virtual void onFloat(int inlet, double f);
    virtual void onSymbol(int inlet, Symbol s);
    virtual void onList(int inlet, AtomSpan args);
    virtual void onAnything(int inlet, Symbol selector, AtomSpan args);

    virtual void onActivate() {}
    virtual void onDeactivate() {}

    // Output (engine thread). Depth-first, like Pd.
    void outBang(int outlet);
    void outFloat(int outlet, double f);
    void outSymbol(int outlet, Symbol s);
    void outList(int outlet, AtomSpan args);
    void outAnything(int outlet, Symbol selector, AtomSpan args);
    /// Sends `selector args` out, choosing bang/float/symbol/list forms the
    /// way Pd's outlet functions would.
    void outMessage(int outlet, Symbol selector, AtomSpan args);
    [[nodiscard]] bool outletConnected(int outlet) const noexcept {
        return bindings_ && bindings_[outlet].count > 0;
    }

    /// Pushes a value to this object's GUI front end (if any).
    void notifyUi(Symbol selector, AtomSpan args = {});

    /// Creates a clock owned by the object; it is unset automatically on
    /// deactivation.
    Clock& makeClock(std::function<void()> callback);
    /// Creates a name binding owned by the object. It binds itself on
    /// activation and unbinds on deactivation; rebind() changes the name at
    /// runtime (engine thread).
    NameBinding& makeBinding(Binding::Kind kind, Symbol name = {});

    void setSignalScalar(int signalInlet, float v) noexcept { scalars_[signalInlet] = v; }

    void post(LogLevel level, std::string_view text) const;
    template <class... A>
    void error(std::format_string<A...> fmt, A&&... a) const {
        postFormatted(LogLevel::Error, fmt.get(), std::make_format_args(a...));
    }
    template <class... A>
    void info(std::format_string<A...> fmt, A&&... a) const {
        postFormatted(LogLevel::Info, fmt.get(), std::make_format_args(a...));
    }

    [[nodiscard]] double now() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
    [[nodiscard]] int blockSize() const noexcept;

private:
    friend class Engine;

    struct Iolet {
        bool signal;
        int signalIndex;
    };
    static int addIolet(std::vector<Iolet>& v, bool signal, int& signalCount);
    void postFormatted(LogLevel level, std::string_view fmt, std::format_args args) const;
    void deliver(int outlet, Symbol selector, AtomSpan args);

    Engine& engine_;
    const ObjectId id_;
    const Symbol className_;
    std::vector<Iolet> inlets_;
    std::vector<Iolet> outlets_;
    int numSignalIn_ = 0;
    int numSignalOut_ = 0;
    std::vector<float> scalars_;
    std::vector<std::unique_ptr<Clock>> clocks_;
    std::vector<std::unique_ptr<NameBinding>> names_;
    const OutletTargets* bindings_ = nullptr; // engine thread; set on plan install
    bool active_ = false;                     // engine thread
};

/// An object's attachment to a name (receive name, table name...). The name
/// can be set at any time; the binding itself only exists while the object
/// is active, and is only touched on the engine thread.
class NameBinding {
public:
    NameBinding(Binding::Kind kind, Object* owner, Symbol name) : binding_(kind, owner), name_(name) {}
    ~NameBinding() { Symbol::unbind(binding_); }

    [[nodiscard]] Symbol name() const noexcept { return name_; }
    /// Changes the name; rebinds immediately if the owner is active.
    void rebind(Symbol name) {
        const bool wasBound = active_;
        unbind();
        name_ = name;
        if (wasBound)
            bind();
    }

private:
    friend class Object;
    void bind() {
        active_ = true;
        if (!name_.empty() && name_ != sym::empty())
            name_.bind(binding_);
    }
    void unbind() {
        active_ = false;
        Symbol::unbind(binding_);
    }

    Binding binding_;
    Symbol name_;
    bool active_ = false;
};

} // namespace qure
