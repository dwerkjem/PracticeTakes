#pragma once

#include <JuceHeader.h>

#include "../../../../platform/take/TakeRecorder.h"
#include "../../../../platform/take/TakeStatus.h"
#include "../../../../platform/transport/PracticeClock.h"
#include "../../../localisation/Localisation.h"
#include "../../../localisation/TakeText.h"
#include "../../../theme/AppLookAndFeel.h"
#include "PlaybackMixPanel.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <string_view>

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
    // The width this needs: everything, or without the readout when the
    // window is narrow. 576 and 414 for English at 100%; it grows with the text
    // size and with a language whose words are longer.
    [[nodiscard]] int requiredWidth(bool withReadout)
    {
        constexpr int gap = 4;
        const Widths widths = controlWidths();
        int total = widths.record + widths.play + widths.stop + widths.tempo + widths.click +
                    widths.loop + widths.mix + 6 * gap;
        if (withReadout)
        {
            total += widths.readout + gap;
        }
        return total;
    }

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
        // (openspec/changes/archive/2026-11-08-takes).
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
        playButton.setTooltip(l10n::text("transport.play.tooltip"));
        playButton.onClick = [this] { clock.togglePlay(); };
        addAndMakeVisible(playButton);

        stopButton.setComponentID("transport-stop");
        stopButton.setTooltip(l10n::text("transport.stop.tooltip"));
        stopButton.onClick = [this] { clock.stop(); };
        appearance::setIcon(stopButton, appearance::Icon::stop);
        addAndMakeVisible(stopButton);

        positionLabel.setComponentID("transport-position");
        positionLabel.setJustificationType(juce::Justification::centred);
        positionLabel.setTooltip(l10n::text("transport.position.tooltip"));
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
        tempoSlider.setTooltip(l10n::text("transport.tempo.tooltip"));
        tempoSlider.onValueChange = [this]
        { clock.setScalePercent(static_cast<int>(tempoSlider.getValue())); };
        addAndMakeVisible(tempoSlider);

        clickButton.setComponentID("transport-click");
        clickButton.setClickingTogglesState(true);
        clickButton.setToggleState(clock.metronomeEnabled(), juce::dontSendNotification);
        clickButton.setTooltip(l10n::text("transport.click.tooltip"));
        clickButton.onClick = [this]
        {
            clock.setMetronomeEnabled(clickButton.getToggleState());
            if (onSettingsChanged)
            {
                onSettingsChanged();
            }
        };
        appearance::setIcon(clickButton, appearance::Icon::drum);
        addAndMakeVisible(clickButton);

        loopButton.setComponentID("transport-loop");
        loopButton.setClickingTogglesState(true);
        loopButton.setToggleState(clock.loopEnabled(), juce::dontSendNotification);
        loopButton.setTooltip(l10n::text("transport.loop.tooltip"));
        loopButton.onClick = [this] { clock.setLoopEnabled(loopButton.getToggleState()); };
        appearance::setIcon(loopButton, appearance::Icon::repeat);
        addAndMakeVisible(loopButton);

        // The score's sound, and each part's and hand's
        // (openspec/changes/archive/2026-11-08-score-playback).
        mixButton.setComponentID("transport-mix");
        mixButton.setTooltip(l10n::text("transport.mix.tooltip"));
        mixButton.onClick = [this] { showMix(); };
        appearance::setIcon(mixButton, appearance::Icon::slidersVertical);
        addAndMakeVisible(mixButton);

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
        // Widths for text at 100 percent, grown with the text size.
        auto bounds = getLocalBounds();
        constexpr int gap = 4;
        const Widths widths = controlWidths();
        const bool compact = getWidth() < requiredWidth(true);

        recordButton.setBounds(bounds.removeFromLeft(widths.record));
        bounds.removeFromLeft(gap);
        playButton.setBounds(bounds.removeFromLeft(widths.play));
        bounds.removeFromLeft(gap);
        stopButton.setBounds(bounds.removeFromLeft(widths.stop));
        bounds.removeFromLeft(gap);

        positionLabel.setVisible(!compact);
        if (!compact)
        {
            positionLabel.setBounds(bounds.removeFromLeft(widths.readout));
            bounds.removeFromLeft(gap);
        }

        tempoSlider.setBounds(bounds.removeFromLeft(widths.tempo));
        bounds.removeFromLeft(gap);
        clickButton.setBounds(bounds.removeFromLeft(widths.click));
        bounds.removeFromLeft(gap);
        loopButton.setBounds(bounds.removeFromLeft(widths.loop));
        bounds.removeFromLeft(gap);
        mixButton.setBounds(bounds.removeFromLeft(widths.mix));
    }

  private:
    struct Widths
    {
        int record = 0;
        int play = 0;
        int stop = 0;
        int readout = 0;
        int tempo = 0;
        int click = 0;
        int loop = 0;
        int mix = 0;
    };

    // Each control's width: its English width at 100%, grown with the text
    // size, and grown again by however much longer the language's words are
    // than English's -- so English lays out exactly as it always has. A button
    // that changes its words -- Record and Finish, Play and Pause -- takes the
    // longer, so the strip does not shift as it plays.
    [[nodiscard]] Widths controlWidths()
    {
        const auto px = [this](int base) { return appearance::scaledFor(*this, base); };
        const int height = std::max(1, getHeight() > 0 ? getHeight() : px(30));
        const auto needs =
            [this, height,
             &px](juce::TextButton& button, int base, std::initializer_list<std::string_view> keys)
        {
            const juce::Font font = getLookAndFeel().getTextButtonFont(button, height);
            float growth = 1.0f;
            for (const auto key : keys)
            {
                const float english =
                    juce::GlyphArrangement::getStringWidth(font, l10n::englishText(key));
                const float here = juce::GlyphArrangement::getStringWidth(font, l10n::text(key));
                if (english > 0.0f)
                {
                    growth = std::max(growth, here / english);
                }
            }
            return static_cast<int>(std::ceil(static_cast<float>(px(base)) * growth));
        };

        Widths widths;
        widths.record =
            needs(recordButton, 56, {l10n::key("transport.record"), l10n::key("transport.finish")});
        widths.play =
            needs(playButton, 56, {l10n::key("transport.play"), l10n::key("transport.pause")});
        widths.stop = needs(stopButton, 46, {l10n::key("transport.stop")});
        widths.click = needs(clickButton, 48, {l10n::key("transport.click")});
        widths.loop = needs(loopButton, 46, {l10n::key("transport.loop")});
        widths.mix = needs(mixButton, 44, {l10n::key("transport.mix")});
        widths.tempo = px(92);

        // The readout, grown as "Bar 3, beat 2" is in this language.
        const juce::Font font = positionLabel.getFont();
        const float english =
            juce::GlyphArrangement::getStringWidth(font, l10n::englishText("transport.position"));
        const float here =
            juce::GlyphArrangement::getStringWidth(font, l10n::text("transport.position"));
        const float growth = english > 0.0f ? std::max(1.0f, here / english) : 1.0f;
        widths.readout = static_cast<int>(std::ceil(static_cast<float>(px(158)) * growth));
        return widths;
    }

  public:
    // What the readout says for a position: "Bar 3, beat 2", or the count-in.
    [[nodiscard]] static juce::String describe(const transport::ClockPosition& position)
    {
        if (!position.hasScore)
        {
            return {};
        }
        if (position.countingIn)
        {
            return l10n::format(
                "transport.position.count-in", {{"beat", juce::String(position.beat + 1)}});
        }
        return l10n::format(
            "transport.position",
            {{"bar", juce::String(position.bar + 1)}, {"beat", juce::String(position.beat + 1)}});
    }

    // Open the Mix popup under its button, inside the window so a capture
    // sees it.
    void showMix()
    {
        juce::Component* window = getTopLevelComponent();
        if (window == nullptr)
        {
            return;
        }
        auto panel =
            std::make_unique<PlaybackMixPanel>(clock, onSettingsChanged, &getLookAndFeel());
        const auto area = window->getLocalArea(this, mixButton.getBounds());
        juce::CallOutBox::launchAsynchronously(std::move(panel), area, window);
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
            return l10n::takeStatus(*live);
        }
        if (const take::Take* ended = takes.lastEnded();
            ended != nullptr && ended->id == announcedTake &&
            juce::Time::getMillisecondCounter() < announceUntil)
        {
            return l10n::takeStatus(*ended);
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
        playButton.setButtonText(
            l10n::text(running ? l10n::key("transport.pause") : l10n::key("transport.play")));
        appearance::setIcon(playButton, running ? appearance::Icon::pause : appearance::Icon::play);
        playButton.setTooltip(l10n::text(
            running ? l10n::key("transport.pause.tooltip") : l10n::key("transport.play.tooltip")));
        positionLabel.setText(readout(position), juce::dontSendNotification);
        positionLabel.setTooltip(positionLabel.getText());

        // Record, or Finish while a take records, red so it cannot be missed.
        const bool recording = takes.isRecording();
        const std::optional<std::string> blocked = takes.whyCannotStart();
        recordButton.setButtonText(
            l10n::text(recording ? l10n::key("transport.finish") : l10n::key("transport.record")));
        appearance::setIcon(
            recordButton, recording ? appearance::Icon::check : appearance::Icon::circleDot);
        recordButton.setEnabled(recording || !blocked.has_value());
        recordButton.setTooltip(
            recording ? l10n::text("transport.finish.tooltip")
            : blocked.has_value()
                ? l10n::format("transport.record.unavailable", {{"reason", l10n::text(*blocked)}})
            : running ? l10n::text("transport.record.while-playing.tooltip")
                      : l10n::text("transport.record.tooltip"));
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
    juce::TextButton recordButton{l10n::text("transport.record")};
    juce::TextButton playButton{l10n::text("transport.play")};
    juce::TextButton stopButton{l10n::text("transport.stop")};
    juce::Label positionLabel;
    juce::Slider tempoSlider;
    juce::TextButton clickButton{l10n::text("transport.click")};
    juce::TextButton loopButton{l10n::text("transport.loop")};
    juce::TextButton mixButton{l10n::text("transport.mix")};
};
