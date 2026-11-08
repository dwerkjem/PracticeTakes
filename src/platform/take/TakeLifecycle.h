#pragma once

#include <string>
#include <utility>

// The states a take passes through, and the only ways between them (no JUCE
// dependency). openspec/changes/takes/design.md, decision 3.
//
// A take is live while it is ready, counting in, performing or paused, and then
// ends exactly once, in one of four ways. "Stopped early" is the incomplete
// take #40 asks to tell apart from a completed one; "failed" keeps its reason.
namespace take
{
enum class TakeState
{
    ready,
    countingIn,
    performing,
    paused,

    completed,
    stoppedEarly,
    cancelled,
    failed
};

// What happened, as the recorder hears it from the clock and the reader.
enum class TakeEvent
{
    countInStarted,
    performanceStarted,
    paused,
    reachedEnd,
    stopped,
    cancelled,
    failed
};

[[nodiscard]] constexpr bool isLive(TakeState state) noexcept
{
    return state == TakeState::ready || state == TakeState::countingIn ||
           state == TakeState::performing || state == TakeState::paused;
}

[[nodiscard]] constexpr bool isEnded(TakeState state) noexcept
{
    return !isLive(state);
}

// A take that ended with something worth keeping: everything but cancelled.
[[nodiscard]] constexpr bool isKept(TakeState state) noexcept
{
    return isEnded(state) && state != TakeState::cancelled;
}

class TakeLifecycle
{
  public:
    [[nodiscard]] TakeState state() const noexcept
    {
        return current;
    }

    // Why a failed take failed. Empty for any other state.
    [[nodiscard]] const std::string& failureReason() const noexcept
    {
        return reason;
    }

    // Apply an event. True when the state changed; false when the event does
    // not apply in the current state, which leaves everything as it was.
    bool apply(TakeEvent event, std::string failure = {})
    {
        const TakeState next = transition(current, event, performed);
        if (next == current)
        {
            return false;
        }
        current = next;
        performed = performed || next == TakeState::performing;
        if (next == TakeState::failed)
        {
            reason = std::move(failure);
        }
        return true;
    }

    // Whether the performance has begun: the count-in ended at least once.
    [[nodiscard]] bool hasPerformed() const noexcept
    {
        return performed;
    }

    // Where `event` takes `from`; `from` itself when it does not apply.
    // `performed` is whether the performance began, which decides whether a
    // stop keeps the take.
    [[nodiscard]] static constexpr TakeState
    transition(TakeState from, TakeEvent event, bool performed) noexcept
    {
        if (isEnded(from))
        {
            return from;
        }

        switch (event)
        {
        case TakeEvent::cancelled:
            return TakeState::cancelled;

        case TakeEvent::failed:
            return TakeState::failed;

        case TakeEvent::countInStarted:
            // From a pause too: resuming counts in again.
            return from == TakeState::performing ? from : TakeState::countingIn;

        case TakeEvent::performanceStarted:
            return TakeState::performing;

        case TakeEvent::paused:
            return from == TakeState::ready ? from : TakeState::paused;

        case TakeEvent::reachedEnd:
            // Only a take that was played can reach the end of it.
            return performed && (from == TakeState::performing || from == TakeState::paused)
                       ? TakeState::completed
                       : from;

        case TakeEvent::stopped:
            // Stopped before a note could have been played is nothing to keep.
            return performed ? TakeState::stoppedEarly : TakeState::cancelled;
        }

        return from;
    }

  private:
    TakeState current = TakeState::ready;
    bool performed = false;
    std::string reason;
};
} // namespace take
