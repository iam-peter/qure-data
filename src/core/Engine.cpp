#include "Engine.hpp"
#include "Plan.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>

namespace qure {

static_assert(std::is_trivially_copyable_v<Atom>, "atoms are memcpy'd through the rings");

namespace {

/// Receiver for messages to "pd", e.g. a message box `; pd dsp 1`.
class PdReceiver final : public Object {
public:
    explicit PdReceiver(const ObjectArgs& a) : Object(a) {}
    void receiveNamed(Symbol selector, AtomSpan args) override {
        if (selector == sym::dsp())
            engine().setDspFromEngine(!args.empty() && args[0].asFloat() != 0.0);
        // "fast-forward", "quit", ... are not supported yet.
    }
};

} // namespace

Engine::Engine(EngineConfig config) : config_(config) {
    // Make sure the common symbols are interned on this (control) thread so
    // the engine thread never hits the symbol table's first-use path.
    for (auto s : {sym::bang(), sym::float_(), sym::symbol(), sym::list(), sym::set(), sym::empty(), sym::pd(),
                   sym::dsp()})
        (void)s;
    inBus_.assign(static_cast<std::size_t>(config_.inputChannels * config_.blockSize), 0.0f);
    outBus_.assign(static_cast<std::size_t>(config_.outputChannels * config_.blockSize), 0.0f);
    scratchAtoms_.reserve(4096);

    const ObjectArgs args{*this, sym::pd(), {}, {}, 0};
    pdReceiver_ = std::make_unique<PdReceiver>(args);
    // No engine thread exists yet, so binding here is safe.
    static_cast<Object*>(pdReceiver_.get())->makeBinding(Binding::Kind::Receiver, sym::pd());
    pdReceiver_->activate();
}

Engine::~Engine() {
    // Drivers must be stopped by now. Deactivate whatever is still running
    // so bindings and clocks are released before objects die.
    if (current_) {
        for (auto& o : current_->objects)
            o->deactivate();
    }
    pdReceiver_->deactivate();
    if (auto* p = pending_.exchange(nullptr))
        (void)p; // owned by plans_
    plans_.clear();
}

// ===========================================================================
// Control thread

void Engine::objectCreated(std::shared_ptr<Object> object) {
    pendingActivate_.push_back({nextGeneration_, std::move(object)});
}

void Engine::objectRemoved(std::shared_ptr<Object> object) {
    pendingDeactivate_.push_back({nextGeneration_, std::move(object)});
}

void Engine::publish(std::unique_ptr<Plan> plan) {
    plan->generation = nextGeneration_++;
    const auto installed = installedGeneration_.load(std::memory_order_acquire);

    // Lifecycle entries already confirmed by an installed plan are done.
    auto confirmed = [installed](const Pending& p) { return p.generation <= installed; };
    std::erase_if(pendingActivate_, confirmed);
    std::erase_if(pendingDeactivate_, confirmed);
    // Everything else goes into this plan too. If the engine raced us and
    // installed a predecessor meanwhile, an object may be (de)activated
    // twice – both operations are idempotent.
    for (auto& p : pendingActivate_)
        plan->activate.push_back(p.object);
    for (auto& p : pendingDeactivate_)
        plan->deactivate.push_back(p.object);

    Plan* raw = plan.get();
    plans_.push_back(std::move(plan));
    latest_ = raw;
    lastPublished_ = raw->generation;
    if (Plan* skipped = pending_.exchange(raw, std::memory_order_acq_rel)) {
        // The engine never saw it; drop it right away.
        std::erase_if(plans_, [skipped](const auto& p) { return p.get() == skipped; });
    }
}

void Engine::reclaim() {
    const auto installed = installedGeneration_.load(std::memory_order_acquire);
    std::erase_if(plans_, [installed](const auto& p) { return p->generation < installed; });
}

void Engine::reconfigure(const EngineConfig& config) {
    config_ = config;
    inBus_.assign(static_cast<std::size_t>(config_.inputChannels * config_.blockSize), 0.0f);
    outBus_.assign(static_cast<std::size_t>(config_.outputChannels * config_.blockSize), 0.0f);
    ioPos_ = 0;
    const auto setup = dspSetup();
    if (latest_)
        for (auto& o : latest_->objects)
            o->prepare(setup);
    for (auto& p : pendingActivate_)
        p.object->prepare(setup);
}

void Engine::pushCommand(Command cmd, std::uint64_t target, Symbol selector, Symbol name, int inlet, AtomSpan args) {
    const CommandHeader h{lastPublished_,         target, selector.data(), name.data(), cmd,
                          static_cast<std::uint32_t>(inlet), static_cast<std::uint32_t>(args.size())};
    const auto bytes = sizeof h + args.size_bytes();
    commands_.push(bytes, [&](std::span<std::byte> buf) {
        std::memcpy(buf.data(), &h, sizeof h);
        if (!args.empty())
            std::memcpy(buf.data() + sizeof h, args.data(), args.size_bytes());
    });
}

void Engine::sendToObject(ObjectId target, int inlet, Symbol selector, AtomSpan args) {
    pushCommand(Command::ToObject, target, selector, {}, inlet, args);
}

void Engine::sendUiEvent(ObjectId target, Symbol selector, AtomSpan args) {
    pushCommand(Command::UiEvent, target, selector, {}, 0, args);
}

void Engine::sendToName(Symbol name, Symbol selector, AtomSpan args) {
    pushCommand(Command::ToName, 0, selector, name, 0, args);
}

void Engine::requestLoadbang(std::span<const ObjectId> objects) {
    constexpr std::size_t chunk = 1024;
    std::vector<Atom> ids;
    for (std::size_t i = 0; i < objects.size(); i += chunk) {
        ids.clear();
        for (std::size_t j = i; j < std::min(objects.size(), i + chunk); ++j)
            ids.emplace_back(static_cast<double>(objects[j]));
        pushCommand(Command::Loadbang, 0, {}, {}, 0, ids);
    }
}

void Engine::setDsp(bool on) { pushCommand(Command::SetDsp, 0, {}, {}, on ? 1 : 0, {}); }

void Engine::drainEvents(const std::function<void(const EngineEvent&)>& visit) {
    std::vector<Atom> atoms;
    while (events_.pop([&](std::span<const std::byte> rec) {
        EventHeader h;
        std::memcpy(&h, rec.data(), sizeof h);
        EngineEvent e{h.kind};
        const auto* payload = rec.data() + sizeof h;
        switch (h.kind) {
        case EngineEvent::Kind::Notify:
            atoms.resize(h.argc);
            if (h.argc)
                std::memcpy(atoms.data(), payload, h.argc * sizeof(Atom));
            e.object = h.object;
            e.selector = Symbol::fromData(h.selector);
            e.args = atoms;
            break;
        case EngineEvent::Kind::Log:
            e.level = h.level;
            e.text = std::string_view(reinterpret_cast<const char*>(payload), h.argc);
            break;
        case EngineEvent::Kind::DspState: e.dspOn = h.flag; break;
        }
        visit(e);
        return true;
    })) {
    }
}

// ===========================================================================
// Engine thread

std::span<float> Engine::inputBus(int channel) noexcept {
    if (channel < 0 || channel >= config_.inputChannels)
        return {};
    return {inBus_.data() + static_cast<std::size_t>(channel * config_.blockSize),
            static_cast<std::size_t>(config_.blockSize)};
}

std::span<float> Engine::outputBus(int channel) noexcept {
    if (channel < 0 || channel >= config_.outputChannels)
        return {};
    return {outBus_.data() + static_cast<std::size_t>(channel * config_.blockSize),
            static_cast<std::size_t>(config_.blockSize)};
}

void Engine::installPending() noexcept {
    Plan* p = pending_.exchange(nullptr, std::memory_order_acq_rel);
    if (!p)
        return;
    for (auto& o : p->activate)
        o->activate();
    for (auto& o : p->deactivate)
        o->deactivate();
    for (const auto& b : p->bindings)
        b.object->bindings_ = p->outletTables.data() + b.firstOutlet;
    current_ = p;
    installedGeneration_.store(p->generation, std::memory_order_release);
}

void Engine::drainCommands() noexcept {
    const auto installed = current_ ? current_->generation : 0;
    std::array<Atom, 64> local;
    while (commands_.pop([&](std::span<const std::byte> rec) {
        CommandHeader h;
        std::memcpy(&h, rec.data(), sizeof h);
        if (h.requiredGeneration > installed)
            return false; // wait until the plan this command refers to is live

        std::span<Atom> args;
        if (h.argc <= local.size()) {
            args = std::span<Atom>(local.data(), h.argc);
        } else {
            scratchAtoms_.resize(h.argc); // rare: very long lists
            args = scratchAtoms_;
        }
        if (h.argc)
            std::memcpy(args.data(), rec.data() + sizeof h, h.argc * sizeof(Atom));

        auto findObject = [&](std::uint64_t id) -> Object* {
            if (!current_)
                return nullptr;
            auto it = current_->byId.find(id);
            return it == current_->byId.end() ? nullptr : it->second;
        };
        const Symbol selector = Symbol::fromData(h.selector);

        switch (h.command) {
        case Command::ToObject:
            if (auto* o = findObject(h.target); o && static_cast<int>(h.inlet) < std::max(1, o->numInlets()))
                o->receive(static_cast<int>(h.inlet), selector, args);
            break;
        case Command::UiEvent:
            if (auto* o = findObject(h.target))
                o->uiEvent(selector, args);
            break;
        case Command::ToName:
            deliverToName(Symbol::fromData(h.name), selector, args);
            break;
        case Command::Loadbang:
            for (const auto& a : args)
                if (auto* o = findObject(static_cast<std::uint64_t>(a.asFloat())))
                    o->loadbang();
            break;
        case Command::SetDsp: setDspFromEngine(h.inlet != 0); break;
        }
        return true;
    })) {
    }
}

void Engine::deliverToName(Symbol name, Symbol selector, AtomSpan args) {
    for (Binding* b = name.firstBinding(); b;) {
        Binding* next = b->next; // a receiver may unbind itself while handling
        if (b->kind == Binding::Kind::Receiver)
            b->owner->receiveNamed(selector, args);
        b = next;
    }
}

void Engine::pushEvent(const EventHeader& h, const void* payload, std::size_t bytes) noexcept {
    events_.push(sizeof h + bytes, [&](std::span<std::byte> buf) {
        std::memcpy(buf.data(), &h, sizeof h);
        if (bytes)
            std::memcpy(buf.data() + sizeof h, payload, bytes);
    });
}

void Engine::notify(ObjectId object, Symbol selector, AtomSpan args) noexcept {
    const EventHeader h{EngineEvent::Kind::Notify, LogLevel::Info, false, object, selector.data(),
                        static_cast<std::uint32_t>(args.size())};
    pushEvent(h, args.data(), args.size_bytes());
}

void Engine::log(LogLevel level, std::string_view text) noexcept {
    const EventHeader h{EngineEvent::Kind::Log, level, false, 0, nullptr, static_cast<std::uint32_t>(text.size())};
    pushEvent(h, text.data(), text.size());
}

void Engine::setDspFromEngine(bool on) noexcept {
    dspOn_ = on;
    const EventHeader h{EngineEvent::Kind::DspState, LogLevel::Info, on, 0, nullptr, 0};
    pushEvent(h, nullptr, 0);
}

void Engine::runDsp() noexcept {
    const int n = config_.blockSize;
    const auto& steps = current_->steps;
    const float* const* ins = current_->inPtrs.data();
    float* const* outs = current_->outPtrs.data();
    for (const DspStep& s : steps) {
        switch (s.op) {
        case DspStep::Op::Process:
            s.object->process(DspIO(n, ins + s.inBegin, static_cast<int>(s.inCount), outs + s.outBegin,
                                    static_cast<int>(s.outCount)));
            break;
        case DspStep::Op::Sum: {
            float* __restrict dst = outs[s.outBegin];
            const float* __restrict first = ins[s.inBegin];
            std::copy_n(first, n, dst);
            for (std::uint32_t k = 1; k < s.inCount; ++k) {
                const float* __restrict src = ins[s.inBegin + k];
                for (int i = 0; i < n; ++i)
                    dst[i] += src[i];
            }
            break;
        }
        case DspStep::Op::Fill: std::fill_n(outs[s.outBegin], n, *s.scalar); break;
        }
    }
}

void Engine::runBlock() noexcept {
    installPending();
    drainCommands();

    const auto block = blocks_.load(std::memory_order_relaxed);
    const double msPerBlock = 1000.0 * config_.blockSize / config_.sampleRate;
    scheduler_.advanceTo(static_cast<double>(block + 1) * msPerBlock);

    std::ranges::fill(outBus_, 0.0f);
    if (dspOn_ && current_)
        runDsp();
    blocks_.store(block + 1, std::memory_order_relaxed);
}

void Engine::tick() noexcept {
    std::ranges::fill(inBus_, 0.0f);
    runBlock();
}

void Engine::process(const float* input, float* output, int frames) noexcept {
    const int bs = config_.blockSize;
    const int nin = config_.inputChannels;
    const int nout = config_.outputChannels;
    int done = 0;
    while (done < frames) {
        const int n = std::min(bs - ioPos_, frames - done);
        for (int f = 0; f < n; ++f) {
            for (int c = 0; c < nin; ++c)
                inBus_[static_cast<std::size_t>(c * bs + ioPos_ + f)] =
                    input ? input[static_cast<std::size_t>((done + f) * nin + c)] : 0.0f;
            for (int c = 0; c < nout; ++c)
                output[static_cast<std::size_t>((done + f) * nout + c)] =
                    outBus_[static_cast<std::size_t>(c * bs + ioPos_ + f)];
        }
        ioPos_ += n;
        done += n;
        if (ioPos_ == bs) {
            runBlock();
            ioPos_ = 0;
        }
    }
}

} // namespace qure
