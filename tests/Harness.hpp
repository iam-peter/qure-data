#pragma once

#include "Plan.hpp"
#include "Runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace qure::test {

/// Drives a Runtime synchronously: the test thread plays both the control
/// and the engine thread (never at the same time).
struct Harness {
    Runtime rt;
    std::vector<std::string> log;
    std::vector<EngineEvent> notifications;
    std::vector<std::vector<Atom>> notificationArgs;

    explicit Harness(EngineConfig cfg = {}) : rt(cfg) {}

    Canvas& open(std::string_view text) {
        auto r = rt.openPatchText(text, "test.pd");
        REQUIRE(r.has_value());
        return **r;
    }

    void run(int blocks = 1) {
        for (int i = 0; i < blocks; ++i)
            rt.engine().tick();
        drain();
    }

    void drain() {
        rt.engine().drainEvents([&](const EngineEvent& e) {
            if (e.kind == EngineEvent::Kind::Log)
                log.emplace_back(e.text);
            else if (e.kind == EngineEvent::Kind::Notify) {
                notifications.push_back(e);
                notificationArgs.emplace_back(e.args.begin(), e.args.end());
            }
        });
    }

    /// Runs and returns the log lines produced, clearing it.
    std::vector<std::string> take(int blocks = 1) {
        run(blocks);
        auto out = std::move(log);
        log.clear();
        return out;
    }

    /// First box whose text starts with `cls` (searching subpatches too).
    static Box* find(Canvas& c, std::string_view cls, int nth = 0) {
        for (const auto& b : c.boxes()) {
            if (!b->text.empty() && b->text[0].isSymbol() && b->text[0].asSymbol().str() == cls && nth-- == 0)
                return b.get();
        }
        for (const auto& b : c.boxes())
            if (b->child)
                if (auto* f = find(*b->child, cls, nth))
                    return f;
        return nullptr;
    }

    void send(Box* box, int inlet, std::string_view sel, std::vector<Atom> args = {}) {
        REQUIRE(box);
        REQUIRE(box->object);
        rt.engine().sendToObject(box->object->id(), inlet, Symbol(sel), args);
    }

    void dsp(bool on) {
        rt.engine().setDsp(on);
        run(1);
    }
};

} // namespace qure::test
