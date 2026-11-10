#pragma once

#include <JuceHeader.h>

#include "../../application/theme/MiniStaff.h"
#include "../../application/theme/MusicFont.h"
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
#include <functional>
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
        pitch::CurrentTarget& applicationTarget,
        MidiInputService& midiInputService,
        std::function<pitch::ZoneLimits()> zoneLimitsSource);
    ~NoteViewComponent() override;

    void paint(juce::Graphics& graphics) override;

    // A click on a key, or on a line or space of the staff, makes that note
    // the target; hovering shows which.
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
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
    [[nodiscard]] pitch::StaffClef staffClef() const;

    // Where each part is drawn: empty for a part not shown (the compact view
    // draws the keyboard alone).
    struct Areas
    {
        juce::Rectangle<int> reading;
        juce::Rectangle<int> staff;
        juce::Rectangle<int> keyboard;
        juce::Rectangle<int> trail;
    };
    [[nodiscard]] Areas areas() const;
    [[nodiscard]] MiniStaffStyle staffStyle() const;

    // The staff's head positions after the clef: the target's and the sung
    // note's.
    [[nodiscard]] float targetHeadX(juce::Rectangle<int> staff) const;
    [[nodiscard]] float sungHeadX(juce::Rectangle<int> staff) const;

    // The key, or the staff's note, under a point; nothing elsewhere.
    [[nodiscard]] std::optional<int> keyUnder(juce::Point<float> point) const;
    [[nodiscard]] std::optional<score::Pitch> staffNoteUnder(juce::Point<float> point) const;
    void updateHover(juce::Point<float> point);

    void paintReading(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintStaff(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintTrail(juce::Graphics& graphics, juce::Rectangle<int> area) const;
    void paintKeyboard(juce::Graphics& graphics, juce::Rectangle<int> area) const;

    // A fractional pitch's x within `area`, across the window's keys.
    [[nodiscard]] float xOf(double midi, juce::Rectangle<int> area) const;

    [[nodiscard]] AppPalette palette() const;

    // The zone the note being sung is in, and the acceptable range either
    // side of the target, in cents.
    [[nodiscard]] std::optional<pitch::IntonationZone> zone() const;
    [[nodiscard]] double toleranceCents() const;
    [[nodiscard]] pitch::ZoneLimits zoneLimits() const;

    SharedPitchAnalysis& pitchAnalysis;
    const pitch::ReferencePitch& referencePitch;
    pitch::CurrentTarget& target;
    MidiInputService& midi;

    // The singer's near-perfect and acceptable ranges, as Settings → Practice
    // sets them.
    std::function<pitch::ZoneLimits()> limits;

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

    // What a click would choose, shown faintly: a key, or a note on the staff.
    std::optional<int> hoverKey;
    std::optional<score::Pitch> hoverStaffNote;

    // The music font, for the clef: one copy however many tools draw staves.
    juce::SharedResourcePointer<MusicFont> musicFont;
    Theme currentTheme = Theme::light;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteViewComponent)
};
