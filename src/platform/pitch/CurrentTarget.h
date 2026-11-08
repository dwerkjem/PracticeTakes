#pragma once

#include <memory>
#include <string>
#include <utility>

#include "../score/Score.h"
#include "../score/TargetNoteSelection.h"

// The note the player is aiming at, for the whole application, or why there is
// none (no JUCE dependency).
// openspec/changes/archive/2026-11-08-system-wide-target-note/design.md, decision 2.
//
// Held like score::CurrentScore: published whole, read on the message thread,
// and changed when the pointer is. Until #296 the shell sets it from each score
// it opens; #296's picker writes to this same holder.
namespace pitch
{
struct Target
{
    // The sounding pitch, which is what is matched against what is heard.
    int midiNoteNumber = 69;
    std::string name;

    // The spelling as written, which differs only for a transposing part.
    std::string writtenName;
    bool soundsAsWritten = true;

    // Where it came from, in words: "Voice, bar 1".
    std::string source;
};

class CurrentTarget
{
  public:
    // A target, or none with the reason in a sentence.
    void publish(std::shared_ptr<const Target> newTarget, std::string reasonWhenNone = {})
    {
        current = std::move(newTarget);
        reason = current != nullptr ? std::string{} : std::move(reasonWhenNone);
    }

    [[nodiscard]] std::shared_ptr<const Target> target() const
    {
        return current;
    }

    [[nodiscard]] const std::string& whyNone() const noexcept
    {
        return reason;
    }

  private:
    std::shared_ptr<const Target> current;
    std::string reason = "No score is open";
};

// The target a score offers (decision 3), or none and why.
[[nodiscard]] inline std::pair<std::shared_ptr<const Target>, std::string>
targetFromScore(const std::shared_ptr<const score::Score>& score)
{
    const TargetNoteSelection selection = selectTargetNote(score);
    switch (selection.outcome)
    {
    case TargetNoteOutcome::noScoreOpen:
        return {nullptr, "No score is open"};
    case TargetNoteOutcome::noPitchedNote:
        return {nullptr, "This score has no notes to aim at"};
    case TargetNoteOutcome::onlyPolyphonic:
        return {nullptr, "Every part plays more than one note at a time; a target is one note"};
    case TargetNoteOutcome::found:
        break;
    }

    // Checked rather than trusted from the outcome: `found` means it is there,
    // but nothing here should have to know that.
    if (!selection.note.has_value())
    {
        return {nullptr, "This score has no notes to aim at"};
    }
    const TargetNote& note = selection.note.value();
    auto target = std::make_shared<Target>();
    target->midiNoteNumber = note.soundingMidiNoteNumber;
    target->name = note.soundingName;
    target->writtenName = note.writtenName;
    target->soundsAsWritten = note.soundsAsWritten;
    target->source = note.partName + ", bar " +
                     (note.printedBarNumber.empty() ? std::to_string(note.location.measureIndex + 1)
                                                    : note.printedBarNumber);
    return {std::move(target), {}};
}
} // namespace pitch
