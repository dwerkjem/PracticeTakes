#pragma once

#include <JuceHeader.h>

#include <memory>

#include "../../../application/theme/Theme.h"
#include "../../../application/tools/ToolComponent.h"
#include "../../../platform/score/CurrentScore.h"
#include "../../../platform/score/engraving/ScoreLayout.h"

// The score, drawn as staff notation.
//
// A multi-staff part is drawn as one braced grand staff -- which is the point
// of the tool for a pianist, and the reason it exists before any of the
// practice features that will sit on top of it.
//
// **This class makes no layout decision.** Every position it draws comes from
// `score::engraving::layOutScore`, which is JUCE-free and unit tested; what is
// here is a timer, a viewport, a scale, and a paint routine that multiplies
// staff spaces by one number. That split is deliberate: a tool's `Component` is
// outside `PracticeTakesTests` by design, so the untested surface is kept as
// thin as it can be made. If you find yourself computing an x here, it belongs
// in the engraving layer instead.
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
    // The page itself, scrolled inside a viewport. Separated from the tool so
    // that scrolling is JUCE's job rather than an offset this has to maintain.
    class Page final : public juce::Component
    {
      public:
        explicit Page(ScoreViewComponent& ownerView);

        void paint(juce::Graphics& graphics) override;

      private:
        ScoreViewComponent& owner;
    };

    // Twice a second, matching the Target Note tool. What is being watched for
    // is a user opening a file -- the least urgent update in the application.
    static constexpr int refreshRateHz = 2;

    // Default drawing scale, in pixels per staff space. Everything on the page
    // is a multiple of this and nothing else.
    static constexpr float defaultStaffSpacePixels = 7.0f;

    void timerCallback() override;

    // Re-read the holder; re-lay out only for a score this has not seen.
    void refreshFromCurrentScore();

    // Lay out for the current width, and resize the page to match.
    void relayout();

    void paintPage(juce::Graphics& graphics) const;
    void paintStaffLines(
        juce::Graphics& graphics,
        const score::engraving::SystemLayout& system,
        const score::engraving::StaffLayout& staff) const;
    void paintEmptyState(juce::Graphics& graphics, juce::Rectangle<int> bounds) const;

    // Staff spaces to pixels. The only conversion in the tool, which is what
    // keeps the layer below it scale-independent.
    [[nodiscard]] float toPixels(double staffSpaces) const noexcept;

    // Vertical pixel position of a staff step within a staff of a system.
    //
    // A double rather than an int because a stem does not have to end on a
    // whole step -- in a beamed group it ends wherever the beam is.
    [[nodiscard]] float yFor(
        const score::engraving::SystemLayout& system,
        const score::engraving::StaffLayout& staff,
        double staffStep) const noexcept;

    [[nodiscard]] AppPalette palette() const;

    const score::CurrentScore& currentScore;

    // The score this was last laid out for. Pointer identity is a sufficient
    // change check -- holding the old score is what stops its address being
    // reused -- which is finding 2 of score-model-findings.md, confirmed.
    std::shared_ptr<const score::Score> laidOutScore;

    score::engraving::ScoreLayout layout;

    juce::Viewport viewport;
    Page page;

    float staffSpacePixels = defaultStaffSpacePixels;
    Theme currentTheme = Theme::light;

    // The width the layout was computed for, so a resize that does not change
    // it does not redo the work.
    int laidOutWidth = 0;

    juce::Font musicFont;
};
