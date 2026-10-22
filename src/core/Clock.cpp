#include "Clock.hpp"

#include <cassert>

namespace qure {

Clock::Clock(Scheduler& scheduler, std::function<void()> callback)
    : scheduler_(scheduler), callback_(std::move(callback)) {}

Clock::~Clock() {
    // Objects unset their clocks on deactivation, which happens on the engine
    // thread before the object can be destroyed.
    assert(!set_ && "clock destroyed while scheduled");
}

void Clock::delay(double ms) { setAt(scheduler_.now() + (ms > 0.0 ? ms : 0.0)); }

void Clock::setAt(double timeMs) {
    if (set_)
        scheduler_.remove(*this);
    time_ = timeMs;
    scheduler_.insert(*this);
}

void Clock::unset() {
    if (set_)
        scheduler_.remove(*this);
}

void Scheduler::insert(Clock& c) {
    // Clocks with equal times fire in the order they were set (FIFO), which
    // is what Pd does and what [delay 0] chains rely on.
    Clock* prev = nullptr;
    Clock* cur = head_;
    while (cur && cur->time_ <= c.time_) {
        prev = cur;
        cur = cur->next_;
    }
    c.prev_ = prev;
    c.next_ = cur;
    if (prev)
        prev->next_ = &c;
    else
        head_ = &c;
    if (cur)
        cur->prev_ = &c;
    c.set_ = true;
}

void Scheduler::remove(Clock& c) {
    if (c.prev_)
        c.prev_->next_ = c.next_;
    else
        head_ = c.next_;
    if (c.next_)
        c.next_->prev_ = c.prev_;
    c.prev_ = c.next_ = nullptr;
    c.set_ = false;
}

void Scheduler::advanceTo(double endMs) {
    while (head_ && head_->time_ < endMs) {
        Clock* c = head_;
        if (c->time_ > now_)
            now_ = c->time_;
        remove(*c);
        c->callback_();
    }
    now_ = endMs;
}

} // namespace qure
