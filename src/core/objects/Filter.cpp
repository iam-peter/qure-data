// Filters and signal control objects:
// [lop~] [hip~] [bp~] [vcf~] [biquad~] [rpole~] [rzero~] [rzero_rev~]
// [cpole~] [czero~] [czero_rev~] [slop~] [samphold~] [threshold~] [vline~]
// [lrshift~] and the delay lines [delwrite~] [delread~] [delread4~]/[vd~].
//
// Coefficient formulas follow Pd (compatibility level 0.44 and later), so
// patches sound the same.

#include "Common.hpp"

#include <bit>
#include <cstdint>

namespace qure::objects {
namespace {

// Pd computes its coefficients with this approximation of 2π.
constexpr float kPdTwoPi = 2.0f * 3.14159f;

/// Pd's PD_BIGORSMALL: true for denormals (and zeros) and huge values.
inline bool bigOrSmall(float f) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(f) & 0x60000000u;
    return bits == 0 || bits == 0x60000000u;
}
inline float flushed(float f) noexcept { return bigOrSmall(f) ? 0.0f : f; }

/// Selectors compared against on the engine thread, interned once.
struct Names {
    Symbol clear{"clear"}, set{"set"}, reset{"reset"}, stop{"stop"};
};
const Names& names() {
    static const Names n;
    return n;
}

/// Base for filters: makes sure the selector table exists (constructor
/// runs on the control thread) and handles "clear".
class FilterBase : public Object {
public:
    explicit FilterBase(const ObjectArgs& a) : Object(a), names_(names()) {}

protected:
    virtual void clear() {}
    /// "set" with its arguments; return false if the object has none.
    virtual bool set(AtomSpan) { return false; }

    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == names_.clear)
            clear();
        else if (selector == names_.set && set(args))
            return;
        else
            Object::onAnything(inlet, selector, args);
    }

    const Names& names_;
};

// ---------------------------------------------------------------------------
// [lop~ hz]: one-pole low pass; the cutoff inlet takes a signal.

class Lop final : public FilterBase {
public:
    explicit Lop(const ObjectArgs& a) : FilterBase(a) {
        addSignalInlet();
        addSignalInlet(static_cast<float>(argFloat(a.args, 0)));
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { conv_ = kPdTwoPi / static_cast<float>(s.sampleRate); }
    void process(const DspIO& io) override {
        const auto in = io.in(0), hz = io.in(1);
        auto out = io.out(0);
        float last = last_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float coef = std::clamp(hz[i] * conv_, 0.0f, 1.0f);
            last = out[i] = coef * in[i] + (1.0f - coef) * last;
        }
        last_ = flushed(last);
    }

protected:
    void clear() override { last_ = 0; }

private:
    float conv_ = kPdTwoPi / 48000.0f;
    float last_ = 0;
};

// [hip~ hz]: one-pole one-zero high pass (Pd 0.44+ normalised form).

class Hip final : public FilterBase {
public:
    explicit Hip(const ObjectArgs& a) : FilterBase(a), hz_(argFloat(a.args, 0)) {
        addSignalInlet();
        addInlet();
        addSignalOutlet();
        update();
    }
    void prepare(const DspSetup& s) override {
        sr_ = s.sampleRate;
        update();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0);
        auto out = io.out(0);
        if (coef_ < 1.0f) {
            const float normal = 0.5f * (1.0f + coef_);
            float last = last_;
            for (std::size_t i = 0; i < out.size(); ++i) {
                const float next = in[i] + coef_ * last;
                out[i] = normal * (next - last);
                last = next;
            }
            last_ = flushed(last);
        } else {
            std::ranges::copy(in, out.begin());
            last_ = 0;
        }
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            hz_ = f;
            update();
        } else {
            Object::onFloat(inlet, f);
        }
    }
    void clear() override { last_ = 0; }

private:
    void update() {
        hz_ = std::max(hz_, 0.0);
        coef_ = std::clamp(static_cast<float>(1.0 - hz_ * kPdTwoPi / sr_), 0.0f, 1.0f);
    }

    double hz_, sr_ = 48000.0;
    float coef_ = 1, last_ = 0;
};

// [bp~ hz q]: two-pole band pass.

class Bp final : public FilterBase {
public:
    explicit Bp(const ObjectArgs& a) : FilterBase(a), freq_(argFloat(a.args, 0)), q_(argFloat(a.args, 1)) {
        addSignalInlet();
        addInlet();
        addInlet();
        addSignalOutlet();
        update();
    }
    void prepare(const DspSetup& s) override {
        sr_ = static_cast<float>(s.sampleRate);
        update();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0);
        auto out = io.out(0);
        float last = x1_, prev = x2_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float output = in[i] + coef1_ * last + coef2_ * prev;
            out[i] = gain_ * output;
            prev = last;
            last = output;
        }
        x1_ = flushed(last);
        x2_ = flushed(prev);
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1)
            freq_ = f;
        else if (inlet == 2)
            q_ = f;
        else
            return Object::onFloat(inlet, f);
        update();
    }
    void clear() override { x1_ = x2_ = 0; }

private:
    static float qcos(float f) {
        if (f >= -(0.5f * 3.14159f) && f <= 0.5f * 3.14159f) {
            const float g = f * f;
            return ((g * g * g * (-1.0f / 720.0f) + g * g * (1.0f / 24.0f)) - g * 0.5f) + 1.0f;
        }
        return 0.0f;
    }
    void update() {
        if (freq_ < 0.001)
            freq_ = 10;
        if (q_ < 0)
            q_ = 0;
        const float omega = static_cast<float>(freq_) * kPdTwoPi / sr_;
        float oneminusr = q_ < 0.001 ? 1.0f : omega / static_cast<float>(q_);
        oneminusr = std::min(oneminusr, 1.0f);
        const float r = 1.0f - oneminusr;
        coef1_ = 2.0f * qcos(omega) * r;
        coef2_ = -r * r;
        gain_ = 2.0f * oneminusr * (oneminusr + r * omega);
    }

    double freq_, q_;
    float sr_ = 48000.0f;
    float coef1_ = 0, coef2_ = 0, gain_ = 0, x1_ = 0, x2_ = 0;
};

// [vcf~ q]: resonant band pass / low pass with a signal center frequency.
// Outputs: band pass (real part), low pass (imaginary part).

class VcfCosTable {
public:
    static constexpr int kSize = 2048;
    static const VcfCosTable& instance() {
        static const VcfCosTable t;
        return t;
    }
    [[nodiscard]] float at(int i) const noexcept { return table_[static_cast<std::size_t>(i)]; }

private:
    VcfCosTable() {
        for (int i = 0; i <= kSize; ++i)
            table_[static_cast<std::size_t>(i)] =
                static_cast<float>(std::cos(2.0 * std::numbers::pi * i / kSize));
    }
    std::array<float, kSize + 1> table_{};
};

class Vcf final : public FilterBase {
public:
    explicit Vcf(const ObjectArgs& a) : FilterBase(a), table_(VcfCosTable::instance()) {
        q_ = static_cast<float>(argFloat(a.args, 0));
        addSignalInlet();
        addSignalInlet();
        addInlet();
        addSignalOutlet();
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { isr_ = 6.28318f / static_cast<float>(s.sampleRate); }
    void process(const DspIO& io) override {
        constexpr int N = VcfCosTable::kSize;
        const auto in = io.in(0), freq = io.in(1);
        auto out1 = io.out(0), out2 = io.out(1);
        float re = re_, im = im_;
        const float qinv = q_ > 0 ? 1.0f / q_ : 0.0f;
        const float ampcorrect = 2.0f - 2.0f / (q_ + 2.0f);
        for (std::size_t i = 0; i < out1.size(); ++i) {
            float cf = freq[i] * isr_;
            if (!(cf >= 0))
                cf = 0;
            double index = static_cast<double>(cf * (static_cast<float>(N) / 6.28318f));
            index -= N * std::floor(index / N);
            const int whole = std::min(static_cast<int>(index), N - 1);
            const float frac = static_cast<float>(index - whole);
            float r = qinv > 0 ? 1.0f - cf * qinv : 0.0f;
            r = std::max(r, 0.0f);
            const float oneminusr = 1.0f - r;
            const int ci = whole;
            const int si = (whole - N / 4) & (N - 1);
            const float coefr = r * (table_.at(ci) + frac * (table_.at(ci + 1) - table_.at(ci)));
            const float coefi = r * (table_.at(si) + frac * (table_.at(si + 1) - table_.at(si)));
            const float re2 = re;
            re = out1[i] = ampcorrect * oneminusr * in[i] + coefr * re2 - coefi * im;
            im = out2[i] = coefi * re2 + coefr * im;
        }
        re_ = flushed(re);
        im_ = flushed(im);
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 2)
            q_ = static_cast<float>(std::clamp(f, 0.0, 1e18));
        else
            Object::onFloat(inlet, f);
    }
    void clear() override { re_ = im_ = 0; }

private:
    const VcfCosTable& table_;
    float q_, isr_ = 6.28318f / 48000.0f;
    float re_ = 0, im_ = 0;
};

// [biquad~ fb1 fb2 ff1 ff2 ff3]: raw biquad (direct form II).

class Biquad final : public FilterBase {
public:
    explicit Biquad(const ObjectArgs& a) : FilterBase(a) {
        addSignalInlet();
        addSignalOutlet();
        setCoefficients(a.args);
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0);
        auto out = io.out(0);
        float last = x1_, prev = x2_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float output = flushed(in[i] + fb1_ * last + fb2_ * prev);
            out[i] = ff1_ * output + ff2_ * last + ff3_ * prev;
            prev = last;
            last = output;
        }
        x1_ = last;
        x2_ = prev;
    }

protected:
    void onList(int inlet, AtomSpan args) override {
        if (inlet == 0)
            setCoefficients(args);
        else
            Object::onList(inlet, args);
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 0) {
            const Atom a(f);
            setCoefficients(AtomSpan(&a, 1));
        } else {
            Object::onFloat(inlet, f);
        }
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        // Pd: "clear" takes the same optional state arguments as "set".
        if (selector == names_.clear || selector == names_.set) {
            x1_ = static_cast<float>(argFloat(args, 0));
            x2_ = static_cast<float>(argFloat(args, 1));
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    void setCoefficients(AtomSpan args) {
        float fb1 = static_cast<float>(argFloat(args, 0));
        float fb2 = static_cast<float>(argFloat(args, 1));
        float ff1 = static_cast<float>(argFloat(args, 2));
        float ff2 = static_cast<float>(argFloat(args, 3));
        float ff3 = static_cast<float>(argFloat(args, 4));
        const float discriminant = fb1 * fb1 + 4 * fb2;
        bool stable;
        if (discriminant < 0) // complex conjugate poles: |p|² = -fb2
            stable = fb2 >= -1.0f;
        else // real poles: both inside [-1, 1]
            stable = fb1 <= 2.0f && fb1 >= -2.0f && 1.0f - fb1 - fb2 >= 0 && 1.0f + fb1 - fb2 >= 0;
        if (!stable)
            fb1 = fb2 = ff1 = ff2 = ff3 = 0;
        fb1_ = fb1;
        fb2_ = fb2;
        ff1_ = ff1;
        ff2_ = ff2;
        ff3_ = ff3;
    }

    float fb1_ = 0, fb2_ = 0, ff1_ = 0, ff2_ = 0, ff3_ = 0;
    float x1_ = 0, x2_ = 0;
};

// ---------------------------------------------------------------------------
// Raw one-pole / one-zero filters with signal coefficients.

enum class RawKind { Pole, Zero, ZeroRev };

template <RawKind K>
class RawFilter final : public FilterBase {
public:
    explicit RawFilter(const ObjectArgs& a) : FilterBase(a) {
        addSignalInlet();
        addSignalInlet(static_cast<float>(argFloat(a.args, 0)));
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0), coef = io.in(1);
        auto out = io.out(0);
        float last = last_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float next = in[i];
            if constexpr (K == RawKind::Pole) {
                out[i] = last = coef[i] * last + next;
            } else if constexpr (K == RawKind::Zero) {
                out[i] = next - coef[i] * last;
                last = next;
            } else {
                out[i] = last - coef[i] * next;
                last = next;
            }
        }
        if constexpr (K == RawKind::Pole)
            last = flushed(last);
        last_ = last;
    }

protected:
    void clear() override { last_ = 0; }
    bool set(AtomSpan args) override {
        last_ = static_cast<float>(argFloat(args, 0));
        return true;
    }

private:
    float last_ = 0;
};

/// Complex one-pole / one-zero filters: inlets re, im, coef re, coef im.
template <RawKind K>
class ComplexFilter final : public FilterBase {
public:
    explicit ComplexFilter(const ObjectArgs& a) : FilterBase(a) {
        addSignalInlet();
        addSignalInlet();
        addSignalInlet(static_cast<float>(argFloat(a.args, 0)));
        addSignalInlet(static_cast<float>(argFloat(a.args, 1)));
        addSignalOutlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto inre = io.in(0), inim = io.in(1), cre = io.in(2), cim = io.in(3);
        auto outre = io.out(0), outim = io.out(1);
        float lastre = lastre_, lastim = lastim_;
        for (std::size_t i = 0; i < outre.size(); ++i) {
            const float nextre = inre[i], nextim = inim[i], coefre = cre[i], coefim = cim[i];
            if constexpr (K == RawKind::Pole) {
                const float tempre = outre[i] = nextre + lastre * coefre - lastim * coefim;
                lastim = outim[i] = nextim + lastre * coefim + lastim * coefre;
                lastre = tempre;
            } else if constexpr (K == RawKind::Zero) {
                outre[i] = nextre - lastre * coefre + lastim * coefim;
                outim[i] = nextim - lastre * coefim - lastim * coefre;
                lastre = nextre;
                lastim = nextim;
            } else {
                // (A bar) - z^-1: same magnitude response as 1 - A z^-1.
                outre[i] = lastre - nextre * coefre - nextim * coefim;
                outim[i] = lastim - nextre * coefim + nextim * coefre;
                lastre = nextre;
                lastim = nextim;
            }
        }
        if constexpr (K == RawKind::Pole) {
            lastre = flushed(lastre);
            lastim = flushed(lastim);
        }
        lastre_ = lastre;
        lastim_ = lastim;
    }

protected:
    void clear() override { lastre_ = lastim_ = 0; }
    bool set(AtomSpan args) override {
        lastre_ = static_cast<float>(argFloat(args, 0));
        lastim_ = static_cast<float>(argFloat(args, 1));
        return true;
    }

private:
    float lastre_ = 0, lastim_ = 0;
};

// [slop~ hz neglimit negfreq poslimit posfreq]: slew-limiting low pass.

class Slop final : public FilterBase {
public:
    explicit Slop(const ObjectArgs& a) : FilterBase(a) {
        addSignalInlet();
        for (std::size_t i = 0; i < 5; ++i)
            addSignalInlet(static_cast<float>(argFloat(a.args, i)));
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { coef_ = kPdTwoPi / static_cast<float>(s.sampleRate); }
    void process(const DspIO& io) override {
        const auto sigin = io.in(0), freqin = io.in(1), neglimit = io.in(2), negfreqin = io.in(3),
                   poslimit = io.in(4), posfreqin = io.in(5);
        auto out = io.out(0);
        float last = last_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float diff = sigin[i] - last;
            const float inc = std::clamp(freqin[i] * coef_, 0.0f, 1.0f);
            const float posinc = std::clamp(posfreqin[i] * coef_, 0.0f, 1.0f);
            const float neginc = std::clamp(negfreqin[i] * coef_, 0.0f, 1.0f);
            const float maxdiff = std::max(poslimit[i], 0.0f);
            const float mindiff = std::max(neglimit[i], 0.0f);
            float diffinc;
            if (diff > maxdiff)
                diffinc = posinc * (diff - maxdiff) + inc * maxdiff;
            else if (diff < -mindiff)
                diffinc = neginc * (diff + mindiff) - inc * mindiff;
            else
                diffinc = inc * diff;
            last = out[i] = last + diffinc;
        }
        last_ = flushed(last);
    }

protected:
    bool set(AtomSpan args) override {
        last_ = static_cast<float>(argFloat(args, 0));
        return true;
    }

private:
    float coef_ = kPdTwoPi / 48000.0f;
    float last_ = 0;
};

// ---------------------------------------------------------------------------
// [samphold~]: samples the left input whenever the right input decreases.

class SampHold final : public FilterBase {
public:
    explicit SampHold(const ObjectArgs& a) : FilterBase(a) {
        addSignalInlet();
        addSignalInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0), ctl = io.in(1);
        auto out = io.out(0);
        float lastin = lastin_, lastout = lastout_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float next = ctl[i];
            if (next < lastin)
                lastout = in[i];
            out[i] = lastout;
            lastin = next;
        }
        lastin_ = lastin;
        lastout_ = lastout;
    }

protected:
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == names_.reset)
            lastin_ = !args.empty() && args[0].isFloat() ? static_cast<float>(args[0].asFloat()) : 1e20f;
        else if (selector == names_.set)
            lastout_ = static_cast<float>(argFloat(args, 0));
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    float lastin_ = 0, lastout_ = 0;
};

// [threshold~ hi hidead lo lodead]: bangs left when the input rises to
// `hi`, right when it falls below `lo`, with dead times in between.

class Threshold final : public FilterBase {
public:
    explicit Threshold(const ObjectArgs& a) : FilterBase(a) {
        addSignalInlet();
        addInlet();
        addOutlet();
        addOutlet();
        clock_ = &makeClock([this] { outBang(state_ ? 0 : 1); });
        setThresholds(argFloat(a.args, 0), argFloat(a.args, 1), argFloat(a.args, 2), argFloat(a.args, 3));
    }
    void prepare(const DspSetup& s) override { msPerBlock_ = 1000.0 * s.blockSize / s.sampleRate; }
    void process(const DspIO& io) override {
        if (deadwait_ > 0) {
            deadwait_ -= msPerBlock_;
            return;
        }
        for (float x : io.in(0)) {
            if (state_ ? x < lo_ : x >= hi_) {
                state_ = !state_;
                deadwait_ = state_ ? hidead_ : lodead_;
                clock_->delay(0);
                return;
            }
        }
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            state_ = f != 0;
            deadwait_ = 0;
        } else {
            Object::onFloat(inlet, f);
        }
    }
    bool set(AtomSpan args) override {
        setThresholds(argFloat(args, 0), argFloat(args, 1), argFloat(args, 2), argFloat(args, 3));
        return true;
    }

private:
    void setThresholds(double hi, double hidead, double lo, double lodead) {
        hi_ = static_cast<float>(hi);
        lo_ = static_cast<float>(std::min(lo, hi));
        hidead_ = hidead;
        lodead_ = lodead;
    }

    Clock* clock_;
    bool state_ = false;
    float hi_ = 0, lo_ = 0;
    double hidead_ = 0, lodead_ = 0, deadwait_ = 0, msPerBlock_ = 64000.0 / 48000.0;
};

// ---------------------------------------------------------------------------
// [vline~]: sample-accurate line segments. A float starts a ramp to that
// value; the middle inlet sets its duration and the right inlet a delay
// (both in ms, reset after each segment). Segments are kept in time order
// in a vector consumed from the front; capacity is reserved up front.

class VLine final : public FilterBase {
public:
    explicit VLine(const ObjectArgs& a) : FilterBase(a) {
        addInlet();
        addInlet();
        addInlet();
        addSignalOutlet();
        segments_.reserve(kReserve);
    }
    void prepare(const DspSetup& s) override { msPerSample_ = 1000.0 / s.sampleRate; }

    void process(const DspIO& io) override {
        auto out = io.out(0);
        const double msps = msPerSample_;
        double f = value_, inc = inc_;
        // The engine processes a block after advancing logical time to its
        // end, so the block covers [now - n·msps, now).
        double timenow = now() - static_cast<double>(out.size()) * msps;
        for (auto& sample : out) {
            const double timenext = timenow + msps;
            while (head_ < segments_.size() && segments_[head_].start < timenext) {
                const Segment& s = segments_[head_];
                if (stoptime_ <= timenext) {
                    f = target_;
                    inc = 0;
                }
                if (s.stop <= s.start) { // zero length: jump
                    f = s.target;
                    inc = 0;
                } else {
                    const double incPerMs = (s.target - f) / (s.stop - s.start);
                    f = f + incPerMs * (timenext - s.start);
                    inc = incPerMs * msps;
                }
                inc_ = inc;
                target_ = s.target;
                stoptime_ = s.stop;
                popFront();
            }
            if (stoptime_ <= timenext) {
                f = target_;
                inc = inc_ = 0;
                stoptime_ = 1e300;
            }
            sample = static_cast<float>(f);
            f += inc;
            timenow = timenext;
        }
        value_ = f;
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            time_ = f;
            return;
        }
        if (inlet == 2) {
            delay_ = f;
            return;
        }
        if (inlet != 0)
            return Object::onFloat(inlet, f);
        addSegment(std::isfinite(f) ? f : 0.0);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == names_.stop)
            stop();
        else
            Object::onAnything(inlet, selector, args);
    }

private:
    struct Segment {
        double start, stop, target;
    };
    static constexpr std::size_t kReserve = 64;

    [[nodiscard]] std::size_t count() const noexcept { return segments_.size() - head_; }
    void popFront() noexcept {
        if (++head_ == segments_.size()) {
            segments_.clear();
            head_ = 0;
        }
    }
    /// Removes consumed slots so appending does not grow the vector.
    void compact() {
        if (head_ == 0)
            return;
        segments_.erase(segments_.begin(), segments_.begin() + static_cast<std::ptrdiff_t>(head_));
        head_ = 0;
    }

    void stop() {
        segments_.clear();
        head_ = 0;
        inc_ = 0;
        time_ = delay_ = 0;
        target_ = value_;
        stoptime_ = 1e300;
    }

    void addSegment(double target) {
        const double timenow = now();
        const double duration = std::max(time_, 0.0);
        const double delay = delay_;
        const double start = timenow + delay;

        // With DSP off nothing consumes segments; drop the ones already
        // over so they cannot pile up.
        if (!engine().dspRunning())
            while (count() > 0 && timenow > segments_[head_].stop + 0.1)
                popFront();

        if (delay < 0) { // negative delay: stop and jump right away
            value_ = target;
            stop();
            return;
        }

        // Pd's rules: append after a segment that starts earlier (or at the
        // same time, if that one is a jump and this one a ramp); otherwise
        // the new segment replaces everything from the first segment that
        // starts later (or at the same time, unless that is a jump and this
        // is a ramp).
        std::size_t at = segments_.size();
        if (count() > 0) {
            const Segment& tail = segments_.back();
            const bool append = tail.start < start || (tail.start == start && tail.stop <= tail.start && duration > 0);
            if (!append) {
                for (at = head_; at < segments_.size(); ++at) {
                    const Segment& s = segments_[at];
                    if (s.start > start || (s.start == start && (s.stop > s.start || duration <= 0)))
                        break;
                }
            }
        }
        segments_.resize(at);
        if (segments_.size() == segments_.capacity())
            compact();
        segments_.push_back({start, start + duration, target});
        time_ = delay_ = 0;
    }

    std::vector<Segment> segments_;
    std::size_t head_ = 0;
    double value_ = 0, inc_ = 0, target_ = 0, stoptime_ = 1e300;
    double time_ = 0, delay_ = 0;
    double msPerSample_ = 1000.0 / 48000.0;
};

// [lrshift~ n]: shifts each block by n samples, left (earlier) for
// positive n, right for negative, filling with zeros.

class LrShift final : public Object {
public:
    explicit LrShift(const ObjectArgs& a) : Object(a), shift_(static_cast<int>(argFloat(a.args, 0))) {
        addSignalInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        const auto in = io.in(0);
        auto out = io.out(0);
        const int n = io.frames();
        const int shift = std::clamp(shift_, -n, n);
        for (int i = 0; i < n; ++i) {
            const int src = i + shift;
            out[static_cast<std::size_t>(i)] = src >= 0 && src < n ? in[static_cast<std::size_t>(src)] : 0.0f;
        }
    }

private:
    int shift_;
};

// ---------------------------------------------------------------------------
// Delay lines. The buffer layout follows Pd: `kExtra` guard samples at the
// front repeat the last ones of the ring so the 4-point interpolation of
// [delread4~] never has to wrap.

constexpr int kExtra = 4;

class DelWrite final : public FilterBase {
public:
    explicit DelWrite(const ObjectArgs& a) : FilterBase(a), ms_(argFloat(a.args, 1)) {
        Symbol name = argSymbol(a.args, 0);
        if (name.empty())
            name = Symbol("delwrite~");
        addSignalInlet();
        makeBinding(Binding::Kind::DelayLine, name);
    }

    void prepare(const DspSetup& s) override {
        int nsamps = static_cast<int>(ms_ * s.sampleRate * 0.001);
        nsamps = std::max(nsamps, 1);
        nsamps += (-nsamps) & 3; // round up to a multiple of 4
        nsamps += s.blockSize;
        size_ = nsamps;
        buffer_.assign(static_cast<std::size_t>(nsamps + kExtra), 0.0f);
        phase_ = kExtra;
        lastBlock_ = ~std::uint64_t{0};
    }

    void process(const DspIO& io) override {
        float* vp = buffer_.data();
        float* const ep = vp + size_ + kExtra;
        float* bp = vp + phase_;
        int phase = phase_ + io.frames();
        for (float x : io.in(0)) {
            *bp++ = flushed(x);
            if (bp == ep) {
                vp[0] = ep[-4];
                vp[1] = ep[-3];
                vp[2] = ep[-2];
                vp[3] = ep[-1];
                bp = vp + kExtra;
                phase -= size_;
            }
        }
        phase_ = phase;
        lastBlock_ = engine().blocksProcessed();
    }

    /// Ring length in samples (without the guard points).
    [[nodiscard]] int size() const noexcept { return size_; }
    /// Write position (index into buffer(), in [kExtra, size + kExtra)).
    [[nodiscard]] int phase() const noexcept { return phase_; }
    [[nodiscard]] const float* buffer() const noexcept { return buffer_.data(); }
    /// True when this writer already ran in the current block, i.e. a
    /// reader scheduled now sees this block's input.
    [[nodiscard]] bool wroteThisBlock() const noexcept { return lastBlock_ == engine().blocksProcessed(); }

protected:
    void clear() override { std::ranges::fill(buffer_, 0.0f); }

private:
    double ms_;
    std::vector<float> buffer_;
    int size_ = 0;
    int phase_ = kExtra;
    std::uint64_t lastBlock_ = ~std::uint64_t{0};
};

DelWrite* findDelay(Symbol name) {
    if (auto* b = name.find(Binding::Kind::DelayLine))
        return static_cast<DelWrite*>(b->owner);
    return nullptr;
}

/// [delread~ name ms]: whole-sample delay. Reading before the writer in a
/// block gives at least one block of delay, like Pd.
class DelRead final : public Object {
public:
    explicit DelRead(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)), ms_(argFloat(a.args, 1)) {
        addInlet();
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { samplesPerMs_ = s.sampleRate * 0.001; }

    void process(const DspIO& io) override {
        auto out = io.out(0);
        const DelWrite* w = findDelay(name_);
        const int n = io.frames();
        if (!w || w->size() < n) {
            std::ranges::fill(out, 0.0f);
            return;
        }
        const int zerodel = w->wroteThisBlock() ? 0 : n;
        const int nsamps = w->size();
        int delsamps = static_cast<int>(0.5 + samplesPerMs_ * ms_) + n - zerodel;
        delsamps = std::clamp(delsamps, n, nsamps);
        int phase = w->phase() - delsamps;
        if (phase < 0)
            phase += nsamps;
        const float* vp = w->buffer();
        const float* ep = vp + nsamps + kExtra;
        const float* bp = vp + phase;
        for (auto& s : out) {
            s = *bp++;
            if (bp == ep)
                bp -= nsamps;
        }
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 0)
            ms_ = f;
        else
            Object::onFloat(inlet, f);
    }

private:
    Symbol name_;
    double ms_;
    double samplesPerMs_ = 48.0;
};

/// [delread4~ name] / [vd~ name]: signal-controlled delay time (ms) with
/// Pd's 4-point interpolation.
class DelRead4 final : public Object {
public:
    explicit DelRead4(const ObjectArgs& a) : Object(a), name_(argSymbol(a.args, 0)) {
        addSignalInlet();
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { samplesPerMs_ = static_cast<float>(s.sampleRate * 0.001); }

    void process(const DspIO& io) override {
        const auto in = io.in(0);
        auto out = io.out(0);
        const DelWrite* w = findDelay(name_);
        const int n = io.frames();
        const int nsamps = w ? w->size() : 0;
        const float limit = static_cast<float>(nsamps - n);
        if (!w || limit < 0) {
            std::ranges::fill(out, 0.0f);
            return;
        }
        const float zerodel = w->wroteThisBlock() ? 0.0f : static_cast<float>(n);
        const float* vp = w->buffer();
        const float* wp = vp + w->phase();
        float fn = static_cast<float>(n - 1);
        for (std::size_t i = 0; i < out.size(); ++i) {
            float delsamps = samplesPerMs_ * in[i] - zerodel;
            if (!(delsamps >= 1.00001f)) // too small or NaN
                delsamps = 1.00001f;
            if (delsamps > limit)
                delsamps = limit;
            delsamps += fn;
            fn -= 1.0f;
            const int idelsamps = static_cast<int>(delsamps);
            const float frac = delsamps - static_cast<float>(idelsamps);
            const float* bp = wp - idelsamps;
            if (bp < vp + kExtra)
                bp += nsamps;
            const float d = bp[-3], c = bp[-2], b = bp[-1], a = bp[0];
            const float cminusb = c - b;
            out[i] = b + frac * (cminusb - 0.1666667f * (1.0f - frac) *
                                               ((d - a - 3.0f * cminusb) * frac + (d + 2.0f * a - 3.0f * b)));
        }
    }

private:
    Symbol name_;
    float samplesPerMs_ = 48.0f;
};

template <class T>
ObjectRegistry::Factory make() {
    return [](const ObjectArgs& a) -> std::unique_ptr<Object> { return std::make_unique<T>(a); };
}

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerFilterObjects(ObjectRegistry& r) {
    r.add<Lop>("lop~", "one-pole low pass filter");
    r.add<Hip>("hip~", "one-pole high pass filter");
    r.add<Bp>("bp~", "band pass filter (frequency, Q)");
    r.add<Vcf>("vcf~", "voltage-controlled band/low pass filter");
    r.add<Biquad>("biquad~", "raw two-pole two-zero filter");
    r.add("rpole~", make<RawFilter<RawKind::Pole>>(), "raw real one-pole filter");
    r.add("rzero~", make<RawFilter<RawKind::Zero>>(), "raw real one-zero filter");
    r.add("rzero_rev~", make<RawFilter<RawKind::ZeroRev>>(), "raw real one-zero filter, reversed");
    r.add("cpole~", make<ComplexFilter<RawKind::Pole>>(), "raw complex one-pole filter");
    r.add("czero~", make<ComplexFilter<RawKind::Zero>>(), "raw complex one-zero filter");
    r.add("czero_rev~", make<ComplexFilter<RawKind::ZeroRev>>(), "raw complex one-zero filter, reversed");
    r.add<Slop>("slop~", "slew-limiting low pass filter");
    r.add<SampHold>("samphold~", "sample and hold");
    r.add<Threshold>("threshold~", "trigger bangs from a signal");
    r.add<VLine>("vline~", "sample-accurate line generator");
    r.add<LrShift>("lrshift~", "shift a block left or right");
    r.add<DelWrite>("delwrite~", "write to a delay line");
    r.add<DelRead>("delread~", "read from a delay line");
    r.add<DelRead4>("delread4~", "variable delay with interpolation", {"vd~"});
}

} // namespace qure
