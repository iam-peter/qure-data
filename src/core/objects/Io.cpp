// Audio I/O and named signal connections: [dac~] [adc~] [send~]
// [receive~] [throw~] [catch~].

#include "Common.hpp"

namespace qure::objects {
namespace {

/// Channel list from arguments (1-based), defaulting to 1 2.
std::vector<int> channelArgs(AtomSpan args) {
    std::vector<int> ch;
    for (const auto& a : args)
        ch.push_back(static_cast<int>(a.asFloat()) - 1);
    if (ch.empty())
        ch = {0, 1};
    return ch;
}

class Dac final : public Object {
public:
    explicit Dac(const ObjectArgs& a) : Object(a), channels_(channelArgs(a.args)) {
        for (std::size_t i = 0; i < channels_.size(); ++i)
            addSignalInlet();
    }

    void process(const DspIO& io) override {
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            auto bus = engine().outputBus(channels_[i]);
            if (bus.empty())
                continue;
            const auto in = io.in(static_cast<int>(i));
            for (int k = 0; k < io.frames(); ++k)
                bus[static_cast<std::size_t>(k)] += in[static_cast<std::size_t>(k)];
        }
    }

private:
    std::vector<int> channels_;
};

class Adc final : public Object {
public:
    explicit Adc(const ObjectArgs& a) : Object(a), channels_(channelArgs(a.args)) {
        for (std::size_t i = 0; i < channels_.size(); ++i)
            addSignalOutlet();
    }

    void process(const DspIO& io) override {
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            auto out = io.out(static_cast<int>(i));
            auto bus = engine().inputBus(channels_[i]);
            if (bus.empty())
                std::ranges::fill(out, 0.0f);
            else
                std::ranges::copy(bus.first(out.size()), out.begin());
        }
    }

private:
    std::vector<int> channels_;
};

// ---------------------------------------------------------------------------
// [send~ name] keeps the last block; [receive~ name] reads it. The compiler
// orders send~ before receive~ when possible, so there is no extra delay.

class SendSig final : public Object {
public:
    explicit SendSig(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)) {
        addSignalInlet();
        makeBinding(Binding::Kind::SignalSend, name_);
    }
    void prepare(const DspSetup& s) override { buffer_.assign(static_cast<std::size_t>(s.blockSize), 0.0f); }
    void process(const DspIO& io) override { std::ranges::copy(io.in(0), buffer_.begin()); }
    [[nodiscard]] DspLink dspLink() const override {
        return {DspLink::Side::Provides, Binding::Kind::SignalSend, name_};
    }
    [[nodiscard]] std::span<const float> buffer() const { return buffer_; }

private:
    Symbol name_;
    std::vector<float> buffer_;
};

class ReceiveSig final : public Object {
public:
    explicit ReceiveSig(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)) {
        addInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        if (auto* b = name_.find(Binding::Kind::SignalSend)) {
            const auto src = static_cast<SendSig*>(b->owner)->buffer();
            if (src.size() >= out.size()) {
                std::ranges::copy(src.first(out.size()), out.begin());
                return;
            }
        }
        std::ranges::fill(out, 0.0f);
    }
    [[nodiscard]] DspLink dspLink() const override {
        return {DspLink::Side::Consumes, Binding::Kind::SignalSend, name_};
    }
    [[nodiscard]] bool wantsDsp() const override { return true; }

protected:
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            name_ = argSymbol(args, 0);
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    Symbol name_;
};

// ---------------------------------------------------------------------------
// [catch~ name] sums everything [throw~ name] adds during the block.

class Catch final : public Object {
public:
    explicit Catch(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)) {
        addSignalOutlet();
        makeBinding(Binding::Kind::Catch, name_);
    }
    void prepare(const DspSetup& s) override { accum_.assign(static_cast<std::size_t>(s.blockSize), 0.0f); }
    void process(const DspIO& io) override {
        std::ranges::copy(accum_, io.out(0).begin());
        std::ranges::fill(accum_, 0.0f);
    }
    void add(std::span<const float> in) {
        const auto n = std::min(in.size(), accum_.size());
        for (std::size_t i = 0; i < n; ++i)
            accum_[i] += in[i];
    }
    [[nodiscard]] DspLink dspLink() const override { return {DspLink::Side::Consumes, Binding::Kind::Catch, name_}; }

private:
    Symbol name_;
    std::vector<float> accum_;
};

class Throw final : public Object {
public:
    explicit Throw(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)) { addSignalInlet(); }
    void process(const DspIO& io) override {
        if (auto* b = name_.find(Binding::Kind::Catch))
            static_cast<Catch*>(b->owner)->add(io.in(0));
    }
    [[nodiscard]] DspLink dspLink() const override { return {DspLink::Side::Provides, Binding::Kind::Catch, name_}; }

protected:
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            name_ = argSymbol(args, 0);
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    Symbol name_;
};

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerIoObjects(ObjectRegistry& r) {
    r.add<Dac>("dac~", "audio output");
    r.add<Adc>("adc~", "audio input");
    r.add<SendSig>("send~", "send a signal to a name", {"s~"});
    r.add<ReceiveSig>("receive~", "receive a signal from a name", {"r~"});
    r.add<Throw>("throw~", "add a signal to a [catch~] bus");
    r.add<Catch>("catch~", "sum of all [throw~] to this name");
}

} // namespace qure
