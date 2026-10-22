// Oscillators, arithmetic and analysis on signals:
// [osc~] [phasor~] [cos~] [noise~] [sig~] [line~] [+~ -~ *~ /~ max~ min~ pow~]
// [abs~ sqrt~ wrap~ exp~ mtof~ ftom~ dbtorms~ rmstodb~ dbtopow~ powtodb~]
// [clip~] [log~] [snapshot~] [env~] [scope~] [samplerate~] [bang~]

#include "Common.hpp"
#include "Snapshot.hpp"

#include <array>
#include <memory>

namespace qure::objects {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Cosine table with one guard point for linear interpolation.
class CosTable {
public:
    static constexpr int kSize = 2048;
    static const CosTable& instance() {
        static const CosTable t;
        return t;
    }
    /// cos(2π·phase) for phase in [0, 1).
    [[nodiscard]] float lookup(double phase) const noexcept {
        const double idx = phase * kSize;
        const int i = static_cast<int>(idx);
        const float frac = static_cast<float>(idx - i);
        return table_[static_cast<std::size_t>(i)] + frac * (table_[static_cast<std::size_t>(i) + 1] - table_[static_cast<std::size_t>(i)]);
    }

private:
    CosTable() {
        for (int i = 0; i <= kSize; ++i)
            table_[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(kTwoPi * i / kSize));
    }
    std::array<float, kSize + 1> table_{};
};

inline double wrap01(double x) noexcept { return x - std::floor(x); }

// ---------------------------------------------------------------------------

class Phasor : public Object {
public:
    explicit Phasor(const ObjectArgs& a) : Object(a) {
        addSignalInlet(static_cast<float>(argFloat(a.args, 0)));
        addInlet(); // phase
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { conv_ = 1.0 / s.sampleRate; }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1)
            phase_ = wrap01(f);
        else
            Object::onFloat(inlet, f);
    }

    double phase_ = 0, conv_ = 1.0 / 48000.0;
};

class PhasorSig final : public Phasor {
public:
    using Phasor::Phasor;
    void process(const DspIO& io) override {
        const auto freq = io.in(0);
        auto out = io.out(0);
        double p = phase_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<float>(p);
            p += freq[i] * conv_;
            p -= std::floor(p);
        }
        phase_ = p;
    }
};

class Osc final : public Phasor {
public:
    using Phasor::Phasor;
    void process(const DspIO& io) override {
        const auto& table = CosTable::instance();
        const auto freq = io.in(0);
        auto out = io.out(0);
        double p = phase_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = table.lookup(p);
            p += freq[i] * conv_;
            p -= std::floor(p);
        }
        phase_ = p;
    }
};

class Cos final : public Object {
public:
    explicit Cos(const ObjectArgs& a) : Object(a) {
        addSignalInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto& table = CosTable::instance();
        const auto in = io.in(0);
        auto out = io.out(0);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = table.lookup(wrap01(in[i]));
    }
};

class Noise final : public Object {
public:
    explicit Noise(const ObjectArgs& a) : Object(a) {
        static std::uint32_t seed = 307;
        state_ = (seed *= 1319);
        addInlet(); // "seed"
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        std::uint32_t v = state_;
        for (auto& s : out) {
            // Pd's generator, for bit-identical noise.
            s = static_cast<float>(static_cast<std::int32_t>((v & 0x7fffffffu) - 0x40000000u)) *
                static_cast<float>(1.0 / 0x40000000);
            v = v * 435898247u + 382842987u;
        }
        state_ = v;
    }

protected:
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector.str() == "seed")
            state_ = static_cast<std::uint32_t>(argFloat(args, 0));
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    std::uint32_t state_;
};

// ---------------------------------------------------------------------------

class Sig final : public Object {
public:
    explicit Sig(const ObjectArgs& a) : Object(a), value_(static_cast<float>(argFloat(a.args, 0))) {
        addInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override { std::ranges::fill(io.out(0), value_); }

protected:
    void onFloat(int, double f) override { value_ = static_cast<float>(f); }

private:
    float value_;
};

class Line final : public Object {
public:
    explicit Line(const ObjectArgs& a) : Object(a) {
        addInlet();
        addInlet();
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { samplesPerMs_ = s.sampleRate / 1000.0; }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        for (auto& s : out) {
            if (remaining_ > 0) {
                current_ += increment_;
                if (--remaining_ == 0)
                    current_ = target_;
            }
            s = static_cast<float>(current_);
        }
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            time_ = f;
            return;
        }
        const double samples = std::floor(time_ * samplesPerMs_);
        if (samples <= 0) {
            current_ = target_ = f;
            remaining_ = 0;
        } else {
            target_ = f;
            remaining_ = static_cast<long long>(samples);
            increment_ = (target_ - current_) / samples;
        }
        time_ = 0;
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector.str() == "stop") {
            remaining_ = 0;
            target_ = current_;
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    double current_ = 0, target_ = 0, increment_ = 0, time_ = 0;
    double samplesPerMs_ = 48.0;
    long long remaining_ = 0;
};

// ---------------------------------------------------------------------------
// Binary operators. With an argument the right inlet is a control inlet
// (scalar), like Pd's [*~ 0.5]; without, both inlets take signals.

template <class Op>
class Binary final : public Object {
public:
    explicit Binary(const ObjectArgs& a) : Object(a), scalarMode_(!a.args.empty()) {
        addSignalInlet();
        if (scalarMode_) {
            scalar_ = static_cast<float>(argFloat(a.args, 0));
            addInlet();
        } else {
            addSignalInlet();
        }
        addSignalOutlet();
    }

    void process(const DspIO& io) override {
        const float* __restrict a = io.in(0).data();
        float* __restrict out = io.out(0).data();
        const int n = io.frames();
        if (scalarMode_) {
            const float b = scalar_;
            for (int i = 0; i < n; ++i)
                out[i] = Op{}(a[i], b);
        } else {
            const float* __restrict b = io.in(1).data();
            for (int i = 0; i < n; ++i)
                out[i] = Op{}(a[i], b[i]);
        }
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1 && scalarMode_)
            scalar_ = static_cast<float>(f);
        else
            Object::onFloat(inlet, f);
    }

private:
    bool scalarMode_;
    float scalar_ = 0;
};

struct AddOp { float operator()(float a, float b) const noexcept { return a + b; } };
struct SubOp { float operator()(float a, float b) const noexcept { return a - b; } };
struct MulOp { float operator()(float a, float b) const noexcept { return a * b; } };
struct DivOp { float operator()(float a, float b) const noexcept { return b != 0.0f ? a / b : 0.0f; } };
struct MaxOp { float operator()(float a, float b) const noexcept { return a > b ? a : b; } };
struct MinOp { float operator()(float a, float b) const noexcept { return a < b ? a : b; } };
struct PowOp {
    float operator()(float a, float b) const noexcept {
        // Pd: negative bases give 0 unless the exponent is an integer.
        if (a > 0.0f || b == std::trunc(b))
            return std::pow(a, b);
        return 0.0f;
    }
};

// ---------------------------------------------------------------------------
// Unary operators.

template <class Op>
class Unary final : public Object {
public:
    explicit Unary(const ObjectArgs& a) : Object(a) {
        addSignalInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0);
        auto out = io.out(0);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = Op{}(in[i]);
    }
};

struct AbsOp { float operator()(float x) const noexcept { return std::fabs(x); } };
struct SqrtOp { float operator()(float x) const noexcept { return x > 0.0f ? std::sqrt(x) : 0.0f; } };
struct WrapOp { float operator()(float x) const noexcept { return x - std::floor(x); } };
struct ExpOp { float operator()(float x) const noexcept { return std::exp(x); } };
struct MtofOp {
    float operator()(float x) const noexcept {
        if (x <= -1500.0f)
            return 0.0f;
        return 8.17579891564f * std::exp(0.0577622650f * std::min(x, 1499.0f));
    }
};
struct FtomOp {
    float operator()(float x) const noexcept { return x > 0.0f ? 17.3123405046f * std::log(0.12231220585f * x) : -1500.0f; }
};
struct DbtormsOp {
    float operator()(float x) const noexcept {
        if (x <= 0.0f)
            return 0.0f;
        return std::exp(0.1151292546497f * (std::min(x, 485.0f) - 100.0f));
    }
};
struct RmstodbOp {
    float operator()(float x) const noexcept {
        if (x <= 0.0f)
            return 0.0f;
        return std::max(0.0f, 100.0f + 8.6858896380650f * std::log(x));
    }
};
struct DbtopowOp {
    float operator()(float x) const noexcept {
        if (x <= 0.0f)
            return 0.0f;
        return std::exp(0.2302585092994f * (std::min(x, 870.0f) - 100.0f));
    }
};
struct PowtodbOp {
    float operator()(float x) const noexcept {
        if (x <= 0.0f)
            return 0.0f;
        return std::max(0.0f, 100.0f + 4.3429448190325f * std::log(x));
    }
};

class Clip final : public Object {
public:
    explicit Clip(const ObjectArgs& a)
        : Object(a), lo_(static_cast<float>(argFloat(a.args, 0))), hi_(static_cast<float>(argFloat(a.args, 1))) {
        addSignalInlet();
        addInlet();
        addInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0);
        auto out = io.out(0);
        const float lo = std::min(lo_, hi_), hi = std::max(lo_, hi_);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = std::clamp(in[i], lo, hi);
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1)
            lo_ = static_cast<float>(f);
        else if (inlet == 2)
            hi_ = static_cast<float>(f);
        else
            Object::onFloat(inlet, f);
    }

private:
    float lo_, hi_;
};

class Log final : public Object {
public:
    explicit Log(const ObjectArgs& a) : Object(a) {
        addSignalInlet();
        addSignalInlet(static_cast<float>(argFloat(a.args, 0, std::numbers::e)));
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0), base = io.in(1);
        auto out = io.out(0);
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (in[i] <= 0.0f)
                out[i] = -1000.0f;
            else if (base[i] <= 0.0f || base[i] == 1.0f)
                out[i] = std::log(in[i]);
            else
                out[i] = std::log(in[i]) / std::log(base[i]);
        }
    }
};

// ---------------------------------------------------------------------------
// Analysis

class Snapshot final : public Object {
public:
    explicit Snapshot(const ObjectArgs& a) : Object(a) {
        addSignalInlet();
        addOutlet();
    }
    void process(const DspIO& io) override { last_ = io.in(0).back(); }

protected:
    void onBang(int) override { outFloat(0, last_); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == sym::set())
            last_ = static_cast<float>(argFloat(args, 0));
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    float last_ = 0;
};

/// [env~ window period]: RMS amplitude in dB (100 = unity), Hann windowed.
class Env final : public Object {
public:
    explicit Env(const ObjectArgs& a) : Object(a) {
        window_ = std::max(64, static_cast<int>(argFloat(a.args, 0, 1024)));
        period_ = std::max(1, static_cast<int>(argFloat(a.args, 1, window_ / 2.0)));
        addSignalInlet();
        addOutlet();
        clock_ = &makeClock([this] { outFloat(0, result_); });
    }
    void prepare(const DspSetup&) override {
        weights_.resize(static_cast<std::size_t>(window_));
        double sum = 0;
        for (int i = 0; i < window_; ++i) {
            weights_[static_cast<std::size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / window_));
            sum += weights_[static_cast<std::size_t>(i)];
        }
        for (auto& w : weights_)
            w = static_cast<float>(w / sum);
        history_.assign(static_cast<std::size_t>(window_), 0.0f);
    }
    void process(const DspIO& io) override {
        for (float x : io.in(0)) {
            history_[static_cast<std::size_t>(pos_)] = x * x;
            pos_ = (pos_ + 1) % window_;
            if (++count_ >= period_) {
                count_ = 0;
                double power = 0;
                for (int i = 0; i < window_; ++i)
                    power += weights_[static_cast<std::size_t>(i)] *
                             history_[static_cast<std::size_t>((pos_ + i) % window_)];
                result_ = power > 0 ? std::max(0.0, 100.0 + 4.3429448190325 * std::log(power)) : 0.0;
                clock_->delay(0);
            }
        }
    }

private:
    int window_, period_;
    int pos_ = 0, count_ = 0;
    double result_ = 0;
    std::vector<float> weights_, history_;
    Clock* clock_;
};

/// [scope~ samples]: publishes the most recent `samples` (default 512)
/// for a GUI oscilloscope. Not a Pd vanilla object.
class Scope final : public Object {
public:
    explicit Scope(const ObjectArgs& a) : Object(a) {
        size_ = std::clamp(static_cast<int>(argFloat(a.args, 0, 512)), 64, 8192);
        addSignalInlet();
        snapshot_ = std::make_unique<SnapshotBuffer>(static_cast<std::size_t>(size_));
        ring_.assign(static_cast<std::size_t>(size_), 0.0f);
        frame_.assign(static_cast<std::size_t>(size_), 0.0f);
    }
    void process(const DspIO& io) override {
        for (float x : io.in(0)) {
            ring_[static_cast<std::size_t>(pos_)] = x;
            pos_ = (pos_ + 1) % size_;
        }
        filled_ += io.frames();
        if (filled_ >= size_) { // publish one full window at a time
            filled_ = 0;
            for (int i = 0; i < size_; ++i)
                frame_[static_cast<std::size_t>(i)] = ring_[static_cast<std::size_t>((pos_ + i) % size_)];
            snapshot_->write(frame_);
        }
    }
    [[nodiscard]] const SnapshotBuffer* snapshot() const override { return snapshot_.get(); }

private:
    int size_, pos_ = 0, filled_ = 0;
    std::vector<float> ring_, frame_;
    std::unique_ptr<SnapshotBuffer> snapshot_;
};

class SampleRate final : public Object {
public:
    explicit SampleRate(const ObjectArgs& a) : Object(a) {
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int) override { outFloat(0, sampleRate()); }
};

/// [bang~]: bangs after every DSP block.
class BangSig final : public Object {
public:
    explicit BangSig(const ObjectArgs& a) : Object(a) {
        addInlet();
        addOutlet();
        clock_ = &makeClock([this] { outBang(0); });
    }
    [[nodiscard]] bool wantsDsp() const override { return true; }
    void process(const DspIO&) override { clock_->delay(0); }

private:
    Clock* clock_;
};

template <class T>
ObjectRegistry::Factory make() {
    return [](const ObjectArgs& a) -> std::unique_ptr<Object> { return std::make_unique<T>(a); };
}

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerSignalObjects(ObjectRegistry& r) {
    r.add<Osc>("osc~", "cosine oscillator");
    r.add<PhasorSig>("phasor~", "sawtooth 0..1");
    r.add<Cos>("cos~", "cosine of 2π·input");
    r.add<Noise>("noise~", "white noise");
    r.add<Sig>("sig~", "number to signal");
    r.add<Line>("line~", "linear ramp generator");

    r.add("+~", make<Binary<AddOp>>(), "add signals");
    r.add("-~", make<Binary<SubOp>>(), "subtract signals");
    r.add("*~", make<Binary<MulOp>>(), "multiply signals");
    r.add("/~", make<Binary<DivOp>>(), "divide signals");
    r.add("max~", make<Binary<MaxOp>>(), "maximum of signals");
    r.add("min~", make<Binary<MinOp>>(), "minimum of signals");
    r.add("pow~", make<Binary<PowOp>>(), "raise to a power");

    r.add("abs~", make<Unary<AbsOp>>(), "absolute value");
    r.add("sqrt~", make<Unary<SqrtOp>>(), "square root");
    r.add("wrap~", make<Unary<WrapOp>>(), "fractional part");
    r.add("exp~", make<Unary<ExpOp>>(), "e to the power");
    r.add("mtof~", make<Unary<MtofOp>>(), "MIDI note to frequency");
    r.add("ftom~", make<Unary<FtomOp>>(), "frequency to MIDI note");
    r.add("dbtorms~", make<Unary<DbtormsOp>>(), "dB to amplitude");
    r.add("rmstodb~", make<Unary<RmstodbOp>>(), "amplitude to dB");
    r.add("dbtopow~", make<Unary<DbtopowOp>>(), "dB to power");
    r.add("powtodb~", make<Unary<PowtodbOp>>(), "power to dB");
    r.add<Clip>("clip~", "restrict a signal to a range");
    r.add<Log>("log~", "logarithm");

    r.add<Snapshot>("snapshot~", "signal to number on bang");
    r.add<Env>("env~", "envelope follower (dB)");
    r.add<Scope>("scope~", "oscilloscope view");
    r.add<SampleRate>("samplerate~", "output the sample rate");
    r.add<BangSig>("bang~", "bang after each DSP block");
}

} // namespace qure
