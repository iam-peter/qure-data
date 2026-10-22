// Engine side of the visuals module: [gfx.window] [gfx.qml] [gfx.shader]
// [gfx.mix] [gfx.feedback] [gfx.audio~].
//
// Frames are not made here – the GUI builds a render graph from the gfx
// boxes and their texture cords and draws it on the GPU at display rate
// (see docs/VISUALS.md). These objects give the boxes their iolets and pass
// control messages on to the GUI, exactly like front-end-only objects:
// `in <inlet> <selector> <args...>`. [gfx.audio~] also publishes the latest
// samples of its signal for the GUI to turn into a texture.

#include "Common.hpp"
#include "Snapshot.hpp"

#include <memory>

namespace qure::objects {
namespace {

enum class Port : unsigned char { Texture, Control, Signal };

class Gfx : public Object {
public:
    Gfx(const ObjectArgs& a, std::initializer_list<Port> inlets, std::initializer_list<Port> outlets)
        : Object(a), in_("in"), out_("out") {
        addPorts(std::span<const Port>(inlets.begin(), inlets.size()), std::span<const Port>(outlets.begin(), outlets.size()));
        buf_.reserve(32);
    }

    void addPorts(std::span<const Port> inlets, std::span<const Port> outlets) {
        for (Port p : inlets)
            p == Port::Texture ? addTextureInlet() : p == Port::Signal ? addSignalInlet() : addInlet();
        for (Port p : outlets)
            p == Port::Texture ? addTextureOutlet() : p == Port::Signal ? addSignalOutlet() : addOutlet();
    }

    // A QML scene ([gfx.qml]) can send messages out of a control outlet
    // with node.output(outlet, values) → "out outlet values...".
    void uiEvent(Symbol selector, AtomSpan args) override {
        if (selector != out_ || args.empty() || !args[0].isFloat()) {
            receive(0, selector, args);
            return;
        }
        const int outlet = static_cast<int>(args[0].asFloat());
        if (outlet < 0 || outlet >= numOutlets() || isTextureOutlet(outlet) || isSignalOutlet(outlet))
            return;
        const auto rest = args.subspan(1);
        if (rest.empty())
            outBang(outlet);
        else if (rest[0].isSymbol())
            outAnything(outlet, rest[0].asSymbol(), rest.subspan(1));
        else
            outMessage(outlet, sym::list(), rest);
    }

protected:
    void onBang(int inlet) override { forward(inlet, sym::bang(), {}); }
    void onFloat(int inlet, double f) override {
        if (inlet < numInlets() && isSignalInlet(inlet)) {
            Object::onFloat(inlet, f);
            return;
        }
        const Atom a(f);
        forward(inlet, sym::float_(), AtomSpan(&a, 1));
    }
    void onSymbol(int inlet, Symbol s) override {
        const Atom a(s);
        forward(inlet, sym::symbol(), AtomSpan(&a, 1));
    }
    void onList(int inlet, AtomSpan args) override { forward(inlet, sym::list(), args); }
    void onAnything(int inlet, Symbol selector, AtomSpan args) override { forward(inlet, selector, args); }

private:
    void forward(int inlet, Symbol selector, AtomSpan args) {
        buf_.clear();
        buf_.emplace_back(inlet);
        buf_.emplace_back(selector);
        buf_.insert(buf_.end(), args.begin(), args.end());
        notifyUi(in_, buf_);
    }

    Symbol in_, out_;
    AtomVector buf_;
};

/// [gfx.audio~ samples]: the last `samples` (default 256) of its input,
/// published every block for a texture.
class GfxAudio final : public Gfx {
public:
    explicit GfxAudio(const ObjectArgs& a) : Gfx(a, {Port::Signal}, {Port::Texture}) {
        size_ = std::clamp(static_cast<int>(argFloat(a.args, 0, 256)), 16, 4096);
        snapshot_ = std::make_unique<SnapshotBuffer>(static_cast<std::size_t>(size_));
        ring_.assign(static_cast<std::size_t>(size_), 0.0f);
        frame_.assign(static_cast<std::size_t>(size_), 0.0f);
    }

    void process(const DspIO& io) override {
        for (float x : io.in(0)) {
            ring_[static_cast<std::size_t>(pos_)] = x;
            pos_ = (pos_ + 1) % size_;
        }
        for (int i = 0; i < size_; ++i)
            frame_[static_cast<std::size_t>(i)] = ring_[static_cast<std::size_t>((pos_ + i) % size_)];
        snapshot_->write(frame_);
    }
    [[nodiscard]] const SnapshotBuffer* snapshot() const override { return snapshot_.get(); }

private:
    int size_, pos_ = 0;
    std::vector<float> ring_, frame_;
    std::unique_ptr<SnapshotBuffer> snapshot_;
};

/// Factory for a generic gfx object with the given iolets.
ObjectRegistry::Factory gfx(std::vector<Port> ins, std::vector<Port> outs) {
    return [ins, outs](const ObjectArgs& a) -> std::unique_ptr<Object> {
        auto obj = std::make_unique<Gfx>(a, std::initializer_list<Port>{}, std::initializer_list<Port>{});
        obj->addPorts(ins, outs);
        return obj;
    };
}

} // namespace
} // namespace qure::objects

namespace qure {
using namespace objects;

void registerVisualObjects(ObjectRegistry& r) {
    using P = Port;
    r.add("gfx.window", gfx({P::Texture}, {}), "visuals: output window – gfx.window width height");
    r.add("gfx.qml", gfx({P::Texture}, {P::Texture, P::Control}), "visuals: render a QML scene – gfx.qml scene.qml");
    r.add("gfx.shader", gfx({P::Texture, P::Texture}, {P::Texture}),
          "visuals: fragment shader – gfx.shader name-or-file.frag");
    r.add("gfx.mix", gfx({P::Texture, P::Texture, P::Control}, {P::Texture}),
          "visuals: crossfade two textures – right inlet 0..1");
    r.add("gfx.feedback", gfx({P::Texture}, {P::Texture}), "visuals: video feedback – decay zoom rotate");
    r.add<GfxAudio>("gfx.audio~", "visuals: a signal as a texture (red = waveform)");
}

} // namespace qure
