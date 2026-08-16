#include "PitchTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double referenceFrequencyHz = 440.0;
constexpr int referenceMidiNote = 69;

constexpr std::array<const char*, 12> noteNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
} // namespace

double PitchTracker::frequencyToMidi(double frequency)
{
    return referenceMidiNote + 12.0 * std::log2(frequency / referenceFrequencyHz);
}

double PitchTracker::midiToFrequency(double midiPitch)
{
    return referenceFrequencyHz * std::pow(2.0, (midiPitch - referenceMidiNote) / 12.0);
}

std::string noteNameForMidi(int midiNote)
{
    const auto noteIndex = ((midiNote % 12) + 12) % 12;
    const auto octave = (midiNote / 12) - 1;

    return std::string(noteNames[static_cast<std::size_t>(noteIndex)]) + std::to_string(octave);
}

PitchTracker::Update PitchTracker::detected(double frequency, const Settings& settings)
{
    Update update;

    if (!isConfirmedPitch(frequency))
    {
        // A suspected harmonic must not enter the smoothing history, and must
        // not draw a line to itself in the graph. A real large interval is
        // admitted once it has held for several consecutive frames.
        update.accepted = false;
        update.historyMidiNote = std::numeric_limits<double>::quiet_NaN();
        update.hasSignal = signalPresent;
        update.displayedFrequency = displayedFrequency;
        update.displayedCents = displayedCents;
        update.displayedMidiNote = lockedMidiNote;
        update.hasDisplayedNote = hasLockedMidiNote;
        return update;
    }

    framesWithoutPitch = 0;

    const auto stableFrequency = smoothFrequency(frequency, settings.easing, settings.averaging);
    updateDisplayedNote(stableFrequency, settings.noteSwitchSemitones);
    signalPresent = true;

    update.accepted = true;
    update.hasSignal = true;
    update.historyMidiNote = smoothedMidi;
    update.displayedFrequency = displayedFrequency;
    update.displayedCents = displayedCents;
    update.displayedMidiNote = lockedMidiNote;
    update.hasDisplayedNote = hasLockedMidiNote;
    return update;
}

PitchTracker::Update PitchTracker::missing(const Settings& settings)
{
    pendingJumpFrames = 0;
    pendingJumpMidiNote = 0.0;
    ++framesWithoutPitch;

    Update update;
    update.accepted = false;
    update.historyMidiNote = std::numeric_limits<double>::quiet_NaN();

    if (framesWithoutPitch >= static_cast<int>(settings.dropoutFrames))
    {
        reset();
    }

    update.hasSignal = signalPresent;
    update.displayedFrequency = displayedFrequency;
    update.displayedCents = displayedCents;
    update.displayedMidiNote = lockedMidiNote;
    update.hasDisplayedNote = hasLockedMidiNote;
    return update;
}

bool PitchTracker::isConfirmedPitch(double frequency)
{
    const auto midiPitch = frequencyToMidi(frequency);
    if (std::abs(smoothedMidi) <= std::numeric_limits<double>::epsilon() ||
        std::abs(midiPitch - smoothedMidi) <= immediatePitchJumpSemitones)
    {
        pendingJumpFrames = 0;
        pendingJumpMidiNote = 0.0;
        return true;
    }

    if (pendingJumpFrames == 0 ||
        std::abs(midiPitch - pendingJumpMidiNote) > matchingJumpToleranceSemitones)
    {
        pendingJumpMidiNote = midiPitch;
        pendingJumpFrames = 1;
        return false;
    }

    // Follow a genuine glide while requiring every confirming frame to stay in
    // the same small neighbourhood.
    pendingJumpMidiNote +=
        (midiPitch - pendingJumpMidiNote) / static_cast<double>(pendingJumpFrames + 1);
    ++pendingJumpFrames;

    if (pendingJumpFrames < pitchJumpConfirmationFrames)
    {
        return false;
    }

    // Start a fresh smoothing segment at the confirmed note. Otherwise the old
    // pitch keeps making the newly accepted note look like another jump.
    smoothedMidi = midiPitch;
    recentPitchCount = 0;
    recentPitchWriteIndex = 0;
    pendingJumpFrames = 0;
    pendingJumpMidiNote = 0.0;
    return true;
}

double PitchTracker::smoothFrequency(double frequency, double easing, double averaging)
{
    const auto midiPitch = frequencyToMidi(frequency);

    recentMidiPitches[static_cast<std::size_t>(recentPitchWriteIndex)] = midiPitch;
    recentPitchWriteIndex = (recentPitchWriteIndex + 1) % maximumAverageWindow;
    recentPitchCount = std::min(recentPitchCount + 1, maximumAverageWindow);

    const auto averagedMidiPitch = averageRecentMidiPitches(averaging);

    smoothedMidi = std::abs(smoothedMidi) <= std::numeric_limits<double>::epsilon()
                       ? averagedMidiPitch
                       : smoothedMidi + easing * (averagedMidiPitch - smoothedMidi);

    return midiToFrequency(smoothedMidi);
}

double PitchTracker::averageRecentMidiPitches(double averaging) const
{
    const auto requestedWindow = static_cast<int>(averaging);
    const auto pitchesToAverage = std::min(requestedWindow, recentPitchCount);

    double sum = 0.0;
    for (int offset = 0; offset < pitchesToAverage; ++offset)
    {
        const auto index =
            (recentPitchWriteIndex - 1 - offset + maximumAverageWindow) % maximumAverageWindow;
        sum += recentMidiPitches[static_cast<std::size_t>(index)];
    }

    return sum / static_cast<double>(std::max(1, pitchesToAverage));
}

void PitchTracker::updateDisplayedNote(double frequency, double noteSwitchSemitones)
{
    const auto midiPitch = frequencyToMidi(frequency);
    const auto nearestMidiNote = static_cast<int>(std::round(midiPitch));

    if (!hasLockedMidiNote)
    {
        lockedMidiNote = nearestMidiNote;
        hasLockedMidiNote = true;
    }
    else if (std::abs(midiPitch - static_cast<double>(lockedMidiNote)) > noteSwitchSemitones)
    {
        // Hold the current label until the pitch moves far enough away, so the
        // display does not flicker on a boundary.
        lockedMidiNote = nearestMidiNote;
    }

    displayedFrequency = frequency;
    displayedCents = 100.0 * (midiPitch - static_cast<double>(lockedMidiNote));
}

void PitchTracker::reset()
{
    recentMidiPitches.fill(0.0);
    recentPitchCount = 0;
    recentPitchWriteIndex = 0;
    smoothedMidi = 0.0;
    framesWithoutPitch = 0;
    pendingJumpFrames = 0;
    pendingJumpMidiNote = 0.0;
    hasLockedMidiNote = false;
    signalPresent = false;
    displayedFrequency = 0.0;
    displayedCents = 0.0;
}
