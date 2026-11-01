#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"
#include "../../application/tools/ToolComponent.h"
#include "../../platform/score/CurrentScore.h"

#include "ScoreGlyphPainter.h"
#include "engraving/ScoreLayout.h"

#include <memory>

// The tool that draws the open score as notation.
//
// It is the first thing in the application to put music on a staff, and it is
// the consumer `src/features/score/engraving` exists for. What is worth
// asserting about it -- which line a note is on, how wide a bar is, which
// accidental is drawn -- is all in that layer and unit tested without a
// display. What is left here is polling the current score, scrolling, zoom, and
// handing glyphs to `ScoreGlyphPainter`.
//
// The score it reads arrives through `ToolServices`, exactly as the Target Note
// tool's does: a tool reaches application state through that bundle and never
// back into the shell.
class ScoreViewComponent final : public ToolComponent, private juce::Timer
{
  public:
    explicit ScoreViewComponent(const score::CurrentScore& sharedCurrentScore);
    ~ScoreViewComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

    [[nodiscard]] std::vector<MenuEntry> optionsMenuEntries() override;

  private:
    // How large one staff space is drawn, per zoom step. Three steps rather
    // than a continuous control: a score is readable or it is not, and a
    // slider would be a second thing to persist for no gain.
    enum class Zoom
    {
        small,
        medium,
        large
    };

    // The scrolling canvas. A separate component because a `juce::Viewport`
    // scrolls what it contains, and the staff has to be taller than the tool
    // for scrolling to mean anything.
    class Canvas final : public juce::Component
    {
      public:
        explicit Canvas(ScoreViewComponent& ownerToUse);

        void paint(juce::Graphics& graphics) override;

      private:
        ScoreViewComponent& owner;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Canvas)
    };

    // Twice a second, matching the Target Note tool. What is being watched for
    // is a user opening a file, which is the least urgent update in the
    // application.
    static constexpr int refreshRateHz = 2;

    void timerCallback() override;

    // Re-read the holder, and lay the score out again only if it is one this
    // has not seen.
    void refreshFromCurrentScore();

    // Lay the current score out for the width and zoom the tool now has.
    void relayout();

    void setZoom(Zoom zoom);

    [[nodiscard]] double pixelsPerSpace() const;

    [[nodiscard]] AppPalette palette() const;

    void paintMessage(
        juce::Graphics& graphics,
        juce::Rectangle<int> bounds,
        const juce::String& heading,
        const juce::String& detail) const;

    [[nodiscard]] juce::String noticeText() const;

    const score::CurrentScore& currentScore;

    // The score this last laid out. Held rather than just read because it is
    // what makes a change detectable by pointer comparison, and because holding
    // it keeps it alive -- so what is on screen cannot be freed by someone
    // opening a different file mid-paint. See score::CurrentScore.
    std::shared_ptr<const score::Score> renderedScore;

    engraving::ScoreLayout layout;

    Canvas canvas{*this};
    juce::Viewport viewport;

    Zoom currentZoom = Zoom::medium;
    Theme currentTheme = Theme::light;

    // The width, in pixels, the layout was last computed for. Re-laying out on
    // every paint would be wasteful and would make the layout depend on paint
    // order; re-laying out on every `resized` would redo it during a drag.
    int layoutWidth = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScoreViewComponent)
};
