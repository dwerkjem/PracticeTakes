#pragma once

#include <array>
#include <string>

// PitchTracker turns the per-frame estimates of PitchDetector into a stable
// note a human can read.
//
// The two are deliberately different things. PitchDetector is memoryless: give
// it a window of samples and it answers "what frequency is this", with no
// notion of what came before. PitchTracker has memory, and that memory is the
// whole point -- it decides whether a large interval is a real glide or a
// harmonic misdetection, averages and eases the result so the display does not
// jitter, and holds a note label until the pitch moves far enough away to
// justify changing it.
//
// It is free of JUCE so that behaviour can be tested without a display. That
// matters more here than elsewhere: the confirmation state machine is the part
// most likely to be subtly wrong, and it is invisible from the outside except
// as "the tuner feels twitchy".
class PitchTracker
{
  public:
    // The user-adjustable parameters, mirroring the sliders that carry them.
    // Passed in per frame rather than stored, so the tracker holds no
    // configuration of its own -- only what it has heard.
    struct Settings
    {
        double easing = 0.35;              // 0 = frozen, 1 = follow instantly
        double averaging = 5.0;            // frames of pitch history to mean
        double noteSwitchSemitones = 0.55; // distance before the label changes
        double dropoutFrames = 4.0;        // silent frames before giving up
    };

    // What one analysis frame produced. `historyMidiNote` is NaN whenever the
    // frame did not yield a usable pitch -- a rejected jump or silence -- which
    // is how the caller draws a gap in the graph rather than a line across it.
    struct Update
    {
        bool hasSignal = false;
        bool accepted = false;
        double historyMidiNote = 0.0;
        double displayedFrequency = 0.0;
        double displayedCents = 0.0;
        int displayedMidiNote = 69;
        bool hasDisplayedNote = false;
    };

    // A frame in which a pitch was detected.
    [[nodiscard]] Update detected(double frequency, const Settings& settings);

    // A frame in which none was. Enough consecutive ones reset the tracker.
    [[nodiscard]] Update missing(const Settings& settings);

    // Forget everything heard so far. Used when the audio device changes state,
    // where continuing to smooth against pre-interruption pitch would be wrong.
    void reset();

    [[nodiscard]] double smoothedMidiNote() const
    {
        return smoothedMidi;
    }
    [[nodiscard]] bool hasSignal() const
    {
        return signalPresent;
    }

    static constexpr int maximumAverageWindow = 15;

    [[nodiscard]] static double frequencyToMidi(double frequency);
    [[nodiscard]] static double midiToFrequency(double midiPitch);

  private:
    // A jump of more than this is treated as suspect until it repeats.
    static constexpr double immediatePitchJumpSemitones = 5.0;
    // Confirming frames must agree with each other this closely.
    static constexpr double matchingJumpToleranceSemitones = 1.5;
    // How many agreeing frames admit a jump.
    static constexpr int pitchJumpConfirmationFrames = 4;

    [[nodiscard]] bool isConfirmedPitch(double frequency);
    [[nodiscard]] double smoothFrequency(double frequency, double easing, double averaging);
    [[nodiscard]] double averageRecentMidiPitches(double averaging) const;
    void updateDisplayedNote(double frequency, double noteSwitchSemitones);

    std::array<double, maximumAverageWindow> recentMidiPitches{};
    int recentPitchCount = 0;
    int recentPitchWriteIndex = 0;

    double smoothedMidi = 0.0;
    double displayedFrequency = 0.0;
    double displayedCents = 0.0;
    int lockedMidiNote = 69;
    int framesWithoutPitch = 0;
    double pendingJumpMidiNote = 0.0;
    int pendingJumpFrames = 0;

    bool hasLockedMidiNote = false;
    bool signalPresent = false;
};

// The note name for a MIDI number, as "C#4". Shared because the tuner's status
// line and its graph axis must agree; they were separate copies before, free to
// drift into disagreeing about the same note.
[[nodiscard]] std::string noteNameForMidi(int midiNote);
