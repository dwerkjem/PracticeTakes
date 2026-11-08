#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"
#include "../../application/tools/ToolComponent.h"
#include "../../platform/evaluation/TakeReview.h"
#include "../../platform/take/TakeRecorder.h"
#include "../../platform/transport/PracticeClock.h"

#include <vector>

// The Take Review tool: the take under review, the part it is judged against,
// a summary, and every result in time order. Choosing a result moves the
// practice clock there, and with it the Score View
// (openspec/changes/take-review/design.md, decision 4).
//
// It reads no audio. What it shows is the recorder's review, the same one the
// Score View marks, so the list and the notation cannot disagree; the rows are
// evaluation::reviewRows, tested without a display.
class TakeReviewComponent final
    : public ToolComponent,
      private take::TakeListener,
      private juce::ListBoxModel
{
  public:
    TakeReviewComponent(take::TakeRecorder& takeRecorder, transport::PracticeClock& practiceClock);
    ~TakeReviewComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

  private:
    static constexpr int headerHeight = 92;
    static constexpr int rowHeight = 24;

    void takesChanged() override;

    // Re-read the review: the choosers, the summary and the rows.
    void refresh();

    int getNumRows() override;
    void paintListBoxItem(
        int rowNumber,
        juce::Graphics& graphics,
        int width,
        int height,
        bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    juce::String getTooltipForRow(int row) override;

    // Move the clock to a row's place, unless a take is recording.
    void goTo(int row);

    // The colour of a row's dot: what kind of result it is.
    [[nodiscard]] juce::Colour colourOf(const evaluation::ReviewRow& row) const;

    [[nodiscard]] AppPalette palette() const;

    take::TakeRecorder& takes;
    transport::PracticeClock& clock;

    std::vector<evaluation::ReviewRow> rows;
    juce::String heading;
    juce::String summary;

    juce::ComboBox takeChooser;
    juce::ComboBox partChooser;
    juce::ListBox list;

    Theme currentTheme = Theme::light;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TakeReviewComponent)
};
