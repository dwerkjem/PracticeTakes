#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"
#include "../../application/tools/ToolComponent.h"
#include "../../platform/score/CurrentScore.h"
#include "../../platform/score/PartTimeline.h"
#include "../../platform/transport/PracticeClock.h"
#include "NowNextModel.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

// Now & Next: the note being sung now, large, with its time running down, and
// the notes coming (openspec/changes/archive/2026-11-09-now-and-next). What can be tested without
// a display is in NowNextModel.h, score/PartTimeline.h and
// transport/AudibleTick.h.
class NowNextComponent final : public ToolComponent, private juce::Timer
{
  public:
    NowNextComponent(
        const score::CurrentScore& currentScore,
        transport::PracticeClock& practiceClock,
        std::function<int()> outputLatencyFrames);
    ~NowNextComponent() override;

    void paint(juce::Graphics& graphics) override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

    [[nodiscard]] std::optional<ToolSettingsPayload> captureSettings() const override;
    void applySettings(const ToolSettingsPayload& payload) override;
    [[nodiscard]] std::vector<MenuEntry> optionsMenuEntries() override;

    // "high-contrast" turns high contrast on; "default" off.
    [[nodiscard]] bool showView(const juce::String& viewName) override;

    // The payload version, which the catalog's settingsVersion must equal.
    static constexpr int settingsVersion = 1;

  private:
    static constexpr int refreshRateHz = 60;

    // The colours for the current mode.
    struct Colours
    {
        juce::Colour background;
        juce::Colour card;
        juce::Colour foreground;
        juce::Colour muted;
        juce::Colour accent;
        juce::Colour outline;
    };

    void timerCallback() override;

    // Rebuild the timeline when the score or the followed part changes.
    void refreshTimeline();

    [[nodiscard]] Colours colours() const;
    [[nodiscard]] juce::String nameOf(const score::TimelineItem& item) const;
    [[nodiscard]] juce::String beatsText(const score::TimelineItem& item) const;

    void paintNow(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintNext(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintCompact(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintStaff(
        juce::Graphics& graphics,
        juce::Rectangle<float> area,
        const score::TimelineItem& item) const;
    void paintMessage(juce::Graphics& graphics, const juce::String& text) const;

    const score::CurrentScore& scoreHolder;
    transport::PracticeClock& clock;
    std::function<int()> latency;

    // The part followed: none for the target's part (decision 4), or one
    // chosen by its id.
    std::optional<std::string> chosenPartId;
    std::size_t lookAhead = nownext::defaultLookAhead;
    bool highContrast = false;

    std::shared_ptr<const score::Score> shownScore;
    std::optional<std::string> shownPartChoice;
    std::size_t part = 0;
    score::PartTimeline timeline;

    // The latest reading.
    double tick = 0.0;
    bool playing = false;
    bool countingIn = false;
    nownext::View view;
    std::shared_ptr<const transport::ClockTable> table;

    Theme currentTheme = Theme::light;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NowNextComponent)
};
