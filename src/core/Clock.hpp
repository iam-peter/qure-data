#pragma once

#include <functional>

namespace qure {

class Scheduler;

/// A one-shot timer in logical time (milliseconds), like Pd's t_clock.
///
/// Clocks are created on the control thread (usually in an object's
/// constructor) but only ever set, unset and fired on the engine thread.
/// Setting and unsetting never allocate: the scheduler keeps set clocks in
/// an intrusive list sorted by due time.
class Clock {
public:
    Clock(Scheduler& scheduler, std::function<void()> callback);
    ~Clock();
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;

    /// Fires `ms` milliseconds after the current logical time.
    void delay(double ms);
    /// Fires at an absolute logical time.
    void setAt(double timeMs);
    void unset();

    [[nodiscard]] bool isSet() const noexcept { return set_; }
    [[nodiscard]] double dueTime() const noexcept { return time_; }

private:
    friend class Scheduler;

    Scheduler& scheduler_;
    std::function<void()> callback_;
    Clock* prev_ = nullptr;
    Clock* next_ = nullptr;
    double time_ = 0.0;
    bool set_ = false;
};

class Scheduler {
public:
    /// Current logical time in milliseconds. While a clock callback runs
    /// this is that clock's due time, so re-scheduling from a callback is
    /// drift free (sample accurate metronomes).
    [[nodiscard]] double now() const noexcept { return now_; }

    /// Fires every clock due strictly before `endMs`, in order, then sets
    /// the logical time to `endMs`.
    void advanceTo(double endMs);

    /// Logical time elapsed since `since`, in ms.
    [[nodiscard]] double since(double since) const noexcept { return now_ - since; }

private:
    friend class Clock;
    void insert(Clock& c);
    void remove(Clock& c);

    Clock* head_ = nullptr;
    double now_ = 0.0;
};

} // namespace qure
