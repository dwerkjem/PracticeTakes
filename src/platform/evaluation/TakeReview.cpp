#include "TakeReview.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "../pitch/TargetChoice.h"
#include "../score/PitchNames.h"

namespace evaluation
{
namespace
{
[[nodiscard]] bool offPitch(const NoteResult& result)
{
    return result.intonation.has_value() &&
           (result.intonation->verdict == IntonationVerdict::sharp ||
            result.intonation->verdict == IntonationVerdict::flat);
}
} // namespace

std::string writtenNameOf(const take::Take& take, const NoteResult& result)
{
    if (take.score != nullptr && result.part < take.score->parts.size())
    {
        const score::Part& part = take.score->parts[result.part];
        const score::NoteRef& ref = result.ref;
        if (ref.measureIndex < part.measures.size())
        {
            const score::Measure& measure = part.measures[ref.measureIndex];
            if (ref.voiceIndex < measure.voices.size())
            {
                const auto& events = measure.voices[ref.voiceIndex].events;
                if (ref.eventIndex < events.size() &&
                    ref.noteIndex < events[ref.eventIndex].notes.size())
                {
                    const score::Pitch& sounding =
                        events[ref.eventIndex].notes[ref.noteIndex].sounding;
                    if (score::isConsistent(sounding))
                    {
                        return score::spelledName(sounding);
                    }
                }
            }
        }
    }
    return keyName(result.writtenMidi);
}

MomentCounts momentsOf(const NoteReport& report)
{
    MomentCounts counts;
    std::optional<score::Tick> last;
    for (const NoteResult& result : report.results)
    {
        if (last == result.tick)
        {
            continue;
        }
        last = result.tick;
        switch (result.timing)
        {
        case TimingVerdict::early:
            ++counts.early;
            break;
        case TimingVerdict::onTime:
            ++counts.onTime;
            break;
        case TimingVerdict::late:
            ++counts.late;
            break;
        case TimingVerdict::absent:
            break;
        }
    }
    return counts;
}

std::string keyName(int midi)
{
    // Spelled with sharps, as a key with none would spell it, and named by the
    // one naming path (score::pitchName); a number outside MIDI's range is
    // named by its pitch class and octave all the same.
    const int pitchClass = ((midi % 12) + 12) % 12;
    const int octave = (midi - pitchClass) / 12 - 1;
    const auto spelled = pitch::spellMidi(pitchClass + 12, true);
    score::Pitch named = spelled.value_or(score::Pitch{});
    named.octave = octave;
    return score::spelledName(named);
}

std::vector<ReviewRow> reviewRows(const take::Take& take, const NoteReport& report)
{
    std::vector<ReviewRow> rows;
    rows.reserve(report.results.size() + report.extra.size());
    const bool timed = take.table != nullptr && !take.table->empty();

    for (std::size_t index = 0; index < report.results.size(); ++index)
    {
        const NoteResult& result = report.results[index];
        ReviewRow row;
        row.tick = static_cast<double>(result.tick);
        row.result = index;
        if (timed)
        {
            row.frame = transport::frameOfTick(*take.table, row.tick);
            const transport::BeatPosition beat = transport::beatAtFrame(*take.table, row.frame);
            row.bar = beat.bar;
            row.beat = beat.beat;
        }
        row.mistake = result.outcome == NoteOutcome::wrong ||
                      result.outcome == NoteOutcome::missed || offPitch(result) ||
                      result.timing == TimingVerdict::early || result.timing == TimingVerdict::late;

        rows.push_back(std::move(row));
    }

    const std::int64_t latency = latencyFramesOf(take);
    for (std::size_t index = 0; index < report.extra.size(); ++index)
    {
        const ExtraNote& extra = report.extra[index];
        ReviewRow row;
        row.frame = extra.frame - latency;
        row.extra = index;
        row.mistake = true;
        if (timed)
        {
            row.tick = std::max(0.0, transport::tickAtFrame(*take.table, row.frame));
            const transport::BeatPosition beat = transport::beatAtFrame(*take.table, row.frame);
            row.bar = beat.bar;
            row.beat = beat.beat;
        }
        rows.push_back(std::move(row));
    }

    // Time order; a written note before an extra at the same frame, and the
    // report's own order otherwise.
    std::stable_sort(
        rows.begin(), rows.end(),
        [](const ReviewRow& left, const ReviewRow& right) { return left.frame < right.frame; });
    return rows;
}

std::size_t matchScore(const NoteReport& report)
{
    return 2 * report.summary.correct + momentsOf(report).onTime;
}

ReviewedPart bestPartFor(
    const take::Take& take,
    const TimingSettings& timing,
    const IntonationSettings& intonation)
{
    ReviewedPart best;
    if (take.score == nullptr || take.score->parts.empty())
    {
        return best;
    }

    const TakeEvidence evidence = evidenceOf(take, timing.onsets);
    std::optional<std::size_t> bestScore;
    for (std::size_t part = 0; part < take.score->parts.size(); ++part)
    {
        NoteReport report = judgeNotes(take, part, evidence, timing, intonation);
        const std::size_t score = matchScore(report);
        if (!bestScore.has_value() || score > *bestScore)
        {
            bestScore = score;
            best.part = part;
            best.report = std::move(report);
        }
    }
    return best;
}
} // namespace evaluation
