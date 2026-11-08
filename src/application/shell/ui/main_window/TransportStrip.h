#pragma once

#include <JuceHeader.h>

#include "../../../../platform/take/TakeRecorder.h"
#include "../../../../platform/take/TakeStatus.h"
#include "../../../../platform/transport/PracticeClock.h"

#include <functional>

// The practice clock's controls, in the main window's control strip: play or
// pause, stop, where the clock is, the tempo scale, the metronome and the loop
// (openspec/changes/archive/2026-11-08-practice-clock, design.md, decision 1).
//
// Shown only once a score is open, because a clock with no score has nothing
// to play. It reads the clock through a ClockListener like any tool would, and
// drives it through the clock's own methods -- the strip holds no state of its
// own beyond what the controls show.
class TransportStrip final
    : public juce::Component,
      private transport::ClockListener,
      private take::TakeListener,
      private juce::Timer
{
  public:
    // Widths the control strip lays this out at: everything, or without the
    // readout when the window is narrow.
    static constexpr int fullWidth = 552;
    static constexpr int compactWidth = 378;

    // How long the readout says how the last take ended before going back to
    // where the clock is.
    static constexpr int lastTakeShownMilliseconds = 6000;

    // `settingsHandler` is called when a control changes something that is
    // saved -- the metronome switch -- so the shell can persist it.
    TransportStrip(
        transport::PracticeClock& practiceClock,
        take::TakeRecorder& takeRecorder,
        std::function<void()> settingsHandler)
        : clock(practiceClock), takes(takeRecorder), onSettingsChanged(std::move(settingsHandler))
    {
        // Records a take, counting in first; while one records, finishes it
        // (openspec/changes/takes).
        recordButton.setComponentID("transport-record");
        recordButton.onClick = [this]
        {
            if (takes.isRecording())
            {
                takes.stop();
            }
            else
            {
                static_cast<void>(takes.start());
            }
            refresh(clock.position());
        };
        addAndMakeVisible(recordButton);

        // Component ids let the development-only test control channel click
        // these by name.
        playButton.setComponentID("transport-play");
        playButton.setTooltip("Play from here, counting in first (Settings > Practice)");
        playButton.onClick = [this] { clock.togglePlay(); };
        addAndMakeVisible(playButton);

        stopButton.setComponentID("transport-stop");
        stopButton.setTooltip("Stop, and go back to the start of the loop or the score");
        stopButton.onClick = [this] { clock.stop(); };
        addAndMakeVisible(stopButton);

        positionLabel.setComponentID("transport-position");
        positionLabel.setJustificationType(juce::Justification::centred);
        positionLabel.setTooltip("Where the practice clock is in the score");
        addAndMakeVisible(positionLabel);

        tempoSlider.setComponentID("transport-tempo");
        tempoSlider.setSliderStyle(juce::Slider::IncDecButtons);
        tempoSlider.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
        tempoSlider.setRange(
            transport::minimumScalePercent, transport::maximumScalePercent,
            transport::scaleStepPercent);
        tempoSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 52, 24);
        tempoSlider.setTextValueSuffix("%");
        tempoSlider.setValue(clock.scalePercent(), juce::dontSendNotification);
        tempoSlider.setTooltip("Tempo, as a percentage of the score's own");
        tempoSlider.onValueChange = [this]
        { clock.setScalePercent(static_cast<int>(tempoSlider.getValue())); };
        addAndMakeVisible(tempoSlider);

        clickButton.setComponentID("transport-click");
        clickButton.setClickingTogglesState(true);
        clickButton.setToggleState(clock.metronomeEnabled(), juce::dontSendNotification);
        clickButton.setTooltip("Sound the metronome on every beat");
        clickButton.onClick = [this]
        {
            clock.setMetronomeEnabled(clickButton.getToggleState());
            if (onSettingsChanged)
            {
                onSettingsChanged();
            }
        };
        addAndMakeVisible(clickButton);

        loopButton.setComponentID("transport-loop");
        loopButton.setClickingTogglesState(true);
        loopButton.setToggleState(clock.loopEnabled(), juce::dontSendNotification);
        loopButton.setTooltip("Loop the region, or the whole score when none is set");
        loopButton.onClick = [this] { clock.setLoopEnabled(loopButton.getToggleState()); };
        addAndMakeVisible(loopButton);

        clock.addListener(this);
        takes.addListener(this);
        refresh(clock.position());

        // Whether a take can start depends on the microphone and the keyboard,
        // which change without the clock moving.
        startTimerHz(4);
    }

    ~TransportStrip() override
    {
        stopTimer();
        takes.removeListener(this);
        clock.removeListener(this);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        constexpr int gap = 4;
        const bool compact = getWidth() < fullWidth;

        recordButton.setBounds(bounds.removeFromLeft(58));
        bounds.removeFromLeft(gap);
        playButton.setBounds(bounds.removeFromLeft(58));
        bounds.removeFromLeft(gap);
        stopButton.setBounds(bounds.removeFromLeft(48));
        bounds.removeFromLeft(gap);

        positionLabel.setVisible(!compact);
        if (!compact)
        {
            positionLabel.setBounds(bounds.removeFromLeft(170));
            bounds.removeFromLeft(gap);
        }

        tempoSlider.setBounds(bounds.removeFromLeft(92));
        bounds.removeFromLeft(gap);
        clickButton.setBounds(bounds.removeFromLeft(50));
        bounds.removeFromLeft(gap);
        loopButton.setBounds(bounds.removeFromLeft(48));
    }

    // What the readout says for a position: "Bar 3, beat 2", or the count-in.
    [[nodiscard]] static juce::String describe(const transport::ClockPosition& position)
    {
        if (!position.hasScore)
        {
            return {};
        }
        if (position.countingIn)
        {
            return "Count-in " + juce::String(position.beat + 1);
        }
        return "Bar " + juce::String(position.bar + 1) + ", beat " +
               juce::String(position.beat + 1);
    }

  private:
    void clockChanged(const transport::ClockUpdate& update) override
    {
        refresh(update.position);
    }

    void takesChanged() override
    {
        // A take that has just ended is announced for a while.
        if (const take::Take* ended = takes.lastEnded();
            ended != nullptr && ended->id != announcedTake && !takes.isRecording())
        {
            announcedTake = ended->id;
            announceUntil = juce::Time::getMillisecondCounter() +
                            static_cast<juce::uint32>(lastTakeShownMilliseconds);
        }
        refresh(clock.position());
    }

    void timerCallback() override
    {
        refresh(clock.position());
    }

    // The readout: the take while one records, how the last one ended for a
    // few seconds after, and otherwise where the clock is.
    [[nodiscard]] juce::String readout(const transport::ClockPosition& position) const
    {
        if (const take::Take* live = takes.current())
        {
            return juce::String(take::describe(*live));
        }
        if (const take::Take* ended = takes.lastEnded();
            ended != nullptr && ended->id == announcedTake &&
            juce::Time::getMillisecondCounter() < announceUntil)
        {
            return juce::String(take::describe(*ended));
        }
        return describe(position);
    }

    void refresh(const transport::ClockPosition& position)
    {
        if (isVisible() != position.hasScore)
        {
            // The control strip gives the menus less room while this shows, so
            // it lays itself out again whenever this appears or goes.
            setVisible(position.hasScore);
            if (auto* parent = getParentComponent())
            {
                parent->resized();
            }
        }

        const bool running = position.state == transport::ClockState::playing;
        playButton.setButtonText(running ? "Pause" : "Play");
        playButton.setTooltip(
            running ? "Pause, holding the position"
                    : "Play from here, counting in first (Settings > Practice)");
        positionLabel.setText(readout(position), juce::dontSendNotification);
        positionLabel.setTooltip(positionLabel.getText());

        // Record, or Finish while a take records, red so it cannot be missed.
        const bool recording = takes.isRecording();
        const std::optional<std::string> blocked = takes.whyCannotStart();
        recordButton.setButtonText(recording ? "Finish" : "Record");
        recordButton.setEnabled(recording || !blocked.has_value());
        recordButton.setTooltip(
            recording ? "Finish the take, keeping what was played"
            : blocked.has_value()
                ? "Recording is unavailable: " + juce::String(*blocked)
                : "Record a take from the microphone and keyboard, counting in first");
        recordButton.setColour(
            juce::TextButton::buttonColourId,
            recording ? juce::Colour::fromRGB(196, 43, 43)
                      : getLookAndFeel().findColour(juce::TextButton::buttonColourId));
        recordButton.setColour(
            juce::TextButton::textColourOffId,
            recording ? juce::Colours::white
                      : getLookAndFeel().findColour(juce::TextButton::textColourOffId));

        // What a take is measured against cannot change while one records
        // (takes design.md decision 4).
        tempoSlider.setEnabled(!clock.isLocked());
        loopButton.setEnabled(!clock.isLocked());

        if (!tempoSlider.isMouseButtonDown())
        {
            tempoSlider.setValue(clock.scalePercent(), juce::dontSendNotification);
        }
        clickButton.setToggleState(clock.metronomeEnabled(), juce::dontSendNotification);
        loopButton.setToggleState(clock.loopEnabled(), juce::dontSendNotification);
    }

    transport::PracticeClock& clock;
    take::TakeRecorder& takes;
    std::function<void()> onSettingsChanged;
    std::uint64_t announcedTake = 0;
    juce::uint32 announceUntil = 0;
    juce::TextButton recordButton{"Record"};
    juce::TextButton playButton{"Play"};
    juce::TextButton stopButton{"Stop"};
    juce::Label positionLabel;
    juce::Slider tempoSlider;
    juce::TextButton clickButton{"Click"};
    juce::TextButton loopButton{"Loop"};
};
