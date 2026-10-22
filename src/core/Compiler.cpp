#include "Compiler.hpp"

#include <algorithm>
#include <deque>
#include <format>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace qure {
namespace {

struct SignalSource {
    int node;
    int outlet; ///< signal outlet index
};

struct DspNode {
    Object* object;
    std::vector<std::vector<SignalSource>> inputs; ///< per signal inlet
    std::vector<bool> connected;                   ///< inlet has a signal connection in the patch
    std::vector<int> successors;
    int indegree = 0;
};

class Builder {
public:
    Builder(std::vector<std::string>& warnings, int blockSize) : warnings_(warnings), blockSize_(blockSize) {}

    std::unique_ptr<Plan> build(const std::vector<std::unique_ptr<Canvas>>& roots) {
        plan_ = std::make_unique<Plan>();
        for (const auto& r : roots)
            index(*r);
        for (const auto& r : roots)
            collect(*r);
        buildRouting();
        buildDsp();
        return std::move(plan_);
    }

private:
    // -- indexing ------------------------------------------------------------

    void index(const Canvas& canvas) {
        for (const auto& c : canvas.connections())
            outgoing_[c.from].push_back(&c);
        iolets_[&canvas] = {canvas.ioletBoxes(true), canvas.ioletBoxes(false)};
        for (const auto& b : canvas.boxes())
            if (b->child)
                index(*b->child);
    }

    std::span<const Connection* const> outgoing(const Box* box) const {
        auto it = outgoing_.find(box);
        if (it == outgoing_.end())
            return {};
        return it->second;
    }

    const std::vector<Box*>& ioletsOf(const Canvas& canvas, bool inlets) const {
        const auto& p = iolets_.at(&canvas);
        return inlets ? p.first : p.second;
    }

    // Position of an [outlet]/[outlet~] box among its canvas' outlets.
    int outletIndex(const Box& box) const {
        const auto& outs = ioletsOf(*box.canvas, false);
        auto it = std::ranges::find(outs, &box);
        return it == outs.end() ? -1 : static_cast<int>(it - outs.begin());
    }

    // -- collection ------------------------------------------------------------

    void collect(const Canvas& canvas) {
        for (const auto& b : canvas.boxes()) {
            if (b->object) {
                Object* o = b->object.get();
                plan_->objects.push_back(b->object);
                boxOf_[o] = b.get();
                if (o->role() == ObjectRole::Normal) {
                    plan_->byId.emplace(o->id(), o);
                    if (o->wantsDsp()) {
                        nodeOf_[o] = static_cast<int>(nodes_.size());
                        DspNode n{o};
                        n.inputs.resize(static_cast<std::size_t>(o->numSignalInlets()));
                        n.connected.resize(static_cast<std::size_t>(o->numSignalInlets()), false);
                        nodes_.push_back(std::move(n));
                    }
                }
            }
            if (b->child)
                collect(*b->child);
        }
    }

    // -- control routing -------------------------------------------------------

    void resolveControl(const Box* to, int inlet, std::vector<Target>& out, int depth) const {
        if (depth > 512)
            return;
        if (to->child && !to->object) {
            const auto& ins = ioletsOf(*to->child, true);
            if (inlet >= static_cast<int>(ins.size()))
                return;
            const Box* ib = ins[static_cast<std::size_t>(inlet)];
            if (ib->object->role() != ObjectRole::Inlet)
                return;
            for (const auto* c : outgoing(ib))
                if (c->outlet == 0)
                    resolveControl(c->to, c->inlet, out, depth + 1);
            return;
        }
        if (!to->object)
            return;
        switch (to->object->role()) {
        case ObjectRole::Normal: out.push_back({to->object.get(), inlet}); return;
        case ObjectRole::Outlet: {
            const Box* owner = to->canvas->owner();
            const int idx = outletIndex(*to);
            if (!owner || idx < 0)
                return;
            for (const auto* c : outgoing(owner))
                if (c->outlet == idx && !owner->outlets[static_cast<std::size_t>(idx)])
                    resolveControl(c->to, c->inlet, out, depth + 1);
            return;
        }
        default: return;
        }
    }

    void buildRouting() {
        std::vector<std::pair<std::size_t, std::size_t>> ranges; // (offset, count) per outlet
        std::vector<Target> tmp;
        for (const auto& sp : plan_->objects) {
            Object* o = sp.get();
            const Box* box = boxOf_.at(o);
            plan_->bindings.push_back({o, static_cast<std::uint32_t>(ranges.size())});
            for (int outlet = 0; outlet < o->numOutlets(); ++outlet) {
                const auto offset = plan_->targets.size();
                if (!o->isSignalOutlet(outlet) && o->role() == ObjectRole::Normal) {
                    for (const auto* c : outgoing(box)) {
                        if (c->outlet != outlet)
                            continue;
                        tmp.clear();
                        resolveControl(c->to, c->inlet, tmp, 0);
                        plan_->targets.insert(plan_->targets.end(), tmp.begin(), tmp.end());
                    }
                }
                ranges.emplace_back(offset, plan_->targets.size() - offset);
            }
        }
        plan_->outletTables.reserve(ranges.size());
        for (auto [offset, count] : ranges)
            plan_->outletTables.push_back({plan_->targets.data() + offset, static_cast<std::uint32_t>(count)});
    }

    // -- DSP graph -------------------------------------------------------------

    void resolveSignal(const Box* to, int inlet, std::vector<std::pair<int, int>>& out, int depth) const {
        if (depth > 512)
            return;
        if (to->child && !to->object) {
            const auto& ins = ioletsOf(*to->child, true);
            if (inlet >= static_cast<int>(ins.size()))
                return;
            const Box* ib = ins[static_cast<std::size_t>(inlet)];
            if (ib->object->role() != ObjectRole::SignalInlet)
                return;
            for (const auto* c : outgoing(ib))
                if (c->outlet == 0)
                    resolveSignal(c->to, c->inlet, out, depth + 1);
            return;
        }
        if (!to->object)
            return;
        Object* o = to->object.get();
        switch (o->role()) {
        case ObjectRole::Normal:
            if (inlet < o->numInlets() && o->isSignalInlet(inlet))
                if (auto it = nodeOf_.find(o); it != nodeOf_.end())
                    out.emplace_back(it->second, o->signalInletIndex(inlet));
            return;
        case ObjectRole::SignalOutlet: {
            const Box* owner = to->canvas->owner();
            const int idx = outletIndex(*to);
            if (!owner || idx < 0)
                return;
            for (const auto* c : outgoing(owner))
                if (c->outlet == idx)
                    resolveSignal(c->to, c->inlet, out, depth + 1);
            return;
        }
        default: return;
        }
    }

    void addEdge(int from, int to) {
        nodes_[static_cast<std::size_t>(from)].successors.push_back(to);
        ++nodes_[static_cast<std::size_t>(to)].indegree;
    }

    bool reachable(int from, int to) const {
        std::vector<char> seen(nodes_.size(), 0);
        std::vector<int> stack{from};
        while (!stack.empty()) {
            const int n = stack.back();
            stack.pop_back();
            if (n == to)
                return true;
            if (seen[static_cast<std::size_t>(n)])
                continue;
            seen[static_cast<std::size_t>(n)] = 1;
            for (int s : nodes_[static_cast<std::size_t>(n)].successors)
                stack.push_back(s);
        }
        return false;
    }

    void buildEdges() {
        std::vector<std::pair<int, int>> dests;
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            Object* o = nodes_[n].object;
            const Box* box = boxOf_.at(o);
            for (const auto* c : outgoing(box)) {
                if (c->outlet >= o->numOutlets() || !o->isSignalOutlet(c->outlet))
                    continue;
                dests.clear();
                resolveSignal(c->to, c->inlet, dests, 0);
                for (auto [dst, inlet] : dests) {
                    nodes_[static_cast<std::size_t>(dst)].inputs[static_cast<std::size_t>(inlet)].push_back(
                        {static_cast<int>(n), o->signalOutletIndex(c->outlet)});
                    addEdge(static_cast<int>(n), dst);
                }
            }
        }
        // An inlet with a signal connection in the patch reads zeros when
        // nothing real feeds it (e.g. through an unconnected [inlet~]),
        // instead of falling back to its scalar.
        for (const auto& [box, conns] : outgoing_) {
            for (const auto* c : conns) {
                if (!c->isSignal() || !c->to->object)
                    continue;
                auto it = nodeOf_.find(c->to->object.get());
                if (it == nodeOf_.end() || !c->to->object->isSignalInlet(c->inlet))
                    continue;
                nodes_[static_cast<std::size_t>(it->second)]
                    .connected[static_cast<std::size_t>(c->to->object->signalInletIndex(c->inlet))] = true;
            }
        }
        // Implicit edges for named signal connections.
        std::map<std::pair<int, Symbol>, std::vector<int>> providers, consumers;
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            const auto link = nodes_[n].object->dspLink();
            const auto key = std::make_pair(static_cast<int>(link.kind), link.name);
            if (link.side == DspLink::Side::Provides)
                providers[key].push_back(static_cast<int>(n));
            else if (link.side == DspLink::Side::Consumes)
                consumers[key].push_back(static_cast<int>(n));
        }
        for (const auto& [key, prov] : providers) {
            auto it = consumers.find(key);
            if (it == consumers.end())
                continue;
            for (int p : prov)
                for (int c : it->second)
                    if (p != c && !reachable(c, p))
                        addEdge(p, c);
        }
    }

    std::vector<int> sortNodes() {
        std::vector<int> order;
        order.reserve(nodes_.size());
        std::vector<int> indeg(nodes_.size());
        std::deque<int> ready;
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            indeg[n] = nodes_[n].indegree;
            if (indeg[n] == 0)
                ready.push_back(static_cast<int>(n));
        }
        while (!ready.empty()) {
            const int n = ready.front();
            ready.pop_front();
            order.push_back(n);
            for (int s : nodes_[static_cast<std::size_t>(n)].successors)
                if (--indeg[static_cast<std::size_t>(s)] == 0)
                    ready.push_back(s);
        }
        if (order.size() != nodes_.size()) {
            std::string names;
            for (std::size_t n = 0; n < nodes_.size(); ++n)
                if (indeg[n] > 0) {
                    if (!names.empty())
                        names += ", ";
                    names += nodes_[n].object->className().str();
                }
            warnings_.push_back(std::format("DSP loop detected; not running: {}", names));
        }
        return order;
    }

    void buildDsp() {
        buildEdges();
        const auto order = sortNodes();

        std::vector<char> scheduled(nodes_.size(), 0);
        for (int n : order)
            scheduled[static_cast<std::size_t>(n)] = 1;

        // Reader counts per (node, outlet) to know when a buffer dies.
        std::vector<std::vector<int>> readers(nodes_.size());
        std::vector<std::vector<int>> bufferOf(nodes_.size());
        for (std::size_t n = 0; n < nodes_.size(); ++n) {
            readers[n].assign(static_cast<std::size_t>(nodes_[n].object->numSignalOutlets()), 0);
            bufferOf[n].assign(static_cast<std::size_t>(nodes_[n].object->numSignalOutlets()), -1);
        }
        for (int n : order)
            for (const auto& srcs : nodes_[static_cast<std::size_t>(n)].inputs)
                for (const auto& s : srcs)
                    if (scheduled[static_cast<std::size_t>(s.node)])
                        ++readers[static_cast<std::size_t>(s.node)][static_cast<std::size_t>(s.outlet)];

        constexpr int kZero = 0; // buffer 0 is all zeros and never written
        int nextBuffer = 1;
        std::vector<int> freeList;
        auto alloc = [&] {
            if (!freeList.empty()) {
                const int b = freeList.back();
                freeList.pop_back();
                return b;
            }
            return nextBuffer++;
        };

        std::vector<int> inIdx, outIdx; // buffer indices, turned into pointers later
        std::vector<int> temps;
        for (int n : order) {
            auto& node = nodes_[static_cast<std::size_t>(n)];
            Object* o = node.object;
            temps.clear();
            std::vector<int> inputs;
            for (std::size_t k = 0; k < node.inputs.size(); ++k) {
                std::vector<int> srcBufs;
                for (const auto& s : node.inputs[k])
                    if (scheduled[static_cast<std::size_t>(s.node)])
                        srcBufs.push_back(bufferOf[static_cast<std::size_t>(s.node)][static_cast<std::size_t>(s.outlet)]);
                if (srcBufs.empty()) {
                    if (node.connected[k]) {
                        inputs.push_back(kZero);
                    } else {
                        const int t = alloc();
                        temps.push_back(t);
                        DspStep step{DspStep::Op::Fill, o, o->signalScalarPtr(static_cast<int>(k))};
                        step.outBegin = static_cast<std::uint32_t>(outIdx.size());
                        step.outCount = 1;
                        outIdx.push_back(t);
                        plan_->steps.push_back(step);
                        inputs.push_back(t);
                    }
                } else if (srcBufs.size() == 1) {
                    inputs.push_back(srcBufs.front());
                } else {
                    const int t = alloc();
                    temps.push_back(t);
                    DspStep step{DspStep::Op::Sum, o};
                    step.inBegin = static_cast<std::uint32_t>(inIdx.size());
                    step.inCount = static_cast<std::uint32_t>(srcBufs.size());
                    inIdx.insert(inIdx.end(), srcBufs.begin(), srcBufs.end());
                    step.outBegin = static_cast<std::uint32_t>(outIdx.size());
                    step.outCount = 1;
                    outIdx.push_back(t);
                    plan_->steps.push_back(step);
                    inputs.push_back(t);
                }
            }
            DspStep step{DspStep::Op::Process, o};
            step.inBegin = static_cast<std::uint32_t>(inIdx.size());
            step.inCount = static_cast<std::uint32_t>(inputs.size());
            inIdx.insert(inIdx.end(), inputs.begin(), inputs.end());
            step.outBegin = static_cast<std::uint32_t>(outIdx.size());
            step.outCount = static_cast<std::uint32_t>(o->numSignalOutlets());
            for (int out = 0; out < o->numSignalOutlets(); ++out) {
                const int b = alloc();
                bufferOf[static_cast<std::size_t>(n)][static_cast<std::size_t>(out)] = b;
                outIdx.push_back(b);
            }
            plan_->steps.push_back(step);

            // Release what died with this step.
            for (int t : temps)
                freeList.push_back(t);
            for (const auto& srcs : node.inputs)
                for (const auto& s : srcs)
                    if (scheduled[static_cast<std::size_t>(s.node)] &&
                        --readers[static_cast<std::size_t>(s.node)][static_cast<std::size_t>(s.outlet)] == 0)
                        freeList.push_back(bufferOf[static_cast<std::size_t>(s.node)][static_cast<std::size_t>(s.outlet)]);
            for (int out = 0; out < o->numSignalOutlets(); ++out)
                if (readers[static_cast<std::size_t>(n)][static_cast<std::size_t>(out)] == 0)
                    freeList.push_back(bufferOf[static_cast<std::size_t>(n)][static_cast<std::size_t>(out)]);
        }

        plan_->bufferCount = static_cast<std::size_t>(nextBuffer);
        plan_->buffers.allocate(plan_->bufferCount * static_cast<std::size_t>(blockSize_));
        auto ptr = [&](int b) { return plan_->buffers.data.get() + static_cast<std::size_t>(b) * blockSize_; };
        plan_->inPtrs.reserve(inIdx.size());
        for (int b : inIdx)
            plan_->inPtrs.push_back(ptr(b));
        plan_->outPtrs.reserve(outIdx.size());
        for (int b : outIdx)
            plan_->outPtrs.push_back(ptr(b));
    }

    std::vector<std::string>& warnings_;
    const int blockSize_;
    std::unique_ptr<Plan> plan_;
    std::unordered_map<const Box*, std::vector<const Connection*>> outgoing_;
    std::unordered_map<const Canvas*, std::pair<std::vector<Box*>, std::vector<Box*>>> iolets_;
    std::unordered_map<const Object*, const Box*> boxOf_;
    std::unordered_map<const Object*, int> nodeOf_;
    std::vector<DspNode> nodes_;
};

} // namespace

std::unique_ptr<Plan> compilePlan(const std::vector<std::unique_ptr<Canvas>>& roots, std::vector<std::string>& warnings,
                                  int blockSize) {
    return Builder(warnings, blockSize).build(roots);
}

} // namespace qure
