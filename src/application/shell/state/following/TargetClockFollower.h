#pragma once

#include <JuceHeader.h>

#include <functional>
#include <utility>

#include "../../../../platform/pitch/TargetFollowing.h"
#include "../../../../platform/transport/AudibleTick.h"
#include "../../../../platform/transport/PracticeClock.h"

// Hands the practice clock's audible position to the target follower on every
// clock update (openspec/changes/archive/2026-11-09-now-and-next/design.md, decision 5). The shell
// declares it after the clock, so it stops listening before the clock goes.
class TargetClockFollower final : private transport::ClockListener
{
  public:
    TargetClockFollower(
        transport::PracticeClock& practiceClock,
        const score::CurrentScore& scoreHolder,
        pitch::CurrentTarget& sharedTarget,
        std::function<int()> outputLatencyFrames)
        : clock(practiceClock), follower(scoreHolder, sharedTarget),
          latency(std::move(outputLatencyFrames))
    {
        clock.addListener(this);
    }

    ~TargetClockFollower() override
    {
        clock.removeListener(this);
    }

    TargetClockFollower(const TargetClockFollower&) = delete;
    TargetClockFollower& operator=(const TargetClockFollower&) = delete;

  private:
    void clockChanged(const transport::ClockUpdate& update) override
    {
        const transport::ClockPosition& position = update.position;
        const bool playing =
            position.state == transport::ClockState::playing && !position.countingIn;
        const auto table = clock.currentTable();
        const double tick = transport::audibleTick(
            position.tick, playing, clock.timing(), table.get(),
            juce::Time::getMillisecondCounterHiRes(), latency ? latency() : 0);
        static_cast<void>(follower.follow(tick, playing));
    }

    transport::PracticeClock& clock;
    pitch::TargetFollower follower;
    std::function<int()> latency;
};
