#pragma once

#include "Patch.hpp"
#include "Plan.hpp"

#include <memory>
#include <string>
#include <vector>

namespace qure {

/// Compiles open patches into an executable Plan:
///
///  1. Control connections are resolved through subpatch [inlet]/[outlet]
///     boxes, so at runtime a message goes straight to its final receiver.
///  2. Signal connections are resolved the same way through [inlet~] and
///     [outlet~]; the resulting flat graph is sorted topologically, adding
///     implicit edges for [send~]/[receive~] and [throw~]/[catch~] where
///     they do not introduce cycles.
///  3. Signal buffers are assigned with liveness analysis, so a patch with
///     thousands of signal objects touches only as many buffers as are
///     simultaneously live – good for the cache. Fan-in is summed
///     explicitly; unconnected signal inlets read their scalar.
std::unique_ptr<Plan> compilePlan(const std::vector<std::unique_ptr<Canvas>>& roots, std::vector<std::string>& warnings,
                                  int blockSize = 64);

} // namespace qure
