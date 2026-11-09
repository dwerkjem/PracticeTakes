#pragma once

#include "../../application/localisation/Localisation.h"
#include <functional>

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"
#include "../../application/tools/ToolComponent.h"
#include "../../platform/pitch/CurrentTarget.h"
#include "../../platform/score/CurrentScore.h"
#include "../../platform/take/TakeRecorder.h"
#include "../../platform/transport/PlayheadSmoother.h"
#include "../../platform/transport/PracticeClock.h"

#include "ScoreGlyphPainter.h"
#include "ScoreViewSettings.h"
#include "engraving/Pagination.h"
#include "engraving/ReviewMarks.h"
#include "engraving/ScoreCursor.h"
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
// back into the shell. So does the practice clock, which it draws a cursor for,
// marks the sounding and upcoming notes of, and follows while it plays
// (openspec/changes/archive/2026-11-08-score-cursor). Every rule the cursor follows is in
// engraving/ScoreCursor.h; this draws it and scrolls.
//
// The takes arrive the same way, and the take under review is marked on its
// notes -- right, wrong, missed, early, late, extra -- while its score is the
// one shown (openspec/changes/archive/2026-11-08-take-review, decision 3).
// Where each mark goes is engraving/ReviewMarks.h; this draws them.
class ScoreViewComponent final
    : public ToolComponent,
      private juce::Timer,
      private transport::ClockListener,
      private take::TakeListener
{
  public:
    ScoreViewComponent(
        const score::CurrentScore& sharedCurrentScore,
        transport::PracticeClock& practiceClock,
        take::TakeRecorder& takeRecorder,
        pitch::CurrentTarget& applicationTarget,
        std::function<void()> showImportSummary = {});
    ~ScoreViewComponent() override;

    void paint(juce::Graphics& graphics) override;

    // "N import diagnostics" on the title strip is a link to the import
    // summary (#350).
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
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

        // A click on a note makes it the target; a double-click moves the
        // practice clock to the onset under it (score-cursor decision 6, now on
        // the double-click so one click can choose). A drag does neither: it
        // is kept for choosing a loop region.
        void mouseUp(const juce::MouseEvent& event) override;
        void mouseDoubleClick(const juce::MouseEvent& event) override;

      private:
        ScoreViewComponent& owner;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Canvas)
    };

    // A viewport that says when it was scrolled by the reader rather than by
    // the view itself, which is what suspends following (decision 5).
    class WatchedViewport final : public juce::Viewport
    {
      public:
        explicit WatchedViewport(ScoreViewComponent& ownerToUse) : owner(ownerToUse) {}

        void visibleAreaChanged(const juce::Rectangle<int>& area) override;

      private:
        ScoreViewComponent& owner;
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
        std::vector<engraving::PartKey> hiddenParts;
        score_view::PageFit pageFit = score_view::PageFit::width;

        bool operator==(const LayoutKey&) const = default;
    };

    // Twice a second, matching the Target Note tool. What is being watched for
    // is a user opening a file, which is the least urgent update in the
    // application.
    static constexpr int refreshRateHz = 2;

    void timerCallback() override;

    // The clock moved, changed state, or jumped: move the cursor and the
    // marks, and follow it.
    void clockChanged(const transport::ClockUpdate& update) override;

    // Recompute where the cursor is and which notes are marked, for the
    // position last heard and the layout now on screen, repainting the systems
    // that changed.
    void updateCursor();

    // A take was reviewed, or the review changed: mark it again.
    void takesChanged() override;

    // Place the review's marks on the layout now on screen, or none when the
    // review is of another score or the marks are turned off.
    void updateReview();
    void setShowReview(bool show);

    // Keep the cursor's system in view: a page turn, a system step or a
    // screen's move, never a crawl (decision 5).
    void follow();

    // The reader moved the view while the clock played.
    void suspendFollowing();

    // Scroll to (x, y) over the next few frames rather than at once.
    void glideTo(int x, int y);

    // Every frame the display draws: the playhead moved on while the clock
    // plays, and a glide stepped along.
    void frameDrawn();

    [[nodiscard]] static double secondsNow()
    {
        return juce::Time::getMillisecondCounterHiRes() / 1000.0;
    }
    void resumeFollowing();

    // Scroll or turn without that counting as the reader doing it.
    void scrollProgrammatically(int x, int y);

    // Where a system is drawn on the canvas: its origin (the top line of its
    // first staff, and its left edge), the pixels per staff space it is drawn
    // at, and the rectangle of its staves. Nothing for a system not drawn --
    // one on another page.
    struct SystemOnCanvas
    {
        double originX = 0.0;
        double originY = 0.0;
        double pixelsPerSpace = 1.0;
        juce::Rectangle<int> bounds;
    };
    [[nodiscard]] std::optional<SystemOnCanvas> systemOnCanvas(std::size_t index) const;

    // The cursor band and the note marks on one system, over its notation.
    void
    paintCursorOn(juce::Graphics& graphics, std::size_t index, const SystemOnCanvas& where) const;

    // The target's note outlined, when it is a note of this score.
    void
    paintTargetOn(juce::Graphics& graphics, std::size_t index, const SystemOnCanvas& where) const;

    // The review's marks on one system, over the cursor.
    void
    paintReviewOn(juce::Graphics& graphics, std::size_t index, const SystemOnCanvas& where) const;

    void seekToCanvasPoint(juce::Point<float> point);

    // A right-click: aim at the note under the pointer, or move the clock
    // there (target-note-picker decision 6).
    // The note under a point on the canvas: its whole chord, which of the
    // chord's notes the point is on, and where it is ("Voice, bar 3").
    struct NoteUnder
    {
        std::vector<score::Note> chord;
        std::size_t clicked = 0;
        std::string source;
        std::size_t partIndex = 0;
        score::NoteRef ref;
    };
    [[nodiscard]] std::optional<NoteUnder> noteUnder(juce::Point<float> point) const;

    // Makes the note under `point` the target, if there is one.
    void aimAtNoteUnder(juce::Point<float> point);

    void showNoteMenu(juce::Point<float> point);
    void showGoToBar();
    void goToBar(const juce::String& text);
    void setPageFit(score_view::PageFit fit);
    void setLookAhead(int onsets);
    void setFollow(bool shouldFollow);

    // Re-read the holder, and lay the score out again only if it is one this
    // has not seen.
    void refreshFromCurrentScore();

    // Lay the current score out for the view, size and room the tool now has,
    // then bring the measure `keep` back into view. `force` lays out even when
    // nothing in the key changed -- a new score.
    void relayout(std::optional<std::size_t> keep, bool force = false);

    void setView(score_view::ViewMode view);
    void setSizeStep(std::size_t step);

    // Hide a part that is shown, or show one that is hidden, keeping the bar
    // in view (score-part-selection design.md decision 7).
    void togglePart(const engraving::PartKey& part);

    // The "Parts" submenu, or nothing when the open score has one drawable
    // part and so nothing to choose between (decision 5).
    [[nodiscard]] std::optional<MenuEntry> partsMenuEntry();
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

    // A line of the strip under the score: what it says, and the identifier
    // a dismissal is remembered by. The missing music font cannot be
    // dismissed, since it explains a page with no symbols on it.
    struct ShownNotice
    {
        std::string code;
        juce::String text;
        bool dismissable = true;
    };

    [[nodiscard]] std::vector<ShownNotice> shownNotices() const;
    [[nodiscard]] juce::String noticeText() const;

    // The strip is reserved whether or not anything is in it, so opening a
    // score does not move the staff -- until the reader dismisses a notice.
    // From then it is there only while something undismissed is. Decided when
    // the score or the dismissals change, never on a resize, so a page that
    // becomes oversize as the panel shrinks cannot flap it.
    [[nodiscard]] bool wantsNoticeStrip() const;
    void updateNoticeStrip();
    void dismissNotices();
    void showDismissedNotices();

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

    transport::PracticeClock& clock;

    // The clock as last heard, where its cursor is on the layout, and which
    // notes are marked. `cursor` is empty with no score or nothing laid out.
    transport::ClockPosition clockPosition;
    std::optional<engraving::CursorPlace> cursor;
    engraving::Targets targets;

    take::TakeRecorder& takes;
    pitch::CurrentTarget& target;

    // The target last outlined, to repaint when another tool moves it.
    std::shared_ptr<const pitch::Target> outlinedTarget;

    // The take under review's marks on the current layout; empty when none
    // are drawn.
    engraving::ReviewMarks reviewMarks;

    // Following is on (settings.follow) but the reader has scrolled away while
    // the clock plays. Cleared by Follow, or by the clock starting again.
    bool followSuspended = false;

    // Set while the view scrolls or lays itself out, so the viewport's report
    // of that is not taken for the reader scrolling.
    bool scrollingProgrammatically = false;

    Canvas canvas{*this};
    WatchedViewport viewport{*this};

    // On the title strip: shrink, the size, enlarge; and in Page view the page
    // turns either side of "Page n of m".
    juce::TextButton shrinkButton{juce::String::fromUTF8("\xe2\x88\x92")};
    juce::Label sizeLabel;
    juce::TextButton enlargeButton{"+"};
    juce::TextButton previousPageButton{juce::String::fromUTF8("\xe2\x80\xb9")};
    juce::Label pageLabel;
    juce::TextButton nextPageButton{juce::String::fromUTF8("\xe2\x80\xba")};

    // Shown while following is suspended and the clock plays.
    juce::TextButton followButton{l10n::text("score.view.follow")};

    // Closes the notice strip, beside its text.
    juce::TextButton dismissNoticesButton;
    bool noticeStripReserved = true;
    juce::Rectangle<int> noticeStripBounds;

    // Opens the open score's import summary; empty does nothing (#350).
    std::function<void()> onShowImportSummary;

    // Where "N import diagnostics" was last drawn, empty when it was not, and
    // whether the mouse is over it.
    juce::Rectangle<int> diagnosticsLink;
    bool diagnosticsLinkHovered = false;

    // Where the score's title goes: the title strip less the controls.
    juce::Rectangle<int> titleTextBounds;

    score_view::Settings settings;
    Theme currentTheme = Theme::light;

    LayoutKey laidOutFor;

    // Where the clock is between its reports, and the scroll a follow is
    // gliding towards.
    transport::PlayheadSmoother playhead;
    struct Glide
    {
        juce::Point<int> from;
        juce::Point<int> to;
        double started = 0.0;
    };
    std::optional<Glide> glide;

    // Last, so it stops before anything it calls is destroyed.
    juce::VBlankAttachment frames{this, [this] { frameDrawn(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScoreViewComponent)
};
