#pragma once

#include "Atom.hpp"
#include "Clock.hpp"
#include "Object.hpp"
#include "SpscRing.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace qure {

struct Plan;

struct EngineConfig {
    double sampleRate = 48000.0;
    int blockSize = 64;
    int inputChannels = 2;
    int outputChannels = 2;
};

/// What the engine reports back to the control thread.
struct EngineEvent {
    enum class Kind : unsigned char { Notify, Log, DspState };
    Kind kind;
    ObjectId object = 0;   ///< Notify
    Symbol selector;       ///< Notify
    AtomSpan args;         ///< Notify
    LogLevel level{};      ///< Log
    std::string_view text; ///< Log
    bool dspOn = false;    ///< DspState
};

/// The realtime half of the system.
///
/// There are exactly two threads involved:
///  * the *control thread* (the GUI) edits patches, compiles Plans and
///    publishes them, and sends/receives messages through lock-free rings;
///  * the *engine thread* (the audio callback, or a timer thread while no
///    audio device runs) calls process()/tick(). Everything it touches is
///    either owned by it or published to it with release/acquire.
///
/// No mutex is ever taken on the engine thread.
class Engine {
public:
    explicit Engine(EngineConfig config = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }
    [[nodiscard]] DspSetup dspSetup() const noexcept { return {config_.sampleRate, config_.blockSize}; }

    // =====================================================================
    // Control thread
    // =====================================================================
    [[nodiscard]] ObjectId allocateObjectId() noexcept { return nextObjectId_.fetch_add(1, std::memory_order_relaxed); }

    /// Registers a freshly created object; it is activated by the next plan.
    void objectCreated(std::shared_ptr<Object> object);
    /// Registers a removed object; it is deactivated by the next plan and
    /// destroyed (here, on the control thread) once no plan references it.
    void objectRemoved(std::shared_ptr<Object> object);

    /// Generation the next published plan will carry.
    [[nodiscard]] std::uint64_t nextGeneration() const noexcept { return nextGeneration_; }
    /// Attaches pending lifecycle lists and publishes. The engine thread
    /// picks the plan up at its next block boundary.
    void publish(std::unique_ptr<Plan> plan);
    /// Frees plans the engine thread no longer uses. Call regularly.
    void reclaim();
    /// The most recently published plan (control-thread view).
    [[nodiscard]] const Plan* latestPlan() const noexcept { return latest_; }

    /// Changes sample rate / block size / channel counts. Only valid while
    /// no driver is calling process(); re-prepares every live object.
    void reconfigure(const EngineConfig& config);

    // Messages into the engine (processed at the next block boundary).
    void sendToObject(ObjectId target, int inlet, Symbol selector, AtomSpan args);
    void sendUiEvent(ObjectId target, Symbol selector, AtomSpan args);
    void sendToName(Symbol name, Symbol selector, AtomSpan args);
    void requestLoadbang(std::span<const ObjectId> objects);
    void setDsp(bool on);

    /// Delivers everything the engine reported since the last call.
    void drainEvents(const std::function<void(const EngineEvent&)>& visit);

    // =====================================================================
    // Engine thread
    // =====================================================================
    /// Audio callback entry point: interleaved buffers, any frame count.
    /// Internally runs whole blocks; adds one block of latency.
    void process(const float* input, float* output, int frames) noexcept;
    /// Runs exactly one block with silent input (timer driver, tests).
    void tick() noexcept;

    [[nodiscard]] Scheduler& scheduler() noexcept { return scheduler_; }
    [[nodiscard]] double now() const noexcept { return scheduler_.now(); }
    [[nodiscard]] bool dspRunning() const noexcept { return dspOn_; }
    [[nodiscard]] std::span<float> inputBus(int channel) noexcept;
    [[nodiscard]] std::span<float> outputBus(int channel) noexcept;

    /// Sends a message to every receiver bound to `name`.
    void deliverToName(Symbol name, Symbol selector, AtomSpan args);
    void notify(ObjectId object, Symbol selector, AtomSpan args) noexcept;
    void log(LogLevel level, std::string_view text) noexcept;
    void setDspFromEngine(bool on) noexcept;

    /// Guards against infinite message recursion (Pd's "stack overflow").
    [[nodiscard]] bool enterMessage() noexcept { return ++depth_ <= kMaxDepth; }
    void leaveMessage() noexcept { --depth_; }

    /// Thread-safe flag: has the engine processed at least one block since
    /// the last call? Used by tests and drivers to wait for plan installs.
    [[nodiscard]] std::uint64_t installedGeneration() const noexcept {
        return installedGeneration_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t blocksProcessed() const noexcept {
        return blocks_.load(std::memory_order_relaxed);
    }

private:
    static constexpr int kMaxDepth = 1000;

    enum class Command : std::uint32_t { ToObject, UiEvent, ToName, Loadbang, SetDsp };
    struct CommandHeader {
        std::uint64_t requiredGeneration;
        std::uint64_t target;
        SymbolData* selector;
        SymbolData* name;
        Command command;
        std::uint32_t inlet;
        std::uint32_t argc;
    };
    struct EventHeader {
        EngineEvent::Kind kind;
        LogLevel level;
        bool flag;
        std::uint64_t object;
        SymbolData* selector;
        std::uint32_t argc; ///< atoms, or bytes of text for Log
    };

    void pushCommand(Command cmd, std::uint64_t target, Symbol selector, Symbol name, int inlet, AtomSpan args);
    void installPending() noexcept;
    void drainCommands() noexcept;
    void runDsp() noexcept;
    void runBlock() noexcept;
    void pushEvent(const EventHeader& h, const void* payload, std::size_t bytes) noexcept;

    EngineConfig config_;

    // control thread state
    std::atomic<ObjectId> nextObjectId_{1};
    std::uint64_t nextGeneration_ = 1;
    std::uint64_t lastPublished_ = 0;
    struct Pending {
        std::uint64_t generation;
        std::shared_ptr<Object> object;
    };
    std::vector<Pending> pendingActivate_;
    std::vector<Pending> pendingDeactivate_;
    std::vector<std::unique_ptr<Plan>> plans_; ///< published, not yet reclaimed
    const Plan* latest_ = nullptr;

    // shared
    std::atomic<Plan*> pending_{nullptr};
    std::atomic<std::uint64_t> installedGeneration_{0};
    std::atomic<std::uint64_t> blocks_{0};
    SpscRing commands_{1 << 20};
    SpscRing events_{1 << 20};

    // engine thread state
    Plan* current_ = nullptr;
    Scheduler scheduler_;
    bool dspOn_ = false;
    int depth_ = 0;
    std::vector<float> inBus_, outBus_;   // channel-major, blockSize each
    int ioPos_ = 0;                       // frame position inside the current block
    std::unique_ptr<Object> pdReceiver_;  // handles "; pd dsp 1"
    std::vector<Atom> scratchAtoms_;
};

} // namespace qure
