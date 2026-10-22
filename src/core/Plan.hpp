#pragma once

#include "Object.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>

namespace qure {

/// One instruction of the compiled DSP program.
struct DspStep {
    enum class Op : unsigned char {
        Process, ///< obj->process(io) with pointers [inBegin, +inCount) and [outBegin, +outCount)
        Sum,     ///< out[0] = sum of in[...]
        Fill,    ///< out[0] = *scalar
    };
    Op op = Op::Process;
    Object* object = nullptr;
    const float* scalar = nullptr;
    std::uint32_t inBegin = 0, inCount = 0;
    std::uint32_t outBegin = 0, outCount = 0;
};

/// 64-byte aligned float storage for all signal buffers of a plan.
struct AlignedBuffer {
    struct Free {
        void operator()(float* p) const noexcept { ::operator delete[](p, std::align_val_t{64}); }
    };
    std::unique_ptr<float[], Free> data;
    std::size_t size = 0;

    void allocate(std::size_t n) {
        data.reset(static_cast<float*>(::operator new[](n * sizeof(float), std::align_val_t{64})));
        size = n;
        std::fill_n(data.get(), n, 0.0f);
    }
};

/// An immutable, fully resolved snapshot of every open patch: who receives
/// what from whom and in which order the DSP runs. Built on the control
/// thread, published atomically, executed by the engine thread. Editing a
/// patch never blocks or locks the audio thread – it just swaps plans.
struct Plan {
    std::uint64_t generation = 0;

    /// Keeps every live object alive while this plan can still run.
    std::vector<std::shared_ptr<Object>> objects;

    /// Lifecycle work to do on installation (engine thread). Shared pointers
    /// so that objects removed before their first install stay alive until
    /// this plan is reclaimed on the control thread.
    std::vector<std::shared_ptr<Object>> activate;
    std::vector<std::shared_ptr<Object>> deactivate;

    // ---- control routing ----
    std::vector<Target> targets;             ///< flattened destination lists
    std::vector<OutletTargets> outletTables; ///< per object, per outlet, into `targets`
    struct Binding {
        Object* object;
        std::uint32_t firstOutlet;
    };
    std::vector<Binding> bindings;
    std::unordered_map<ObjectId, Object*> byId;

    // ---- DSP ----
    std::vector<DspStep> steps;
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;
    AlignedBuffer buffers;
    std::size_t bufferCount = 0;
};

} // namespace qure
