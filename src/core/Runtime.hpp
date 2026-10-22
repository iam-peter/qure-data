#pragma once

#include "Engine.hpp"
#include "ObjectRegistry.hpp"
#include "Patch.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace qure {

/// Qure Data saves its patches as `.qd`; the content is Pd's file format,
/// so `.pd` files from Pd open (and abstractions resolve) as well.
inline constexpr const char* kPatchExtension = ".qd";
inline constexpr const char* kPdExtension = ".pd";

/// Owns the engine, the object registry and every open document, and turns
/// document edits into published plans. This is the whole non-GUI program:
/// the Qt application and the tests both drive a Runtime.
class Runtime {
public:
    explicit Runtime(EngineConfig config = {});
    ~Runtime();

    [[nodiscard]] Engine& engine() noexcept { return engine_; }
    [[nodiscard]] ObjectRegistry& registry() noexcept { return registry_; }

    // ---- documents ----
    Canvas& newPatch(std::string name = "Untitled");
    std::expected<Canvas*, std::string> openPatch(const std::filesystem::path& file);
    /// Loads a patch from text (tests, clipboard, templates).
    std::expected<Canvas*, std::string> openPatchText(std::string_view text, std::string name,
                                                      std::filesystem::path directory = {});
    void closePatch(Canvas& patch);
    [[nodiscard]] const std::vector<std::unique_ptr<Canvas>>& patches() const noexcept { return patches_; }

    /// Compiles every open patch into a plan and hands it to the engine.
    void commit();
    /// Sends loadbang to every object in `canvas` (subpatches first).
    void loadbang(Canvas& canvas);
    /// Housekeeping: frees plans and objects the engine has let go of.
    void collectGarbage() { engine_.reclaim(); }

    /// Messages produced while compiling (e.g. "DSP loop detected").
    [[nodiscard]] const std::vector<std::string>& compileWarnings() const noexcept { return warnings_; }
    /// Whether an open patch contains [adc~], as of the last commit. Audio
    /// input is opened only then: on a Bluetooth headset, opening the
    /// microphone switches playback to the low-quality headset profile.
    [[nodiscard]] bool usesAudioInput() const noexcept { return usesAudioInput_; }

    // ---- abstractions: `name.qd`, then `name.pd`, next to the patch or
    // in a search path ----
    std::vector<std::filesystem::path> searchPaths;
    [[nodiscard]] std::filesystem::path findAbstraction(const Canvas& from, std::string_view name) const;

    // ---- used by Canvas ----
    /// Creates the engine object / child canvas for a box from its text.
    void instantiate(Box& box);
    [[nodiscard]] BoxId allocateBoxId() noexcept { return nextBoxId_++; }
    [[nodiscard]] int allocateDollarZero() noexcept { return nextDollarZero_++; }

private:
    void instantiateObjectBox(Box& box);
    std::unique_ptr<Canvas> loadAbstraction(Canvas& parent, const std::filesystem::path& file, AtomSpan args,
                                            std::string& error);

    Engine engine_;
    ObjectRegistry registry_;
    std::vector<std::unique_ptr<Canvas>> patches_;
    std::vector<std::string> warnings_;
    bool usesAudioInput_ = false;
    BoxId nextBoxId_ = 1;
    int nextDollarZero_ = 1000;
    int abstractionDepth_ = 0;
};

} // namespace qure
