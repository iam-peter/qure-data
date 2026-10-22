// Named float arrays and the objects that use them:
// graph arrays (`#X array`, class "@array"), [table], [array define/size/
// get/set/sum/max/min], [tabread] [tabread4] [tabwrite], [tabread~]
// [tabread4~] [tabwrite~] [tabplay~] [tabosc4~] [tabsend~] [tabreceive~].
//
// A table binds its name twice: as Binding::Kind::Table so users can find
// its storage, and as a Receiver so `; name 0 1 2` style messages reach it.
// Users look the table up by name every time they need it (every block for
// signal objects), since tables can appear, disappear and be resized.

#include "Common.hpp"
#include "Snapshot.hpp"
#include "Table.hpp"

#include <atomic>
#include <bit>
#include <climits>
#include <cstdint>
#include <memory>
#include <thread>

namespace qure::objects {
namespace {

/// Selectors compared against on the engine thread, interned once (the
/// first constructor runs on the control thread).
struct Names {
    Symbol set{"set"}, start{"start"}, stop{"stop"};
    Symbol resize{"resize"}, constant{"const"}, normalize{"normalize"};
    Symbol sinesum{"sinesum"}, cosinesum{"cosinesum"}, keep{"keep"}, rename{"rename"}, print{"print"};
    // Graph appearance messages: no effect on the data, passed to the GUI.
    std::array<Symbol, 11> appearance{Symbol("bounds"), Symbol("xticks"), Symbol("yticks"), Symbol("xlabel"),
                                      Symbol("ylabel"),  Symbol("style"),  Symbol("width"),  Symbol("color"),
                                      Symbol("vis"),     Symbol("edit"),   Symbol("visname")};
};
const Names& names() {
    static const Names n;
    return n;
}

inline bool bigOrSmall(float f) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(f) & 0x60000000u;
    return bits == 0 || bits == 0x60000000u;
}

/// The table bound to `name`, or nullptr.
TableStorage* findTable(Symbol name) noexcept {
    if (name.empty())
        return nullptr;
    if (auto* b = name.find(Binding::Kind::Table))
        return dynamic_cast<TableStorage*>(b->owner);
    return nullptr;
}

/// Pd's 4-point interpolation around b = p[0] (needs p[-1] .. p[2]).
inline float interpolate4(const float* p, float frac) noexcept {
    const float a = p[-1], b = p[0], c = p[1], d = p[2];
    const float cminusb = c - b;
    return b + frac * (cminusb - (1.0f / 6.0f) * (1.0f - frac) *
                                     ((d - a - 3.0f * cminusb) * frac + (d + 2.0f * a - 3.0f * b)));
}

// ===========================================================================
// The table itself.

class Table final : public Object, public TableStorage {
public:
    enum class Flavor {
        Graph,  ///< `#X array name size type flags` inside a graph
        Plain,  ///< [table name size]
        Define, ///< [array define [-k] [-yrange lo hi] [-pix w h] name size]
    };

    Table(const ObjectArgs& a, Flavor flavor) : Object(a), names_(names()) {
        AtomSpan args = a.args;
        double size = 100;
        switch (flavor) {
        case Flavor::Graph: {
            name_ = argSymbol(args, 0);
            size = argFloat(args, 1, 100);
            const auto flags = static_cast<int>(argFloat(args, 3));
            saveContents_ = (flags & 1) != 0;
            break;
        }
        case Flavor::Plain:
            name_ = argSymbol(args, 0);
            size = argFloat(args, 1, 100);
            break;
        case Flavor::Define:
            while (!args.empty() && args[0].isSymbol() && args[0].asSymbol().str().starts_with('-')) {
                const auto flag = args[0].asSymbol().str();
                if (flag == "-k")
                    saveContents_ = true;
                else if ((flag == "-yrange" || flag == "-pix") && args.size() >= 3)
                    args = args.subspan(2);
                else
                    error("array define: unknown flag {}", flag);
                args = args.subspan(1);
            }
            name_ = argSymbol(args, 0);
            if (!args.empty() && args[0].isSymbol())
                args = args.subspan(1);
            size = argFloat(args, 0, 100);
            addInlet();
            addOutlet();
            break;
        }
        if (flavor != Flavor::Graph && name_.empty()) {
            static int counter = 0;
            name_ = Symbol(std::format("table{}", counter++));
        }
        if (size < 1)
            size = flavor == Flavor::Graph ? 1 : 100;
        data_.assign(static_cast<std::size_t>(size), 0.0f);
        buffers_.push_back(std::make_unique<SnapshotBuffer>(data_.size()));
        current_.store(buffers_.back().get(), std::memory_order_release);
        publish();

        tableBinding_ = &makeBinding(Binding::Kind::Table, name_);
        receiverBinding_ = &makeBinding(Binding::Kind::Receiver, name_);
        refresh_ = &makeClock([this] { publish(); });
    }

    // ---- TableStorage ----
    [[nodiscard]] std::span<float> tableData() noexcept override { return data_; }

    void tableChanged() noexcept override {
        if (!active_) { // control thread, not yet visible to the engine
            publish();
            return;
        }
        if (!refresh_->isSet())
            refresh_->setAt(std::max(now(), lastPublish_ + kRefreshMs));
    }

    void tableResize(std::size_t size) override {
        size = std::max<std::size_t>(size, 1);
        if (size == data_.size())
            return;
        data_.resize(size, 0.0f);
        if (size > current_.load(std::memory_order_relaxed)->capacity()) {
            // The GUI may still be reading the old buffer: keep it alive.
            buffers_.push_back(std::make_unique<SnapshotBuffer>(size));
            current_.store(buffers_.back().get(), std::memory_order_release);
        }
        tableChanged();
    }

    /// Saved contents longer than the declared size grow the array: the
    /// size in the box text can be stale after a "resize".
    void tableLoad(std::size_t offset, std::span<const float> values) override {
        if (offset + values.size() > data_.size()) {
            data_.resize(offset + values.size(), 0.0f);
            buffers_.push_back(std::make_unique<SnapshotBuffer>(data_.size()));
            current_.store(buffers_.back().get(), std::memory_order_release);
        }
        std::ranges::copy(values, data_.begin() + static_cast<std::ptrdiff_t>(offset));
        publish();
    }

    [[nodiscard]] std::vector<float> tableSnapshot() const override {
        std::vector<float> out;
        for (int attempt = 0; attempt < 10000; ++attempt) {
            std::uint64_t seen = ~std::uint64_t{0};
            if (current_.load(std::memory_order_acquire)->read(out, seen))
                return out;
            std::this_thread::yield();
        }
        return out;
    }

    [[nodiscard]] bool tableSaveContents() const noexcept override { return saveContents_; }

    [[nodiscard]] const SnapshotBuffer* snapshot() const override {
        return current_.load(std::memory_order_acquire);
    }

protected:
    void onActivate() override { active_ = true; }
    void onDeactivate() override { active_ = false; }

    void onBang(int) override {
        // [array define] outputs a pointer to its scalar in Pd; there are no
        // data structures here.
        error("array define: pointers are not supported");
    }
    void onFloat(int, double) override {} // a one-element list: no values
    void onList(int, AtomSpan args) override { setValues(args); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        const Names& n = names_;
        if (selector == n.resize) {
            tableResize(static_cast<std::size_t>(std::max(1.0, argFloat(args, 0))));
        } else if (selector == n.constant) {
            std::ranges::fill(data_, static_cast<float>(argFloat(args, 0)));
            tableChanged();
        } else if (selector == n.normalize) {
            normalize(argFloat(args, 0));
        } else if (selector == n.sinesum || selector == n.cosinesum) {
            fourierSum(args, selector == n.sinesum);
        } else if (selector == n.keep) {
            saveContents_ = argFloat(args, 0) != 0;
        } else if (selector == n.rename) {
            if (const Symbol s = argSymbol(args, 0); !s.empty()) {
                name_ = s;
                tableBinding_->rebind(s);
                receiverBinding_->rebind(s);
            }
        } else if (selector == n.print) {
            info("array {}: length {}", name_.str(), data_.size());
        } else if (std::ranges::find(n.appearance, selector) != n.appearance.end()) {
            notifyUi(selector, args);
        } else {
            Object::onAnything(inlet, selector, args);
        }
    }

private:
    static constexpr double kRefreshMs = 30.0;

    void publish() noexcept {
        current_.load(std::memory_order_relaxed)->write(data_);
        if (active_)
            lastPublish_ = now();
    }

    /// Pd's array "list" method: first atom is the start index.
    void setValues(AtomSpan args) {
        if (args.size() < 2)
            return;
        long long first = static_cast<long long>(args[0].asFloat());
        AtomSpan values = args.subspan(1);
        if (first < 0) { // drop values before the start
            const auto skip = static_cast<std::size_t>(-first);
            if (skip >= values.size())
                return;
            values = values.subspan(skip);
            first = 0;
        }
        const auto start = static_cast<std::size_t>(first);
        if (start >= data_.size())
            return;
        const auto n = std::min(values.size(), data_.size() - start);
        for (std::size_t i = 0; i < n; ++i)
            data_[start + i] = static_cast<float>(values[i].asFloat());
        tableChanged();
    }

    void normalize(double target) {
        if (target <= 0)
            target = 1;
        double maxv = 0;
        for (float v : data_)
            maxv = std::max(maxv, static_cast<double>(std::fabs(v)));
        if (maxv > 0) {
            const double k = target / maxv;
            for (auto& v : data_)
                v = static_cast<float>(v * k);
        }
        tableChanged();
    }

    /// "sinesum n a1 a2 ..." / "cosinesum n a0 a1 ...": resizes to n + 3
    /// (n rounded down to a power of two) and fills with harmonics, with
    /// one guard point before and two after the period, as [tabosc4~] wants.
    void fourierSum(AtomSpan args, bool sine) {
        if (args.size() < 2) {
            error("{}: {}: need number of points and partial strengths", sine ? "sinesum" : "cosinesum",
                  name_.str());
            return;
        }
        long npoints = static_cast<long>(argFloat(args, 0));
        if (npoints <= 0)
            npoints = 4096;
        npoints = static_cast<long>(std::bit_floor(static_cast<unsigned long>(npoints)));
        const AtomSpan partials = args.subspan(1);
        tableResize(static_cast<std::size_t>(npoints + 3));
        const double phaseIncrement = 2.0 * std::numbers::pi / static_cast<double>(npoints);
        for (std::size_t i = 0; i < data_.size(); ++i) {
            double sum = 0;
            for (std::size_t j = 0; j < partials.size(); ++j) {
                const double amp = partials[j].asFloat();
                sum += sine ? amp * std::sin((static_cast<double>(j) + 1.0) * static_cast<double>(i) * phaseIncrement)
                            : amp * std::cos(static_cast<double>(j) * static_cast<double>(i) * phaseIncrement);
            }
            data_[i] = static_cast<float>(sum);
        }
        tableChanged();
    }

    const Names& names_;
    Symbol name_;
    std::vector<float> data_;
    std::vector<std::unique_ptr<SnapshotBuffer>> buffers_; // current one last
    std::atomic<SnapshotBuffer*> current_{nullptr};
    NameBinding* tableBinding_ = nullptr;
    NameBinding* receiverBinding_ = nullptr;
    Clock* refresh_ = nullptr;
    double lastPublish_ = -1e300;
    bool saveContents_ = false;
    bool active_ = false;
};

// ===========================================================================
// Control-rate access.

/// Base for objects that refer to a table by name ("set name" changes it).
class TableUser : public Object {
public:
    TableUser(const ObjectArgs& a, Symbol name) : Object(a), names_(names()), name_(name) {}

protected:
    [[nodiscard]] TableStorage* table() const noexcept { return findTable(name_); }
    /// Like table(), but complains when the table is missing.
    TableStorage* tableOrError() const {
        auto* t = table();
        if (!t)
            error("{}: no such array", name_.str());
        return t;
    }

    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == names_.set && inlet == 0)
            name_ = argSymbol(args, 0);
        else
            Object::onAnything(inlet, selector, args);
    }

    const Names& names_;
    Symbol name_;
};

class TabRead final : public TableUser {
public:
    explicit TabRead(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addInlet();
        addOutlet();
    }

protected:
    void onFloat(int, double f) override {
        auto* t = tableOrError();
        if (!t)
            return;
        const auto data = t->tableData();
        if (data.empty()) {
            outFloat(0, 0);
            return;
        }
        const auto last = static_cast<long long>(data.size()) - 1;
        const auto i = std::clamp(static_cast<long long>(f), 0LL, last);
        outFloat(0, data[static_cast<std::size_t>(i)]);
    }
};

class TabRead4 final : public TableUser {
public:
    explicit TabRead4(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addInlet();
        addOutlet();
    }

protected:
    void onFloat(int, double f) override {
        auto* t = tableOrError();
        if (!t)
            return;
        const auto data = t->tableData();
        const auto npoints = static_cast<long long>(data.size());
        if (npoints < 4)
            outFloat(0, 0);
        else if (f <= 1)
            outFloat(0, data[1]);
        else if (f >= static_cast<double>(npoints - 2))
            outFloat(0, data[static_cast<std::size_t>(npoints - 2)]);
        else {
            auto n = static_cast<long long>(f);
            n = std::min(n, npoints - 3);
            outFloat(0, interpolate4(data.data() + n, static_cast<float>(f - static_cast<double>(n))));
        }
    }
};

class TabWrite final : public TableUser {
public:
    explicit TabWrite(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addInlet();
        addInlet();
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            index_ = f;
            return;
        }
        auto* t = tableOrError();
        if (!t)
            return;
        const auto data = t->tableData();
        if (data.empty())
            return;
        const auto i = std::clamp(static_cast<long long>(index_), 0LL, static_cast<long long>(data.size()) - 1);
        data[static_cast<std::size_t>(i)] = static_cast<float>(f);
        t->tableChanged();
    }

private:
    double index_ = 0;
};

// ---- [array size/get/set/sum/max/min] -------------------------------------

/// [array size name]: bang outputs the size, a float resizes. The right
/// inlet takes a new name.
class ArraySize final : public TableUser {
public:
    explicit ArraySize(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addInlet();
        addInlet();
        addOutlet();
    }

protected:
    void onBang(int inlet) override {
        if (inlet != 0)
            return Object::onBang(inlet);
        if (auto* t = tableOrError())
            outFloat(0, static_cast<double>(t->tableData().size()));
    }
    void onFloat(int inlet, double f) override {
        if (inlet != 0)
            return Object::onFloat(inlet, f);
        if (auto* t = tableOrError())
            t->tableResize(static_cast<std::size_t>(std::max(1.0, f)));
    }
    void onSymbol(int inlet, Symbol s) override {
        if (inlet == 1)
            name_ = s;
        else
            Object::onSymbol(inlet, s);
    }
};

enum class RangeOp { Get, Set, Sum, Max, Min };

/// The range operations share Pd's argument scheme: name, onset, count
/// (-1 = to the end). Inlets: [get/sum/max/min] main, count, name;
/// [set] main (list), onset, name.
template <RangeOp Op>
class ArrayRange final : public TableUser {
public:
    explicit ArrayRange(const ObjectArgs& a) : TableUser(a, {}) {
        AtomSpan args = a.args;
        if (!args.empty() && args[0].isSymbol()) {
            name_ = args[0].asSymbol();
            args = args.subspan(1);
        }
        onset_ = argFloat(args, 0, 0);
        count_ = argFloat(args, 1, -1);
        addInlet();
        addInlet(); // count, or onset for [array set]
        addInlet(); // name
        if constexpr (Op == RangeOp::Get || Op == RangeOp::Sum)
            addOutlet();
        if constexpr (Op == RangeOp::Max || Op == RangeOp::Min) {
            addOutlet(); // value
            addOutlet(); // index
        }
        if constexpr (Op == RangeOp::Get)
            list_.reserve(256);
    }

protected:
    void onBang(int inlet) override {
        if (inlet != 0)
            return Object::onBang(inlet);
        if constexpr (Op == RangeOp::Set)
            setValues({});
        else
            run();
    }
    void onFloat(int inlet, double f) override {
        if (inlet == 1) {
            if constexpr (Op == RangeOp::Set)
                onset_ = f;
            else
                count_ = f;
            return;
        }
        if (inlet != 0)
            return Object::onFloat(inlet, f);
        if constexpr (Op == RangeOp::Set) {
            const Atom v(f);
            setValues(AtomSpan(&v, 1));
        } else {
            onset_ = f;
            run();
        }
    }
    void onList(int inlet, AtomSpan args) override {
        if constexpr (Op == RangeOp::Set) {
            if (inlet == 0)
                return setValues(args);
        }
        Object::onList(inlet, args);
    }
    void onSymbol(int inlet, Symbol s) override {
        if (inlet == 2)
            name_ = s;
        else
            Object::onSymbol(inlet, s);
    }

private:
    /// The selected range, clamped to the table.
    std::span<float> range(TableStorage& t) const {
        const auto data = t.tableData();
        const auto size = static_cast<double>(data.size());
        const auto onset = static_cast<std::size_t>(std::clamp(std::trunc(onset_), 0.0, size));
        auto count = data.size() - onset;
        if (count_ >= 0)
            count = std::min(count, static_cast<std::size_t>(count_));
        return data.subspan(onset, count);
    }

    void run() {
        auto* t = tableOrError();
        if (!t)
            return;
        const auto r = range(*t);
        if constexpr (Op == RangeOp::Get) {
            list_.clear();
            for (float v : r)
                list_.emplace_back(static_cast<double>(v));
            outList(0, list_);
        } else if constexpr (Op == RangeOp::Sum) {
            double sum = 0;
            for (float v : r)
                sum += v;
            outFloat(0, sum);
        } else {
            const auto onset = static_cast<std::size_t>(r.data() - t->tableData().data());
            double best = Op == RangeOp::Max ? -1e30 : 1e30;
            double index = -1;
            for (std::size_t i = 0; i < r.size(); ++i)
                if (Op == RangeOp::Max ? r[i] > best : r[i] < best) {
                    best = r[i];
                    index = static_cast<double>(i + onset);
                }
            outFloat(1, index);
            outFloat(0, best);
        }
    }

    void setValues(AtomSpan values) {
        auto* t = tableOrError();
        if (!t)
            return;
        const auto r = range(*t);
        const auto n = std::min(r.size(), values.size());
        for (std::size_t i = 0; i < n; ++i)
            r[i] = static_cast<float>(values[i].asFloat());
        t->tableChanged();
    }

    double onset_ = 0, count_ = -1;
    AtomVector list_;
};

template <class T>
std::unique_ptr<Object> makeUnique(const ObjectArgs& a) {
    return std::make_unique<T>(a);
}

/// [array ...] dispatches on its first argument, like Pd.
std::unique_ptr<Object> makeArrayObject(const ObjectArgs& a) {
    if (a.args.empty() || !a.args[0].isSymbol())
        return std::make_unique<Table>(a, Table::Flavor::Define);
    const auto fn = a.args[0].asSymbol().str();
    const ObjectArgs sub{a.engine, a.className, a.args.subspan(1), a.rawArgs.empty() ? a.rawArgs : a.rawArgs.subspan(1),
                         a.dollarZero};
    if (fn == "define" || fn == "d")
        return std::make_unique<Table>(sub, Table::Flavor::Define);
    if (fn == "size")
        return makeUnique<ArraySize>(sub);
    if (fn == "get")
        return makeUnique<ArrayRange<RangeOp::Get>>(sub);
    if (fn == "set")
        return makeUnique<ArrayRange<RangeOp::Set>>(sub);
    if (fn == "sum")
        return makeUnique<ArrayRange<RangeOp::Sum>>(sub);
    if (fn == "max")
        return makeUnique<ArrayRange<RangeOp::Max>>(sub);
    if (fn == "min")
        return makeUnique<ArrayRange<RangeOp::Min>>(sub);
    if (fn.starts_with('-')) // flags: [array -k name] means define
        return std::make_unique<Table>(a, Table::Flavor::Define);
    throw ObjectError(std::format("array {}: unknown function", fn));
}

// ===========================================================================
// Signal objects.

/// [tabread~ name]: non-interpolating lookup; the index is truncated.
class TabReadSig final : public TableUser {
public:
    explicit TabReadSig(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addSignalInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        auto* t = table();
        const auto data = t ? t->tableData() : std::span<float>{};
        if (data.empty()) {
            std::ranges::fill(out, 0.0f);
            return;
        }
        const auto in = io.in(0);
        const int maxindex = static_cast<int>(std::min<std::size_t>(data.size(), INT_MAX)) - 1;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const float x = in[i];
            int index = x >= static_cast<float>(maxindex) ? maxindex : x > 0 ? static_cast<int>(x) : 0;
            out[i] = data[static_cast<std::size_t>(index)];
        }
    }
};

/// [tabread4~ name]: 4-point interpolation; the right inlet adds an onset
/// (in double precision, for long tables).
class TabRead4Sig final : public TableUser {
public:
    explicit TabRead4Sig(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addSignalInlet();
        addSignalInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        auto* t = table();
        const auto data = t ? t->tableData() : std::span<float>{};
        const long long maxindex = static_cast<long long>(data.size()) - 3;
        if (maxindex < 1) {
            std::ranges::fill(out, 0.0f);
            return;
        }
        const auto in = io.in(0), onset = io.in(1);
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double findex = static_cast<double>(in[i]) + static_cast<double>(onset[i]);
            long long index;
            float frac;
            if (!(findex >= 1)) { // also NaN
                index = 1;
                frac = 0;
            } else if (findex >= static_cast<double>(maxindex)) {
                index = maxindex;
                frac = findex >= static_cast<double>(maxindex + 1) ? 1.0f : static_cast<float>(findex - static_cast<double>(maxindex));
            } else {
                index = static_cast<long long>(findex);
                frac = static_cast<float>(findex - static_cast<double>(index));
            }
            out[i] = interpolate4(data.data() + index, frac);
        }
    }
};

/// [tabwrite~ name]: records into the table after a bang or "start onset".
class TabWriteSig final : public TableUser {
public:
    explicit TabWriteSig(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addSignalInlet();
    }
    void process(const DspIO& io) override {
        if (phase_ == kIdle)
            return;
        auto* t = table();
        if (!t)
            return;
        const auto data = t->tableData();
        const auto end = data.size();
        if (phase_ >= end) {
            phase_ = kIdle;
            return;
        }
        const auto in = io.in(0);
        const auto n = std::min(in.size(), end - phase_);
        for (std::size_t i = 0; i < n; ++i)
            data[phase_ + i] = bigOrSmall(in[i]) ? 0.0f : in[i];
        phase_ += n;
        if (phase_ >= end)
            phase_ = kIdle;
        t->tableChanged();
    }

protected:
    void onBang(int) override { phase_ = 0; }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == names_.start) {
            phase_ = static_cast<std::size_t>(std::max(0.0, argFloat(args, 0)));
        } else if (selector == names_.stop) {
            if (phase_ != kIdle)
                if (auto* t = table())
                    t->tableChanged();
            phase_ = kIdle;
        } else {
            if (selector == names_.set)
                phase_ = kIdle;
            TableUser::onAnything(inlet, selector, args);
        }
    }

private:
    static constexpr std::size_t kIdle = ~std::size_t{0};
    std::size_t phase_ = kIdle;
};

/// [tabplay~ name]: plays the table once. Bang plays all, a float starts
/// at that sample, "start length" plays a part; the right outlet bangs at
/// the end.
class TabPlay final : public TableUser {
public:
    explicit TabPlay(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addInlet();
        addSignalOutlet();
        addOutlet();
        done_ = &makeClock([this] { outBang(1); });
    }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        auto* t = table();
        const auto data = t ? t->tableData() : std::span<float>{};
        if (phase_ >= data.size() || phase_ >= limit_) {
            std::ranges::fill(out, 0.0f);
            return;
        }
        const auto end = std::min(data.size(), limit_);
        const auto n = std::min(out.size(), end - phase_);
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(phase_), n, out.begin());
        std::fill(out.begin() + static_cast<std::ptrdiff_t>(n), out.end(), 0.0f);
        phase_ += n;
        if (phase_ >= end) {
            phase_ = kIdle;
            done_->delay(0);
        }
    }

protected:
    void onBang(int) override { play({}); }
    void onFloat(int, double f) override {
        const Atom a(f);
        play(AtomSpan(&a, 1));
    }
    void onList(int, AtomSpan args) override { play(args); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        if (selector == names_.stop || selector == names_.set)
            phase_ = kIdle;
        if (selector != names_.stop)
            TableUser::onAnything(inlet, selector, args);
    }

private:
    static constexpr std::size_t kIdle = ~std::size_t{0};

    void play(AtomSpan args) {
        const double start = std::max(0.0, std::trunc(argFloat(args, 0)));
        const double length = std::trunc(argFloat(args, 1));
        phase_ = static_cast<std::size_t>(start);
        limit_ = length <= 0 ? kIdle : static_cast<std::size_t>(start + length);
    }

    std::size_t phase_ = kIdle, limit_ = kIdle;
    Clock* done_;
};

/// [tabosc4~ name]: wavetable oscillator. The table holds a power of two
/// points plus three guard points (one before, two after), as made by
/// "sinesum". The right inlet resets the phase.
class TabOsc4 final : public TableUser {
public:
    explicit TabOsc4(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addSignalInlet();
        addInlet();
        addSignalOutlet();
    }
    void prepare(const DspSetup& s) override { conv_ = 1.0 / s.sampleRate; }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        auto* t = table();
        const auto data = t ? t->tableData() : std::span<float>{};
        const auto npoints = data.size() >= 4 ? data.size() - 3 : 0;
        if (npoints == 0 || !std::has_single_bit(npoints)) {
            std::ranges::fill(out, 0.0f);
            return;
        }
        const auto in = io.in(0);
        const auto fn = static_cast<double>(npoints);
        const auto mask = static_cast<long long>(npoints) - 1;
        const double conv = fn * conv_;
        double dphase = fn * phase_;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double whole = std::floor(dphase);
            const auto index = static_cast<long long>(whole) & mask;
            const auto frac = static_cast<float>(dphase - whole);
            out[i] = interpolate4(data.data() + index + 1, frac);
            dphase += static_cast<double>(in[i]) * conv;
        }
        dphase /= fn;
        phase_ = dphase - std::floor(dphase);
    }

protected:
    void onFloat(int inlet, double f) override {
        if (inlet == 1)
            phase_ = f - std::floor(f);
        else
            Object::onFloat(inlet, f);
    }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override {
        TableUser::onAnything(inlet, selector, args);
        if (selector == names_.set)
            if (auto* t = table(); t && (t->tableData().size() < 4 || !std::has_single_bit(t->tableData().size() - 3)))
                error("{}: number of points ({}) not a power of 2 plus three", name_.str(), t->tableData().size());
    }

private:
    double phase_ = 0, conv_ = 1.0 / 48000.0;
};

/// [tabsend~ name]: writes each block to the start of the table.
class TabSend final : public TableUser {
public:
    explicit TabSend(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) { addSignalInlet(); }
    void process(const DspIO& io) override {
        auto* t = table();
        if (!t)
            return;
        const auto data = t->tableData();
        const auto in = io.in(0);
        const auto n = std::min(in.size(), data.size());
        for (std::size_t i = 0; i < n; ++i)
            data[i] = bigOrSmall(in[i]) ? 0.0f : in[i];
        t->tableChanged();
    }
};

/// [tabreceive~ name]: reads the start of the table every block.
class TabReceive final : public TableUser {
public:
    explicit TabReceive(const ObjectArgs& a) : TableUser(a, argSymbol(a.args, 0)) {
        addInlet();
        addSignalOutlet();
    }
    void process(const DspIO& io) override {
        auto out = io.out(0);
        auto* t = table();
        const auto data = t ? t->tableData() : std::span<float>{};
        const auto n = std::min(out.size(), data.size());
        std::copy_n(data.begin(), n, out.begin());
        std::fill(out.begin() + static_cast<std::ptrdiff_t>(n), out.end(), 0.0f);
    }
};

template <class T>
ObjectRegistry::Factory make() {
    return [](const ObjectArgs& a) -> std::unique_ptr<Object> { return std::make_unique<T>(a); };
}

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerTableObjects(ObjectRegistry& r) {
    r.add(
        "@array",
        [](const ObjectArgs& a) -> std::unique_ptr<Object> { return std::make_unique<Table>(a, Table::Flavor::Graph); },
        "array inside a graph");
    r.add(
        "table",
        [](const ObjectArgs& a) -> std::unique_ptr<Object> { return std::make_unique<Table>(a, Table::Flavor::Plain); },
        "named array (not saved)");
    r.add("array", makeArrayObject, "array define / size / get / set / sum / max / min");

    r.add<TabRead>("tabread", "read a table value");
    r.add<TabRead4>("tabread4", "read a table with 4-point interpolation");
    r.add<TabWrite>("tabwrite", "write a table value");
    r.add<TabReadSig>("tabread~", "signal table lookup");
    r.add<TabRead4Sig>("tabread4~", "signal table lookup, 4-point interpolation");
    r.add<TabWriteSig>("tabwrite~", "record a signal into a table");
    r.add<TabPlay>("tabplay~", "play a table as a sample");
    r.add<TabOsc4>("tabosc4~", "wavetable oscillator");
    r.add<TabSend>("tabsend~", "write a block into a table continuously");
    r.add<TabReceive>("tabreceive~", "read a block from a table continuously");
}

} // namespace qure
