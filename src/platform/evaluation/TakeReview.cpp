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

// "an octave low", "2 octaves high"; nothing in the written octave.
[[nodiscard]] std::string octaveText(int octave)
{
    if (octave == 0)
    {
        return {};
    }
    const int size = std::abs(octave);
    std::string text = size == 1 ? "an octave" : std::to_string(size) + " octaves";
    text += octave < 0 ? " low" : " high";
    return text;
}

// A sung note's verdict: "in tune", "18 ¢ sharp", "pitch uncertain".
[[nodiscard]] std::string intonationText(const Intonation& intonation)
{
    const std::string cents = std::to_string(std::lround(std::abs(intonation.cents))) + " \xC2\xA2";
    switch (intonation.verdict)
    {
    case IntonationVerdict::inTune:
    {
        // Within the range, but not near perfect: how far, signed.
        if (intonation.zone != pitch::IntonationZone::acceptable)
        {
            return "in tune";
        }
        const long rounded = std::lround(intonation.cents);
        return "in tune, " + std::string(rounded < 0 ? "\xE2\x88\x92" : "+") +
               std::to_string(std::abs(rounded)) + " \xC2\xA2";
    }
    case IntonationVerdict::sharp:
        return cents + " sharp";
    case IntonationVerdict::flat:
        return cents + " flat";
    case IntonationVerdict::uncertain:
        return "pitch uncertain";
    case IntonationVerdict::wrong:
        return {};
    }
    return {};
}

[[nodiscard]] std::string outcomeText(const NoteResult& result)
{
    const bool sung = result.intonation.has_value();
    switch (result.outcome)
    {
    case NoteOutcome::correct:
        return sung ? intonationText(*result.intonation) : "correct";
    case NoteOutcome::wrong:
        if (!result.playedMidi.has_value())
        {
            return "wrong";
        }
        return (sung ? "wrong, sang " : "wrong, played ") + keyName(*result.playedMidi);
    case NoteOutcome::missed:
        return "missed";
    case NoteOutcome::pitchNotJudged:
        return sung ? intonationText(*result.intonation) : "played";
    }
    return {};
}

[[nodiscard]] bool offPitch(const NoteResult& result)
{
    return result.intonation.has_value() &&
           (result.intonation->verdict == IntonationVerdict::sharp ||
            result.intonation->verdict == IntonationVerdict::flat);
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
        row.mistake = result.outcome == NoteOutcome::wrong ||
                      result.outcome == NoteOutcome::missed || offPitch(result) ||
                      result.timing == TimingVerdict::early || result.timing == TimingVerdict::late;

        // The octave is said only of a note whose pitch was judged.
        const bool judged = result.intonation.has_value() &&
                            result.intonation->verdict != IntonationVerdict::uncertain &&
                            result.intonation->verdict != IntonationVerdict::wrong;
        row.text = place(row.bar, row.beat);
        for (const std::string& part :
             {writtenName(take, result), outcomeText(result),
              judged ? octaveText(result.intonation->octave) : std::string{},
              timingText(result.timing, result.timingErrorMs)})
        {
            if (!part.empty())
            {
                row.text += separator;
                row.text += part;
            }
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
        row.text = place(row.bar, row.beat);
        if (extra.midi.has_value())
        {
            row.text += separator;
            row.text += keyName(*extra.midi);
        }
        row.text += separator;
        row.text += "extra";
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

    // A sung take's correct notes are told by how in tune they were, and its
    // uncertain ones apart from the chords nobody judged (decision 8).
    const bool sung = summary.inTune + summary.sharp + summary.flat + summary.uncertain > 0;
    const std::size_t otherCorrect =
        summary.correct - (summary.inTune + summary.sharp + summary.flat);
    const std::array<std::pair<std::size_t, const char*>, 11> parts{{
        {sung ? summary.inTune : 0, "in tune"},
        {sung ? summary.sharp : 0, "sharp"},
        {sung ? summary.flat : 0, "flat"},
        {sung ? otherCorrect : summary.correct, "correct"},
        {summary.wrong, "wrong"},
        {summary.missed, "missed"},
        {sung ? summary.uncertain : 0, "uncertain"},
        {summary.pitchNotJudged - (sung ? summary.uncertain : 0), "played"},
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

    const TakeEvidence evidence = evidenceOf(take);
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
