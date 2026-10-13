#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"
#include "../../application/tools/ToolComponent.h"
#include "../../platform/score/CurrentScore.h"

#include "TargetNoteSelection.h"

#include <memory>

// The first tool to read a score.
//
// It shows one note: the first pitched note of the current score, with enough
// around it to find the note in the file and play it. That is deliberately
// small. Engraving is #32's, navigating is #33's, and this exists to prove the
// score model can be consumed at all -- which is why what it draws is text and
// not a staff: invariant 10 keeps every coordinate out of the model, so a staff
// position is something a renderer computes rather than something a score can
// be asked for.
//
// Everything worth asserting about it lives in `selectTargetNote`, which has no
// JUCE dependency and is unit tested. What is left here is a timer, a paint
// routine, and a palette.
class TargetNoteComponent final : public ToolComponent, private juce::Timer
{
  public:
    explicit TargetNoteComponent(const score::CurrentScore& sharedCurrentScore);
    ~TargetNoteComponent() override;

    void paint(juce::Graphics& graphics) override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

  private:
    // Twice a second. The thing being watched for is a user opening a file,
    // which is the least urgent update in the application; a faster timer would
    // buy nothing and a push would need machinery the shared-audio precedent
    // deliberately avoided.
    static constexpr int refreshRateHz = 2;

    void timerCallback() override;

    // Re-read the holder, and re-render only if it is a score this has not seen.
    void refreshFromCurrentScore();

    void paintTarget(
        juce::Graphics& graphics,
        juce::Rectangle<int> bounds,
        const TargetNote& target) const;
    void paintMessage(
        juce::Graphics& graphics,
        juce::Rectangle<int> bounds,
        const juce::String& heading,
        const juce::String& detail) const;

    [[nodiscard]] AppPalette palette() const;

    const score::CurrentScore& currentScore;

    // The score this last rendered. Held rather than just read because it is
    // what makes a change detectable by pointer comparison -- and because
    // holding it keeps it alive, so what is on screen cannot be freed by
    // someone opening a different file mid-paint.
    std::shared_ptr<const score::Score> renderedScore;

    TargetNoteSelection selection;

    Theme currentTheme = Theme::light;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TargetNoteComponent)
};
