#pragma once

#include <JuceHeader.h>

#include "../../application/theme/Theme.h"
#include "../../application/tools/ToolComponent.h"
#include "../../platform/audio/SharedPitchAnalysis.h"
#include "../../platform/midi/MidiInputService.h"
#include "../../platform/pitch/CurrentTarget.h"
#include "../../platform/pitch/PitchTracker.h"
#include "../../platform/pitch/ReferencePitch.h"
#include "../../platform/pitch/TargetChoice.h"
#include "NoteViewModel.h"

#include <array>
#include <optional>
#include <set>

// The Note View: the note being sung or played on a piano keyboard and a staff,
// against the target, with a trail of the last few seconds
// (openspec/changes/archive/2026-11-08-note-view). What can be tested without a display is in
// NoteViewModel.h.
class NoteViewComponent final
    : public ToolComponent,
      private juce::Timer,
      private MidiInputService::Consumer
{
  public:
    enum class ClefChoice
    {
        automatic,
        treble,
        bass,
        trebleOctaveDown
    };

    NoteViewComponent(
        SharedPitchAnalysis& sharedPitchAnalysis,
        const pitch::ReferencePitch& applicationReferencePitch,
        const pitch::CurrentTarget& applicationTarget,
        MidiInputService& midiInputService);
    ~NoteViewComponent() override;

    void paint(juce::Graphics& graphics) override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

    [[nodiscard]] std::optional<ToolSettingsPayload> captureSettings() const override;
    void applySettings(const ToolSettingsPayload& payload) override;
    [[nodiscard]] std::vector<MenuEntry> optionsMenuEntries() override;

    // The payload version, which the catalog's settingsVersion must equal.
    static constexpr int settingsVersion = 1;

  private:
    static constexpr int refreshRateHz = 30;

    void timerCallback() override;
    void readKeyboard();

    [[nodiscard]] pitch::StaffClef clefFor(int referenceMidi) const;

    void paintReading(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintStaff(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintTrail(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintKeyboard(juce::Graphics& graphics, juce::Rectangle<int> area) const;

    // A fractional pitch's x within `area`, across the window's keys.
    [[nodiscard]] float xOf(double midi, juce::Rectangle<int> area) const;

    [[nodiscard]] AppPalette palette() const;

    SharedPitchAnalysis& pitchAnalysis;
    const pitch::ReferencePitch& referencePitch;
    const pitch::CurrentTarget& target;
    MidiInputService& midi;

    PitchTracker tracker;
    notes::Trail trail;
    notes::KeyWindow window;
    std::set<int> heldKeys;
    std::array<midi::MidiEvent, 128> keyBuffer{};

    // The latest reading: the note shown and how far off it is.
    std::optional<int> currentNote;
    double currentCents = 0.0;
    std::optional<double> currentPitch;

    ClefChoice clef = ClefChoice::automatic;
    Theme currentTheme = Theme::light;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteViewComponent)
};
