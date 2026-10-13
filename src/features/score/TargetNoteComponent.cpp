#include "TargetNoteComponent.h"

#include <juce_core/juce_core.h>

namespace
{
// Seconds, to two places. A note's start and duration are the one pair of
// numbers that proves the score's tempo map was read rather than defaulted, so
// they are shown rather than rounded away to whole seconds.
[[nodiscard]] juce::String secondsText(double seconds)
{
    return juce::String(seconds, 2) + " s";
}
} // namespace

TargetNoteComponent::TargetNoteComponent(const score::CurrentScore& sharedCurrentScore)
    : currentScore(sharedCurrentScore)
{
    setComponentID("target-note-tool");

    refreshFromCurrentScore();
    startTimerHz(refreshRateHz);
}

TargetNoteComponent::~TargetNoteComponent()
{
    stopTimer();
}

void TargetNoteComponent::timerCallback()
{
    refreshFromCurrentScore();
}

void TargetNoteComponent::refreshFromCurrentScore()
{
    std::shared_ptr<const score::Score> latest = currentScore.score();

    // Pointer identity is the whole change check. Two live scores are never the
    // same pointer -- holding `renderedScore` is what guarantees it, since the
    // old score cannot be freed and have its address reused while this still
    // refers to it. See score::CurrentScore.
    if (latest == renderedScore)
    {
        return;
    }

    renderedScore = std::move(latest);
    selection = selectTargetNote(renderedScore);

    repaint();
}

void TargetNoteComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    repaint();
}

void TargetNoteComponent::resetToDefaults()
{
    // There is nothing to reset: the tool has no settings and no view state,
    // and which note it shows is the score's business rather than a preference.
    // Re-reading the holder is the one honest thing to do here, so that a reset
    // is at least never stale.
    renderedScore = nullptr;
    refreshFromCurrentScore();
}

AppPalette TargetNoteComponent::palette() const
{
    return appPaletteFor(currentTheme);
}

void TargetNoteComponent::paint(juce::Graphics& graphics)
{
    const AppPalette colours = palette();

    graphics.fillAll(colours.background);

    const juce::Rectangle<int> bounds = getLocalBounds().reduced(16);

    switch (selection.outcome)
    {
    case TargetNoteOutcome::noScoreOpen:
        paintMessage(
            graphics, bounds, "No score open",
            "Open a MusicXML file with File > Open Score, and its first note appears here.");

        return;

    case TargetNoteOutcome::noPitchedNote:
        paintMessage(
            graphics, bounds, "No notes in this score",
            juce::String(selection.scoreTitle) + " imported, but holds no pitched note to target.");

        return;

    case TargetNoteOutcome::found:
        // Checked rather than dereferenced on the strength of the outcome:
        // `found` means the note is there, but a reader of this file should not
        // have to know that to see that nothing is dereferenced unchecked.
        if (selection.note.has_value())
        {
            paintTarget(graphics, bounds, *selection.note);
        }

        return;
    }
}

void TargetNoteComponent::paintMessage(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    const juce::String& heading,
    const juce::String& detail) const
{
    const AppPalette colours = palette();

    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    graphics.drawText(heading, bounds.removeFromTop(26), juce::Justification::centredLeft);

    bounds.removeFromTop(6);

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(14.0f));
    graphics.drawFittedText(detail, bounds, juce::Justification::topLeft, 3);
}

void TargetNoteComponent::paintTarget(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds,
    const TargetNote& target) const
{
    const AppPalette colours = palette();

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(13.0f));
    graphics.drawText(
        juce::String(selection.scoreTitle), bounds.removeFromTop(18),
        juce::Justification::centredLeft);

    bounds.removeFromTop(4);

    // The one thing the whole change exists to put on screen.
    graphics.setColour(colours.accent);
    graphics.setFont(juce::FontOptions(44.0f, juce::Font::bold));
    graphics.drawText(
        juce::String(target.soundingName), bounds.removeFromTop(52),
        juce::Justification::centredLeft);

    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(14.0f));

    juce::StringArray lines;

    lines.add(
        "Bar " +
        juce::String(target.printedBarNumber.empty() ? "(unnumbered)" : target.printedBarNumber) +
        "  -  " + juce::String(target.partName) + ", staff " + juce::String(target.staff) +
        ", voice " + juce::String(target.voiceNumber));

    lines.add(
        "Starts at " + secondsText(target.startSeconds) + ", lasts " +
        secondsText(target.durationSeconds));

    lines.add("Sounding MIDI note " + juce::String(target.soundingMidiNoteNumber));

    // Only when it says something: on a non-transposing part the written
    // spelling is the sounding one, and a line repeating it would be noise.
    if (!target.soundsAsWritten)
    {
        lines.add(
            "Written as " + juce::String(target.writtenName) +
            " -- this part transposes, and the target is what it sounds");
    }

    if (target.isChordMember)
    {
        lines.add(
            "One of " + juce::String(static_cast<int>(target.chordNoteCount)) +
            " notes in a chord");
    }

    for (const juce::String& line : lines)
    {
        graphics.drawFittedText(
            line, bounds.removeFromTop(22), juce::Justification::centredLeft, 1);
    }

    bounds.removeFromTop(8);

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(12.0f));
    graphics.drawFittedText(
        "The target is the score's first pitched note. Choosing another one is not built yet.",
        bounds, juce::Justification::topLeft, 2);
}
