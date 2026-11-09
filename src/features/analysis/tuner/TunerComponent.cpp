#include "TunerComponent.h"

#include "../../../platform/pitch/TargetReading.h"
#include "TargetPickerPanel.h"

#include "TunerSettingsCodec.h"

#include "application/theme/AppLookAndFeel.h"

#include <array>

// The display-mode label and chooser as one component, so a docked panel adopts
// the pair with a single reparent and lays out one thing.
class TunerComponent::ModeChooser final : public juce::Component
{
  public:
    ModeChooser(juce::Label& labelToUse, juce::ComboBox& boxToUse)
        : label(labelToUse), box(boxToUse)
    {
        addAndMakeVisible(label);
        addAndMakeVisible(box);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        // The label takes what it needs and the box the rest, so this reads the
        // same on a header line as it did on a row of its own.
        label.setBounds(bounds.removeFromLeft(juce::jmin(88, bounds.getWidth() / 2)));
        bounds.removeFromLeft(6);
        box.setBounds(bounds);
    }

  private:
    juce::Label& label;
    juce::ComboBox& box;
};

#include <algorithm>
#include <cmath>
#include <limits>

// The thresholds that decide a glide from a misdetection, the smoothing, and
// the note lock all live in PitchTracker now. noteNameForMidi comes from the
// same header, so this file and TunerDrawing.cpp cannot disagree about the name
// of a note -- they held separate identical copies before.

//==============================================================================
TunerComponent::TunerComponent(
    AudioInputService& sharedAudioInputService,
    SharedPitchAnalysis& sharedPitchAnalysis,
    const pitch::ReferencePitch& applicationReferencePitch,
    pitch::CurrentTarget& applicationTarget,
    std::function<pitch::ZoneLimits()> zoneLimitsSource)
    : audioInputService(sharedAudioInputService), pitchAnalysis(sharedPitchAnalysis),
      referencePitch(applicationReferencePitch), target(applicationTarget),
      limits(std::move(zoneLimitsSource))
{
    setOpaque(true);

    const auto configureLabel = [this](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(label);
    };

    configureLabel(displayModeLabel, "Display mode");
    configureLabel(easingLabel, "Pitch easing");
    configureLabel(averagingLabel, "Average window");
    configureLabel(thresholdLabel, "Note switch");
    configureLabel(dropoutLabel, "Dropout hold");
    configureLabel(durationLabel, "Graph duration");

    displayModeBox.addItem("Graph", static_cast<int>(DisplayMode::graph));
    displayModeBox.addItem("Bar", static_cast<int>(DisplayMode::bar));
    displayModeBox.addItem("Meter", static_cast<int>(DisplayMode::meter));
    displayModeBox.setSelectedId(AppDefaults::Tuner::displayMode, juce::dontSendNotification);
    displayModeBox.onChange = [this]
    {
        updateGraphControlAvailability();
        resized();
        repaint();
    };

    // The advanced settings live behind the panel's "..." menu rather than a
    // button of their own. Most sessions never open them, and a permanent row
    // for them was a row the display did not get.
    modeChooser = std::make_unique<ModeChooser>(displayModeLabel, displayModeBox);
    addAndMakeVisible(*modeChooser);

    // Range, step and starting value from the tuner's settings schema, which
    // the codec that restores these sliders validates against too.
    const auto configureFromSchema =
        [this](
            juce::Slider& slider, double AppDefaults::TunerSettings::*member,
            const juce::String& suffix)
    {
        const AppDefaults::TunerNumberField& field = AppDefaults::tunerField(member);
        configureSlider(
            slider, field.minimum, field.maximum, field.step, AppDefaults::tunerDefaults().*member,
            suffix);
    };
    configureFromSchema(easingSlider, &AppDefaults::TunerSettings::easing, "");
    configureFromSchema(averagingSlider, &AppDefaults::TunerSettings::averaging, " samples");
    configureFromSchema(thresholdSlider, &AppDefaults::TunerSettings::noteSwitchSemitones, " st");
    configureFromSchema(dropoutSlider, &AppDefaults::TunerSettings::dropoutFrames, " frames");
    configureFromSchema(durationSlider, &AppDefaults::TunerSettings::graphDurationSeconds, " sec");

    clearGraphButton.onClick = [this]
    {
        graphHistory.clear();
        repaint(displayBounds);
    };
    addAndMakeVisible(clearGraphButton);

    applyThemeToControls();
    updateAdvancedSettingsVisibility();

    // State only: the tuner reads its pitch from SharedPitchAnalysis, so it
    // needs to hear about mute and disconnection but takes no sample slot.
    audioInputService.addStateListener(this);
    startTimerHz(analysisRefreshRateHz);
}

TunerComponent::~TunerComponent()
{
    stopTimer();
    audioInputService.removeStateListener(this);
}

bool TunerComponent::showView(const juce::String& view)
{
    // The tuner's own vocabulary. A screenshot of the graph says nothing about
    // the bar or the meter, so each is a surface of its own.
    const auto selectMode = [this](DisplayMode mode)
    { displayModeBox.setSelectedId(static_cast<int>(mode), juce::sendNotificationSync); };

    if (view == "graph")
    {
        selectMode(DisplayMode::graph);

        return true;
    }

    if (view == "bar")
    {
        selectMode(DisplayMode::bar);

        return true;
    }

    if (view == "meter")
    {
        selectMode(DisplayMode::meter);

        return true;
    }

    if (view == "target-picker")
    {
        // The graph under the picker, and the picker opened once the tuner is
        // on screen (target-note-picker).
        selectMode(DisplayMode::graph);
        const juce::Component::SafePointer<TunerComponent> safeThis(this);
        juce::Timer::callAfterDelay(
            600,
            [safeThis]
            {
                if (safeThis != nullptr && safeThis->isShowing())
                {
                    safeThis->openTargetPicker();
                }
            });

        return true;
    }

    if (view == "advanced")
    {
        // The display too, not only the panel. A state has to describe what is
        // on screen rather than what changed about it: without this, the
        // advanced surface inherited whichever display the previous state left
        // selected, so the same capture showed the graph or the meter
        // depending on what came before it in the run.
        selectMode(DisplayMode::graph);
        areAdvancedSettingsExpanded = true;
        updateAdvancedSettingsVisibility();
        resized();

        return true;
    }

    return false;
}

void TunerComponent::resetToDefaults()
{
    displayModeBox.setSelectedId(AppDefaults::Tuner::displayMode, juce::sendNotificationSync);
    easingSlider.setValue(AppDefaults::Tuner::easing);
    averagingSlider.setValue(AppDefaults::Tuner::averaging);
    thresholdSlider.setValue(AppDefaults::Tuner::noteSwitchSemitones);
    dropoutSlider.setValue(AppDefaults::Tuner::dropoutFrames);
    durationSlider.setValue(AppDefaults::Tuner::graphDurationSeconds);
    areAdvancedSettingsExpanded = false;
    graphHistory.clear();
    resetPitchTracking();
    updateAdvancedSettingsVisibility();
    resized();
    repaint();
}

void TunerComponent::applyPreset(AppDefaults::Preset preset)
{
    applySettings(AppDefaults::tunerPreset(preset));
}

void TunerComponent::applySettings(const AppDefaults::TunerSettings& settings)
{
    displayModeBox.setSelectedId(settings.displayMode, juce::sendNotificationSync);
    easingSlider.setValue(settings.easing);
    averagingSlider.setValue(settings.averaging);
    thresholdSlider.setValue(settings.noteSwitchSemitones);
    dropoutSlider.setValue(settings.dropoutFrames);
    durationSlider.setValue(settings.graphDurationSeconds);
    graphHistory.clear();
    resetPitchTracking();
    repaint();
}

AppDefaults::TunerSettings TunerComponent::settings() const
{
    return {displayModeBox.getSelectedId(), easingSlider.getValue(),  averagingSlider.getValue(),
            thresholdSlider.getValue(),     dropoutSlider.getValue(), durationSlider.getValue()};
}

std::optional<ToolSettingsPayload> TunerComponent::captureSettings() const
{
    return TunerSettingsCodec::encode(settings());
}

void TunerComponent::applySettings(const ToolSettingsPayload& payload)
{
    // A payload that does not parse leaves the tuner as it is rather than
    // resetting it: the workspace that carried it is refused upstream, and
    // wiping a working tuner on top of that would lose the user's settings
    // twice over.
    if (const auto decoded = TunerSettingsCodec::decode(payload); decoded.has_value())
    {
        applySettings(*decoded);
    }
}

//==============================================================================
// Appearance and control setup

void TunerComponent::setTheme(Theme theme)
{
    if (currentTheme == theme)
    {
        return;
    }

    currentTheme = theme;
    applyThemeToControls();
    repaint();
}

void TunerComponent::applyThemeToControls()
{
    const auto palette = tunerPaletteFor(currentTheme);

    for (auto* label :
         {&displayModeLabel, &easingLabel, &averagingLabel, &thresholdLabel, &dropoutLabel,
          &durationLabel})
    {
        label->setColour(juce::Label::textColourId, palette.muted);
    }

    for (auto* button : {&clearGraphButton})
    {
        button->setColour(juce::TextButton::buttonColourId, palette.control);
        button->setColour(juce::TextButton::buttonOnColourId, palette.accent.withAlpha(0.75f));
        button->setColour(juce::TextButton::textColourOffId, palette.foreground);
        button->setColour(juce::TextButton::textColourOnId, palette.foreground);
    }

    displayModeBox.setColour(juce::ComboBox::backgroundColourId, palette.control);
    displayModeBox.setColour(juce::ComboBox::textColourId, palette.foreground);
    displayModeBox.setColour(juce::ComboBox::outlineColourId, palette.outline);
    displayModeBox.setColour(juce::ComboBox::arrowColourId, palette.foreground);

    for (auto* slider :
         {&easingSlider, &averagingSlider, &thresholdSlider, &dropoutSlider, &durationSlider})
    {
        slider->setColour(juce::Slider::backgroundColourId, palette.panel);
        slider->setColour(juce::Slider::trackColourId, palette.accent.withAlpha(0.75f));
        slider->setColour(juce::Slider::thumbColourId, palette.accent);
        slider->setColour(juce::Slider::textBoxTextColourId, palette.foreground);
        slider->setColour(juce::Slider::textBoxBackgroundColourId, palette.control);
        slider->setColour(juce::Slider::textBoxOutlineColourId, palette.outline);
    }

    sendLookAndFeelChange();
}

void TunerComponent::configureSlider(
    juce::Slider& slider,
    double minimum,
    double maximum,
    double interval,
    double initialValue,
    const juce::String& suffix)
{
    slider.setRange(minimum, maximum, interval);
    slider.setValue(initialValue, juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 82, 22);
    slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);
}

void TunerComponent::updateAdvancedSettingsVisibility()
{
    for (auto* component : std::array<juce::Component*, 10>{
             &easingLabel, &easingSlider, &averagingLabel, &averagingSlider, &thresholdLabel,
             &thresholdSlider, &dropoutLabel, &dropoutSlider, &durationLabel, &durationSlider})
    {
        component->setVisible(areAdvancedSettingsExpanded);
    }

    updateGraphControlAvailability();
}

void TunerComponent::updateGraphControlAvailability()
{
    const auto isGraphSelected =
        displayModeBox.getSelectedId() == static_cast<int>(DisplayMode::graph);

    durationLabel.setEnabled(isGraphSelected);
    durationSlider.setEnabled(isGraphSelected);
    clearGraphButton.setVisible(areAdvancedSettingsExpanded && isGraphSelected);
}

int TunerComponent::controlAreaHeight() const
{
    // A stacked row is taller than an inline one, so the room reserved here has
    // to be asked the same question resized() asks -- otherwise the display is
    // sized for five short rows and five tall ones are drawn under it.
    const auto rowHeight = advancedRowsStack() ? stackedAdvancedRowHeight : advancedRowHeight;
    const auto expandedRowsHeight = 5 * rowHeight + 8 + 36;

    // Nothing but the advanced rows, and only while they are open. The mode
    // chooser is on the header line when the tuner is docked, and the tuner
    // adds a row for it only when nobody adopted it -- which resized() works
    // out, since it is the only place that knows.
    return (areAdvancedSettingsExpanded ? expandedRowsHeight + 8 : 0) +
           (isModeChooserAdopted() ? 0 : modeChooserHeight + 8);
}

bool TunerComponent::advancedRowsStack() const
{
    return getWidth() < stackedAdvancedRowsBelowWidth;
}

bool TunerComponent::isModeChooserAdopted() const
{
    // Adopted means some other component -- a docked panel's header -- took it
    // as a child. Floating, nobody does, so the tuner shows it itself.
    return modeChooser != nullptr && modeChooser->getParentComponent() != nullptr &&
           modeChooser->getParentComponent() != this;
}

void TunerComponent::placeModeChooser(juce::Rectangle<int> area)
{
    if (modeChooser == nullptr)
    {
        return;
    }

    addAndMakeVisible(*modeChooser);
    modeChooser->setBounds(area);
}

juce::Component* TunerComponent::headerControl()
{
    return modeChooser.get();
}

std::vector<ToolComponent::MenuEntry> TunerComponent::optionsMenuEntries()
{
    // The mode chooser lives on the header line -- which a pane too small to
    // show a header line does not have (DockedToolPanel hides it below the
    // compact threshold), and a floating window's own title bar has no room
    // for it either. Right-click already reaches this menu at every size and
    // presentation; without these three, that reach did not include switching
    // the one thing a tuner offers more than one view of.
    const auto currentMode = static_cast<DisplayMode>(displayModeBox.getSelectedId());
    const auto selectMode = [this](DisplayMode mode)
    { displayModeBox.setSelectedId(static_cast<int>(mode), juce::sendNotificationSync); };

    return {
        {"Graph", [selectMode] { selectMode(DisplayMode::graph); },
         currentMode == DisplayMode::graph},
        {"Bar", [selectMode] { selectMode(DisplayMode::bar); }, currentMode == DisplayMode::bar},
        {"Meter", [selectMode] { selectMode(DisplayMode::meter); },
         currentMode == DisplayMode::meter},
        {areAdvancedSettingsExpanded ? "Hide advanced settings" : "Advanced settings",
         [this]
         {
             areAdvancedSettingsExpanded = !areAdvancedSettingsExpanded;
             updateAdvancedSettingsVisibility();
             resized();
             repaint();
         }},
        {juce::String::fromUTF8("Choose target\xe2\x80\xa6"), [this] { openTargetPicker(); }}};
}

std::optional<int> TunerComponent::steadyNote() const
{
    return steadyReading.steadyNote(juce::Time::getMillisecondCounterHiRes());
}

juce::Rectangle<int> TunerComponent::targetLineBounds() const
{
    return getLocalBounds().reduced(18).removeFromTop(targetHeight);
}

void TunerComponent::openTargetPicker()
{
    juce::Component* window = getTopLevelComponent();
    if (window == nullptr)
    {
        return;
    }
    const juce::Component::SafePointer<TunerComponent> safeThis(this);
    auto panel = std::make_unique<TargetPickerPanel>(
        target, [safeThis]() -> std::optional<int>
        { return safeThis != nullptr ? safeThis->steadyNote() : std::nullopt; }, currentTheme);
    juce::CallOutBox::launchAsynchronously(
        std::move(panel), window->getLocalArea(this, targetLineBounds()), window);
}

void TunerComponent::mouseUp(const juce::MouseEvent& event)
{
    if (!event.mouseWasDraggedSinceMouseDown() && targetLineBounds().contains(event.getPosition()))
    {
        openTargetPicker();
    }
}

void TunerComponent::mouseMove(const juce::MouseEvent& event)
{
    setMouseCursor(
        targetLineBounds().contains(event.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                         : juce::MouseCursor::NormalCursor);
}

juce::String TunerComponent::statusText() const
{
    if (audioErrorMessage.isNotEmpty())
    {
        return audioErrorMessage;
    }

    // Not "whenever there is no signal right now" -- a brief silent gap
    // between two notes clears `hasSignal` too, and mid-practice that is not
    // an invitation to start, it is the ordinary shape of playing something.
    // The prompt belongs to a graph that has nothing in it yet; once there is
    // a real reading in the history, a pause reads as a pause, not as an
    // empty tool asking to be used.
    if (!hasSignal && !hasGraphHistory())
    {
        return "Play or sing a sustained note";
    }

    // Blank rather than a placeholder: the status line still holds its row
    // either way (statusHeight is reserved unconditionally), and a brief gap
    // between notes has nothing worth reporting -- not silence to name, not
    // an error, just a moment where the reading is not current.
    if (!hasSignal)
    {
        return {};
    }

    // One space, not the three the design's HTML carries -- a browser collapses
    // runs of whitespace and renders exactly one, which is what the reference
    // actually shows. JUCE draws all three.
    const auto centsSign = displayedCents > 0.0 ? "+" : "";
    return juce::String(displayedFrequency, 1) + " Hz " + centsSign +
           juce::String(displayedCents, 1) + " cents";
}

namespace
{
[[nodiscard]] std::optional<double>
heardPitch(bool hasSignal, bool hasLockedNote, int lockedNote, double cents)
{
    if (!hasSignal || !hasLockedNote)
    {
        return std::nullopt;
    }
    return static_cast<double>(lockedNote) + (cents / 100.0);
}
} // namespace

juce::String TunerComponent::targetText() const
{
    const std::shared_ptr<const pitch::Target> aimingAt = target.target();
    if (aimingAt == nullptr)
    {
        return juce::String::fromUTF8(target.whyNone().c_str());
    }
    return juce::String::fromUTF8(
        pitch::readAgainstTarget(
            *aimingAt, heardPitch(hasSignal, hasLockedMidiNote, lockedMidiNote, displayedCents),
            zoneLimits().nearPerfectCents)
            .text.c_str());
}

bool TunerComponent::onTarget() const
{
    const std::shared_ptr<const pitch::Target> aimingAt = target.target();
    return aimingAt != nullptr &&
           pitch::readAgainstTarget(
               *aimingAt, heardPitch(hasSignal, hasLockedMidiNote, lockedMidiNote, displayedCents),
               zoneLimits().nearPerfectCents)
                   .standing == pitch::TargetStanding::onIt;
}

bool TunerComponent::hasGraphHistory() const
{
    return std::any_of(
        graphHistory.begin(), graphHistory.end(),
        [](double value) { return std::isfinite(value); });
}

//==============================================================================
// Audio capture

void TunerComponent::audioInputAboutToStart(double sampleRate, int inputChannels)
{
    juce::ignoreUnused(sampleRate, inputChannels);
}

void TunerComponent::audioInputStopped() {}

void TunerComponent::audioInputStateChanged(AudioInputService::InputState state)
{
    resetPitchTracking();

    switch (state)
    {
    case AudioInputService::InputState::disconnected:
        audioErrorMessage = "Microphone disconnected.";
        break;
    case AudioInputService::InputState::opening:
        audioErrorMessage = "Waiting for the microphone.";
        break;
    case AudioInputService::InputState::muted:
        audioErrorMessage = "Microphone muted.";
        break;
    case AudioInputService::InputState::clipping:
        audioErrorMessage = "Microphone input is clipping.";
        break;
    case AudioInputService::InputState::active:
        audioErrorMessage.clear();
        break;
    }

    repaint();
}

//==============================================================================
// Pitch analysis

void TunerComponent::timerCallback()
{
    // SharedPitchAnalysis is the one registered consumer for pitch detection;
    // this tool is a state-only listener and has no FIFO of its own.
    const auto analysis = pitchAnalysis.latestResult();
    inputLevel = analysis.inputLevel;

    const auto update =
        analysis.frequency > 0.0 ? pitchTracker.detected(analysis.frequency, trackerSettings())
                                 : pitchTracker.missing(trackerSettings());
    applyTrackerUpdate(update);

    repaint();
}

PitchTracker::Settings TunerComponent::trackerSettings() const
{
    // Read straight off the sliders each frame, so a mid-note adjustment takes
    // effect immediately rather than at the next note.
    return {
        easingSlider.getValue(), averagingSlider.getValue(), thresholdSlider.getValue(),
        dropoutSlider.getValue(), referencePitch.hertz()};
}

void TunerComponent::applyTrackerUpdate(const PitchTracker::Update& update)
{
    hasSignal = update.hasSignal;
    displayedFrequency = update.displayedFrequency;
    displayedCents = update.displayedCents;
    lockedMidiNote = update.displayedMidiNote;
    hasLockedMidiNote = update.hasDisplayedNote;
    displayedNote = update.hasDisplayedNote
                        ? juce::String(noteNameForMidi(update.displayedMidiNote))
                        : juce::String("--");
    steadyReading.update(
        update.hasSignal && update.hasDisplayedNote ? std::optional<int>(update.displayedMidiNote)
                                                    : std::nullopt,
        update.displayedCents, juce::Time::getMillisecondCounterHiRes());

    // Every frame contributes a point. A NaN is a deliberate gap -- silence, or
    // a reading rejected as a suspected harmonic -- and the graph draws a break
    // rather than a line across it.
    addHistoryPoint(update.historyMidiNote);
}

void TunerComponent::addHistoryPoint(double midiPitch)
{
    graphHistory.push_back(midiPitch);

    const auto desiredPointCount = juce::jlimit(
        100, maximumGraphPoints,
        static_cast<int>(durationSlider.getValue() * analysisRefreshRateHz));

    const auto excessPointCount = static_cast<int>(graphHistory.size()) - desiredPointCount;
    if (excessPointCount > 0)
    {
        graphHistory.erase(graphHistory.begin(), graphHistory.begin() + excessPointCount);
    }
}

void TunerComponent::resetPitchTracking()
{
    // Two resets that used to be one. The tracker forgets what it has heard;
    // the component clears what it is showing. They were entangled before, so
    // neither could be exercised without the other.
    pitchTracker.reset();

    hasLockedMidiNote = false;
    hasSignal = false;
    displayedFrequency = 0.0;
    displayedCents = 0.0;
    displayedNote = "--";
}
