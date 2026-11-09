#include "ReviewMarks.h"

#include <map>
#include <tuple>

#include "ScoreCursor.h"

namespace engraving
{
namespace
{
using NoteKey = std::tuple<std::size_t, std::size_t, std::size_t, std::size_t, std::size_t>;

[[nodiscard]] NoteKey keyOf(std::size_t part, const score::NoteRef& ref)
{
    return {part, ref.measureIndex, ref.voiceIndex, ref.eventIndex, ref.noteIndex};
}

[[nodiscard]] PitchMark pitchMarkOf(const evaluation::NoteResult& result)
{
    if (result.intonation.has_value())
    {
        switch (result.intonation->verdict)
        {
        case evaluation::IntonationVerdict::sharp:
            return PitchMark::sharp;
        case evaluation::IntonationVerdict::flat:
            return PitchMark::flat;
        case evaluation::IntonationVerdict::uncertain:
            return PitchMark::uncertain;
        case evaluation::IntonationVerdict::inTune:
            return result.intonation->zone == pitch::IntonationZone::nearPerfect
                       ? PitchMark::correct
                       : PitchMark::acceptable;
        case evaluation::IntonationVerdict::wrong:
            break;
        }
    }
    switch (result.outcome)
    {
    case evaluation::NoteOutcome::correct:
        return PitchMark::correct;
    case evaluation::NoteOutcome::wrong:
        return PitchMark::wrong;
    case evaluation::NoteOutcome::missed:
        return PitchMark::missed;
    case evaluation::NoteOutcome::pitchNotJudged:
        return PitchMark::none;
    }
    return PitchMark::none;
}

[[nodiscard]] TimingMark timingMarkOf(evaluation::TimingVerdict verdict)
{
    switch (verdict)
    {
    case evaluation::TimingVerdict::early:
        return TimingMark::early;
    case evaluation::TimingVerdict::late:
        return TimingMark::late;
    case evaluation::TimingVerdict::onTime:
    case evaluation::TimingVerdict::absent:
        return TimingMark::none;
    }
    return TimingMark::none;
}
} // namespace

ReviewMarks reviewMarksFor(
    const ScoreLayout& layout,
    const evaluation::NoteReport& report,
    const std::vector<double>& extraTicks)
{
    ReviewMarks marks;
    if (layout.outcome != LayoutOutcome::laidOut)
    {
        return marks;
    }

    std::map<NoteKey, const evaluation::NoteResult*> results;
    for (const evaluation::NoteResult& result : report.results)
    {
        results.emplace(keyOf(result.part, result.ref), &result);
    }

    for (std::size_t index = 0; index < layout.systems.size(); ++index)
    {
        const System& system = layout.systems[index];
        for (std::size_t head = 0; head < system.noteheads.size(); ++head)
        {
            const NoteheadGlyph& glyph = system.noteheads[head];
            if (glyph.isGrace || glyph.staff < 0)
            {
                continue;
            }
            const auto staff = static_cast<std::size_t>(glyph.staff);
            if (staff >= system.staffParts.size())
            {
                continue;
            }
            const auto found = results.find(keyOf(system.staffParts[staff], glyph.source));
            if (found == results.end())
            {
                continue;
            }
            const evaluation::NoteResult& result = *found->second;
            NoteMark mark;
            mark.system = index;
            mark.notehead = head;
            mark.pitch = pitchMarkOf(result);
            mark.timing = timingMarkOf(result.timing);
            mark.playedMidi = result.playedMidi;
            if (result.intonation.has_value() &&
                result.intonation->verdict != evaluation::IntonationVerdict::uncertain)
            {
                mark.cents = result.intonation->cents;
            }
            marks.notes.push_back(mark);
        }
    }

    for (std::size_t index = 0; index < report.extra.size() && index < extraTicks.size(); ++index)
    {
        if (const std::optional<CursorPlace> place = placeOfTick(layout, extraTicks[index]))
        {
            marks.extras.push_back(ExtraMark{place->system, place->x, report.extra[index].midi});
        }
    }
    return marks;
}
} // namespace engraving
