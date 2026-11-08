#pragma once

#include <JuceHeader.h>

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
class TransportStrip final : public juce::Component, private transport::ClockListener
{
  public:
    // Widths the control strip lays this out at: everything, or without the
    // position readout when the window is narrow.
    static constexpr int fullWidth = 472;
    static constexpr int compactWidth = 342;

    // `settingsHandler` is called when a control changes something that is
    // saved -- the metronome switch -- so the shell can persist it.
    TransportStrip(transport::PracticeClock& practiceClock, std::function<void()> settingsHandler)
        : clock(practiceClock), onSettingsChanged(std::move(settingsHandler))
    {
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
        refresh(clock.position());
    }

    ~TransportStrip() override
    {
        clock.removeListener(this);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        constexpr int gap = 4;
        const bool compact = getWidth() < fullWidth;

        playButton.setBounds(bounds.removeFromLeft(64));
        bounds.removeFromLeft(gap);
        stopButton.setBounds(bounds.removeFromLeft(54));
        bounds.removeFromLeft(gap);

        positionLabel.setVisible(!compact);
        if (!compact)
        {
            positionLabel.setBounds(bounds.removeFromLeft(126));
            bounds.removeFromLeft(gap);
        }

        tempoSlider.setBounds(bounds.removeFromLeft(100));
        bounds.removeFromLeft(gap);
        clickButton.setBounds(bounds.removeFromLeft(56));
        bounds.removeFromLeft(gap);
        loopButton.setBounds(bounds.removeFromLeft(52));
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
        positionLabel.setText(describe(position), juce::dontSendNotification);

        if (!tempoSlider.isMouseButtonDown())
        {
            tempoSlider.setValue(clock.scalePercent(), juce::dontSendNotification);
        }
        clickButton.setToggleState(clock.metronomeEnabled(), juce::dontSendNotification);
        loopButton.setToggleState(clock.loopEnabled(), juce::dontSendNotification);
    }

    transport::PracticeClock& clock;
    std::function<void()> onSettingsChanged;
    juce::TextButton playButton{"Play"};
    juce::TextButton stopButton{"Stop"};
    juce::Label positionLabel;
    juce::Slider tempoSlider;
    juce::TextButton clickButton{"Click"};
    juce::TextButton loopButton{"Loop"};
};
