#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "../score/CurrentScore.h"
#include "../score/PartTimeline.h"
#include "../score/PitchNames.h"
#include "CurrentTarget.h"
#include "TargetChoice.h"

// The shared target following the score as it plays (no JUCE dependency).
// openspec/changes/archive/2026-11-09-now-and-next/design.md, decision 5.
namespace pitch
{
// The target for the note sounding at `tick` in `partIndex` of `source`, whose
// timeline is `timeline`: its top note, spelled as written, from the score.
// Nothing on a rest, before the first note or after the last.
[[nodiscard]] inline std::shared_ptr<const Target> targetAtTick(
    const score::Score& source,
    std::size_t partIndex,
    const score::PartTimeline& timeline,
    double tick)
{
    const std::optional<std::size_t> index = score::itemAt(timeline, tick);
    if (!index.has_value() || partIndex >= source.parts.size())
    {
        return nullptr;
    }
    const score::TimelineItem& item = timeline[*index];
    if (!item.isNote() || item.pitches.empty() || !item.ref.has_value())
    {
        return nullptr;
    }

    // The top note, as a melody is sung from a chord; written as the file
    // spells it.
    const score::Pitch& sounding = item.pitches.back();
    const score::Part& part = source.parts[partIndex];
    const score::NoteRef& ref = *item.ref;
    score::Pitch written = sounding;
    std::string bar = std::to_string(ref.measureIndex + 1);
    if (ref.measureIndex < part.measures.size())
    {
        const score::Measure& measure = part.measures[ref.measureIndex];
        if (!measure.printedNumber.empty())
        {
            bar = measure.printedNumber;
        }
        if (ref.voiceIndex < measure.voices.size() &&
            ref.eventIndex < measure.voices[ref.voiceIndex].events.size())
        {
            for (const score::Note& note :
                 measure.voices[ref.voiceIndex].events[ref.eventIndex].notes)
            {
                if (note.sounding.midiNoteNumber == sounding.midiNoteNumber)
                {
                    written = note.written;
                }
            }
        }
    }

    const auto nameOf = [](const score::Pitch& pitch) -> std::string
    {
        if (score::isConsistent(pitch))
        {
            return score::spelledName(pitch);
        }
        const auto spelled = spellMidi(pitch.midiNoteNumber, true);
        return spelled.has_value() ? score::spelledName(*spelled) : std::string{};
    };
    auto target = std::make_shared<Target>();
    target->midiNoteNumber = sounding.midiNoteNumber;
    target->name = nameOf(sounding);
    target->writtenName = nameOf(written);
    target->soundsAsWritten = written.midiNoteNumber == sounding.midiNoteNumber;
    target->source = part.name + ", bar " + bar;
    target->origin = TargetOrigin::score;
    return target;
}

// Whether two targets aim at the same note from the same place: following
// publishes only when this changes.
[[nodiscard]] inline bool sameTarget(const Target* left, const Target* right) noexcept
{
    if (left == nullptr || right == nullptr)
    {
        return left == right;
    }
    return left->midiNoteNumber == right->midiNoteNumber && left->name == right->name &&
           left->source == right->source && left->origin == right->origin;
}

// Keeps a score's target on the note being sung while the clock plays
// (decision 5). The shell gives it the audible tick on every clock update.
class TargetFollower
{
  public:
    TargetFollower(const score::CurrentScore& scoreHolder, CurrentTarget& sharedTarget)
        : holder(scoreHolder), target(sharedTarget)
    {
    }

    // Publish the note at `tick` if the clock is playing, following is on,
    // and the target came from the score; only when the note changes, and
    // never on a rest. True when it published.
    bool follow(double tick, bool playing)
    {
        if (!target.followScoreWhilePlaying || !playing)
        {
            return false;
        }
        const std::shared_ptr<const Target> current = target.target();
        if (current == nullptr || current->origin != TargetOrigin::score)
        {
            return false;
        }
        const std::shared_ptr<const score::Score> source = holder.score();
        if (source == nullptr)
        {
            return false;
        }
        if (source != followed)
        {
            followed = source;
            const TargetNoteSelection selection = selectTargetNote(source);
            part = selection.note.has_value() ? selection.note->partIndex : 0;
            timeline = score::timelineOf(*source, part);
        }
        std::shared_ptr<const Target> next = targetAtTick(*source, part, timeline, tick);
        if (next == nullptr || sameTarget(current.get(), next.get()))
        {
            return false;
        }
        target.publish(std::move(next));
        return true;
    }

  private:
    const score::CurrentScore& holder;
    CurrentTarget& target;

    std::shared_ptr<const score::Score> followed;
    std::size_t part = 0;
    score::PartTimeline timeline;
};
} // namespace pitch
