#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../../platform/audio/AudioInputService.h"
#include "../../platform/audio/LevelMeter.h"
#include "../theme/ThemeType.h"

#include <functional>
#include <vector>

// The microphone's level as a bar, with a line at the take's threshold --
// "Ignore sound below" (#349) -- so you can see whether a sound would be heard
// as playing or singing, and set the threshold against your own room (#381).
//
// Two places draw one: Settings -> Practice, beside the threshold it shows,
// and the Input Level tool. Both through here, so the line means the same in
// each, and both measure as a take does (LevelMeter says how).
//
// It reads samples through a FIFO of its own, drained on a message-thread
// timer like every other tool's, so it costs the audio callback one copy and
// nothing else. A tool holds its stream for as long as it is open, as the shell
// counted on when it opened it; Settings holds one only while the meter is on
// screen, so an open Settings window on another tab takes none.
class InputLevelMeter final
    : public juce::Component,
      public juce::SettableTooltipClient,
      private AudioInputService::Listener,
      private juce::Timer
{
  public:
    enum class Listening
    {
        // From construction until destruction: a tool's stream.
        always,

        // Only while the meter is showing, and only if a stream is free.
        whileShowing
    };

    // The scale, in dB below full scale: the threshold's whole range and up
    // to full scale.
    static constexpr double lowestDb = -80.0;
    static constexpr double highestDb = 0.0;

    // Louder than this, the bar turns to a warning: a little more and the
    // input clips.
    static constexpr double nearClippingDb = -3.0;

    InputLevelMeter(
        AudioInputService& audioInputService,
        std::function<double()> thresholdDb,
        Listening howLong,
        Theme initialTheme);
    ~InputLevelMeter() override;

    void setTheme(Theme newTheme);

    // Drawn bottom to top rather than left to right, for a tall, thin pane.
    void setVertical(bool shouldBeVertical);

    // The dB figures along the scale. Off leaves only the bar.
    void setShowsScale(bool shouldShowScale);

    [[nodiscard]] LevelMeter::Reading reading() const noexcept
    {
        return meter.reading();
    }

    // The threshold now, as the line is drawn at it.
    [[nodiscard]] double thresholdDb() const;

    // Why there is no level to show -- the microphone is muted, disconnected,
    // or there is no stream free to read it -- or empty when there is one.
    [[nodiscard]] const juce::String& unavailableReason() const noexcept
    {
        return unavailable;
    }

    // Back to silence.
    void reset();

    // After every refresh, so a caller showing the figures redraws them.
    std::function<void()> onUpdate;

    void paint(juce::Graphics& graphics) override;

  private:
    static constexpr int refreshRateHz = 30;

    void audioInputAboutToStart(double sampleRate, int inputChannels) override;
    void audioInputStopped() override;
    void audioInputStateChanged(AudioInputService::InputState state) override;
    void timerCallback() override;

    void startListening();
    void stopListening();

    // Where `db` falls along the bar's length, from 0 at the quiet end to 1 at
    // full scale.
    [[nodiscard]] static float proportionFor(double db) noexcept;

    AudioInputService& audio;
    std::function<double()> threshold;
    const Listening listening;
    Theme theme;
    bool vertical = false;
    bool showsScale = true;
    bool listeningNow = false;

    LevelMeter meter;
    std::vector<float> scratch;
    juce::String stateReason;
    juce::String unavailable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InputLevelMeter)
};
