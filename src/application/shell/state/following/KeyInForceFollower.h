#pragma once

#include <JuceHeader.h>

#include "../../../../platform/pitch/KeyInForce.h"
#include "../../../../platform/score/CurrentScore.h"
#include "../../../../platform/transport/PracticeClock.h"
#include "../../../localisation/NoteNames.h"

// Keeps CurrentTarget::keyFifths on the key of the measure the music is at
// (pitch::keyInForce): the target's measure, or the clock's, or with no score
// the reader's own key. Polled rather than told, because three things move it
// -- the clock, the target from any tool, and the open score -- and a check
// is a few comparisons. The shell declares it after all three.
class KeyInForceFollower final : private juce::Timer
{
  public:
    KeyInForceFollower(
        const transport::PracticeClock& practiceClock,
        const score::CurrentScore& scoreHolder,
        pitch::CurrentTarget& sharedTarget)
        : clock(practiceClock), holder(scoreHolder), target(sharedTarget)
    {
        update();
        startTimerHz(refreshRateHz);
    }

    ~KeyInForceFollower() override
    {
        stopTimer();
    }

    KeyInForceFollower(const KeyInForceFollower&) = delete;
    KeyInForceFollower& operator=(const KeyInForceFollower&) = delete;

    // At once, for a change that should not wait for the next check: a score
    // opening, or the reader's key changing in Settings.
    void update()
    {
        const auto score = holder.score();
        const auto aimingAt = target.target();
        target.keyFifths = pitch::keyInForce(
            score.get(), aimingAt.get(), clock.position().tick, l10n::spelling().keyFifths);
    }

  private:
    static constexpr int refreshRateHz = 15;

    void timerCallback() override
    {
        update();
    }

    const transport::PracticeClock& clock;
    const score::CurrentScore& holder;
    pitch::CurrentTarget& target;
};
