#include "TakeReview.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "../score/PitchNames.h"

namespace evaluation
{
namespace
{
// The sounding pitch a result was written as, spelled as the file spelled it.
[[nodiscard]] std::string writtenName(const take::Take& take, const NoteResult& result)
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

[[nodiscard]] std::string outcomeText(const NoteResult& result)
{
    switch (result.outcome)
    {
    case NoteOutcome::correct:
        return "correct";
    case NoteOutcome::wrong:
        return result.playedMidi.has_value() ? "wrong, played " + keyName(*result.playedMidi)
                                             : "wrong";
    case NoteOutcome::missed:
        return "missed";
    case NoteOutcome::pitchNotJudged:
        return "played";
    }
    return {};
}

[[nodiscard]] std::string timingText(TimingVerdict verdict, double errorMs)
{
    const long rounded = std::lround(std::abs(errorMs));
    switch (verdict)
    {
    case TimingVerdict::early:
        return std::to_string(rounded) + " ms early";
    case TimingVerdict::late:
        return std::to_string(rounded) + " ms late";
    case TimingVerdict::onTime:
        return "on time";
    case TimingVerdict::absent:
        return {};
    }
    return {};
}

const std::string separator = " \xC2\xB7 ";

[[nodiscard]] std::string place(std::int64_t bar, int beat)
{
    return "Bar " + std::to_string(bar + 1) + ", beat " + std::to_string(beat + 1);
}

// The on-time and early/late moments: a chord's notes share one.
struct MomentCounts
{
    std::size_t early = 0;
    std::size_t onTime = 0;
    std::size_t late = 0;
};

[[nodiscard]] MomentCounts momentsOf(const NoteReport& report)
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
} // namespace

std::string keyName(int midi)
{
    static const std::array<const char*, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int pitchClass = ((midi % 12) + 12) % 12;
    const int octave = (midi - pitchClass) / 12 - 1;
    return std::string(names.at(static_cast<std::size_t>(pitchClass))) + std::to_string(octave);
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
        row.mistake =
            result.outcome == NoteOutcome::wrong || result.outcome == NoteOutcome::missed ||
            result.timing == TimingVerdict::early || result.timing == TimingVerdict::late;

        row.text = place(row.bar, row.beat) + separator + writtenName(take, result) + separator +
                   outcomeText(result);
        if (const std::string timing = timingText(result.timing, result.timingErrorMs);
            !timing.empty())
        {
            row.text += separator + timing;
        }
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
        row.text =
            place(row.bar, row.beat) + separator +
            (extra.midi.has_value() ? keyName(*extra.midi) + separator : std::string{}) + "extra";
        rows.push_back(std::move(row));
    }

    // Time order; a written note before an extra at the same frame, and the
    // report's own order otherwise.
    std::stable_sort(
        rows.begin(), rows.end(),
        [](const ReviewRow& left, const ReviewRow& right) { return left.frame < right.frame; });
    return rows;
}

std::string summaryLine(const NoteReport& report)
{
    const MomentCounts moments = momentsOf(report);
    const NoteSummary& summary = report.summary;
    const std::array<std::pair<std::size_t, const char*>, 7> parts{{
        {summary.correct, "correct"},
        {summary.wrong, "wrong"},
        {summary.missed, "missed"},
        {summary.pitchNotJudged, "played"},
        {summary.extra, "extra"},
        {moments.early, "early"},
        {moments.late, "late"},
    }};

    std::string line;
    for (const auto& [count, name] : parts)
    {
        if (count == 0)
        {
            continue;
        }
        if (!line.empty())
        {
            line += separator;
        }
        line += std::to_string(count) + " " + name;
    }
    return line.empty() ? "Nothing to judge" : line;
}

std::size_t matchScore(const NoteReport& report)
{
    return 2 * report.summary.correct + momentsOf(report).onTime;
}

ReviewedPart bestPartFor(const take::Take& take, const TimingSettings& settings)
{
    ReviewedPart best;
    if (take.score == nullptr || take.score->parts.empty())
    {
        return best;
    }

    const std::vector<PerformedOnset> onsets = onsetsOf(take);
    std::optional<std::size_t> bestScore;
    for (std::size_t part = 0; part < take.score->parts.size(); ++part)
    {
        NoteReport report = judgeNotes(take, part, onsets, settings);
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
