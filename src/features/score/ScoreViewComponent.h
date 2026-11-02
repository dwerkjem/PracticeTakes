#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"
#include "../../application/tools/ToolComponent.h"
#include "../../platform/score/CurrentScore.h"

#include "ScoreGlyphPainter.h"
#include "ScoreViewSettings.h"
#include "engraving/Pagination.h"
#include "engraving/ScoreLayout.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

// The tool that draws the open score as notation.
//
// It is the first thing in the application to put music on a staff, and it is
// the consumer `src/features/score/engraving` exists for. What is worth
// asserting about it -- which line a note is on, how wide a bar is, which
// accidental is drawn, where a page breaks -- is all in that layer and unit
// tested without a display. What is left here is polling the current score,
// the three views and the staff size, keeping the place across a change of
// either, and handing glyphs to `ScoreGlyphPainter`.
//
// The views and the size are per instance and remembered; see
// openspec/changes/archive/2026-11-02-score-view-modes/design.md.
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
    bool keyPressed(const juce::KeyPress& key) override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

    [[nodiscard]] std::optional<ToolSettingsPayload> captureSettings() const override;
    void applySettings(const ToolSettingsPayload& payload) override;

    // "page", "vertical" or "horizontal", for the capture harness.
    [[nodiscard]] bool showView(const juce::String& view) override;

    [[nodiscard]] std::vector<MenuEntry> optionsMenuEntries() override;

  private:
    // What is drawn inside the viewport. A separate component because a
    // `juce::Viewport` scrolls what it contains: a tall column in Vertical
    // scroll, a long system in Horizontal, and one page in Page view, scrolled
    // only when the page is taller than the panel.
    class Canvas final : public juce::Component
    {
      public:
        explicit Canvas(ScoreViewComponent& ownerToUse);

        void paint(juce::Graphics& graphics) override;

        // A click on the music gives the Score View the keyboard, so page
        // turns and Ctrl/Cmd +/- work without a separate focus step.
        void mouseDown(const juce::MouseEvent& event) override;

      private:
        ScoreViewComponent& owner;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Canvas)
    };

    // What a layout was computed for. Laid out again only when one of these
    // changes: re-laying out on every paint would make the layout depend on
    // paint order, and on every `resized` would redo it during a drag.
    struct LayoutKey
    {
        int width = 0;
        int height = 0;
        score_view::ViewMode view = score_view::ViewMode::page;
        std::size_t sizeStep = score_view::defaultSizeStep;

        bool operator==(const LayoutKey&) const = default;
    };

    // Twice a second, matching the Target Note tool. What is being watched for
    // is a user opening a file, which is the least urgent update in the
    // application.
    static constexpr int refreshRateHz = 2;

    void timerCallback() override;

    // Re-read the holder, and lay the score out again only if it is one this
    // has not seen.
    void refreshFromCurrentScore();

    // Lay the current score out for the view, size and room the tool now has,
    // then bring the measure `keep` back into view. `force` lays out even when
    // nothing in the key changed -- a new score.
    void relayout(std::optional<std::size_t> keep, bool force = false);

    void setView(score_view::ViewMode view);
    void setSizeStep(std::size_t step);
    void turnToPage(std::size_t page);

    // The first measure index on screen, and putting one back on screen.
    [[nodiscard]] std::optional<std::size_t> measureInView() const;
    void revealMeasure(std::size_t measure);

    // The page, in canvas pixels, and the area inside its margins where the
    // systems go.
    [[nodiscard]] juce::Rectangle<float> pageBounds() const;
    [[nodiscard]] juce::Rectangle<float> pageContent() const;

    void updateControls();

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

    // Page view only: the layout's systems cut into pages, and which is shown.
    std::vector<engraving::Page> pages;
    std::size_t currentPage = 0;

    // Owned here rather than shared across the application: parsing it is
    // once per tool opened, and the score view is the only thing that draws
    // notation. Declared before `canvas`, which borrows it to paint.
    MusicFont musicFont;

    Canvas canvas{*this};
    juce::Viewport viewport;

    // On the title strip: shrink, the size, enlarge; and in Page view the page
    // turns either side of "Page n of m".
    juce::TextButton shrinkButton{juce::String::fromUTF8("\xe2\x88\x92")};
    juce::Label sizeLabel;
    juce::TextButton enlargeButton{"+"};
    juce::TextButton previousPageButton{juce::String::fromUTF8("\xe2\x80\xb9")};
    juce::Label pageLabel;
    juce::TextButton nextPageButton{juce::String::fromUTF8("\xe2\x80\xba")};

    // Where the score's title goes: the title strip less the controls.
    juce::Rectangle<int> titleTextBounds;

    score_view::Settings settings;
    Theme currentTheme = Theme::light;

    LayoutKey laidOutFor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScoreViewComponent)
};
