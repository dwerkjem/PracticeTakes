#include "TakeText.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace l10n
{
namespace
{
[[nodiscard]] juce::String utf8(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

[[nodiscard]] juce::String whole(long value)
{
    return l10n::number(static_cast<double>(value), 0);
}

// The pieces of a row or a summary, joined.
[[nodiscard]] juce::String joined(const juce::StringArray& parts)
{
    juce::StringArray kept(parts);
    kept.removeEmptyStrings();
    return kept.joinIntoString(l10n::text("review.separator"));
}

// "an octave low", "2 octaves high"; nothing in the written octave.
[[nodiscard]] juce::String octaveText(int octave)
{
    if (octave == 0)
    {
        return {};
    }
    return l10n::plural(
        octave < 0 ? l10n::pluralKey("review.octave.low") : l10n::pluralKey("review.octave.high"),
        std::abs(octave));
}

// A sung note's verdict: "in tune", "18 ¢ sharp", "pitch uncertain".
[[nodiscard]] juce::String intonationText(const evaluation::Intonation& intonation)
{
    const auto cents = whole(std::lround(std::abs(intonation.cents)));
    switch (intonation.verdict)
    {
    case evaluation::IntonationVerdict::inTune:
    {
        // Within the range, but not near perfect: how far, signed.
        if (intonation.zone != pitch::IntonationZone::acceptable)
        {
            return l10n::text("review.intonation.in-tune");
        }
        const long rounded = std::lround(intonation.cents);
        const auto sign = rounded < 0 ? juce::String::fromUTF8("\xE2\x88\x92") : juce::String("+");
        return l10n::format(
            "review.intonation.in-tune.off", {{"cents", sign + whole(std::abs(rounded))}});
    }
    case evaluation::IntonationVerdict::sharp:
        return l10n::format("review.intonation.sharp", {{"cents", cents}});
    case evaluation::IntonationVerdict::flat:
        return l10n::format("review.intonation.flat", {{"cents", cents}});
    case evaluation::IntonationVerdict::uncertain:
        return l10n::text("review.intonation.uncertain");
    case evaluation::IntonationVerdict::wrong:
        return {};
    }
    return {};
}

[[nodiscard]] juce::String outcomeText(const evaluation::NoteResult& result)
{
    const bool sung = result.intonation.has_value();
    switch (result.outcome)
    {
    case evaluation::NoteOutcome::correct:
        return sung ? intonationText(*result.intonation) : l10n::text("review.outcome.correct");
    case evaluation::NoteOutcome::wrong:
        if (!result.playedMidi.has_value())
        {
            return l10n::text("review.outcome.wrong");
        }
        return l10n::format(
            sung ? l10n::key("review.outcome.wrong.sang")
                 : l10n::key("review.outcome.wrong.played"),
            {{"note", utf8(evaluation::keyName(*result.playedMidi))}});
    case evaluation::NoteOutcome::missed:
        return l10n::text("review.outcome.missed");
    case evaluation::NoteOutcome::pitchNotJudged:
        return sung ? intonationText(*result.intonation) : l10n::text("review.outcome.played");
    }
    return {};
}

[[nodiscard]] juce::String timingText(evaluation::TimingVerdict verdict, double errorMs)
{
    const auto milliseconds = whole(std::lround(std::abs(errorMs)));
    switch (verdict)
    {
    case evaluation::TimingVerdict::early:
        return l10n::format("review.timing.early", {{"ms", milliseconds}});
    case evaluation::TimingVerdict::late:
        return l10n::format("review.timing.late", {{"ms", milliseconds}});
    case evaluation::TimingVerdict::onTime:
        return l10n::text("review.timing.on-time");
    case evaluation::TimingVerdict::absent:
        return {};
    }
    return {};
}

[[nodiscard]] juce::String place(std::int64_t bar, int beat)
{
    return l10n::format(
        "review.row.place",
        {{"bar", whole(static_cast<long>(bar + 1))}, {"beat", whole(beat + 1)}});
}
} // namespace

juce::String takeStatus(const take::Take& take)
{
    const auto length = utf8(take::formatDuration(take.performedSeconds()));
    const auto name = juce::String(static_cast<juce::int64>(take.id));

    switch (take.state())
    {
    case take::TakeState::ready:
        return l10n::text("take.status.starting");
    case take::TakeState::countingIn:
        return l10n::text("take.status.counting-in");
    case take::TakeState::performing:
        return l10n::format("take.status.recording", {{"length", length}});
    case take::TakeState::paused:
        return l10n::format("take.status.paused", {{"length", length}});
    case take::TakeState::completed:
        return l10n::format("take.status.complete", {{"take", name}, {"length", length}});
    case take::TakeState::stoppedEarly:
        return take.stoppedInBar.has_value()
                   ? l10n::format(
                         "take.status.stopped-in-bar",
                         {{"take", name},
                          {"bar", whole(static_cast<long>(*take.stoppedInBar + 1))},
                          {"length", length}})
                   : l10n::format(
                         "take.status.stopped-early", {{"take", name}, {"length", length}});
    case take::TakeState::cancelled:
        return l10n::format("take.status.discarded", {{"take", name}});
    case take::TakeState::failed:
        return l10n::format(
            "take.status.failed",
            {{"take", name}, {"reason", l10n::text(take.lifecycle.failureReason())}});
    }
    return {};
}

juce::String reviewRow(
    const take::Take& take,
    const evaluation::NoteReport& report,
    const evaluation::ReviewRow& row)
{
    juce::StringArray parts{place(row.bar, row.beat)};
    if (row.result.has_value() && *row.result < report.results.size())
    {
        const evaluation::NoteResult& result = report.results[*row.result];
        // The octave is said only of a note whose pitch was judged.
        const bool judged =
            result.intonation.has_value() &&
            result.intonation->verdict != evaluation::IntonationVerdict::uncertain &&
            result.intonation->verdict != evaluation::IntonationVerdict::wrong;
        parts.add(utf8(evaluation::writtenNameOf(take, result)));
        parts.add(outcomeText(result));
        parts.add(judged ? octaveText(result.intonation->octave) : juce::String());
        parts.add(timingText(result.timing, result.timingErrorMs));
    }
    else if (row.extra.has_value() && *row.extra < report.extra.size())
    {
        const evaluation::ExtraNote& extra = report.extra[*row.extra];
        if (extra.midi.has_value())
        {
            parts.add(utf8(evaluation::keyName(*extra.midi)));
        }
        parts.add(l10n::text("review.outcome.extra"));
    }
    return joined(parts);
}

juce::String reviewSummary(const evaluation::NoteReport& report)
{
    const evaluation::MomentCounts moments = evaluation::momentsOf(report);
    const evaluation::NoteSummary& summary = report.summary;

    // A sung take's correct notes are told by how in tune they were, and its
    // uncertain ones apart from the chords nobody judged (decision 8).
    const bool sung = summary.inTune + summary.sharp + summary.flat + summary.uncertain > 0;
    const std::size_t otherCorrect =
        summary.correct - (summary.inTune + summary.sharp + summary.flat);
    const std::array<std::pair<std::size_t, std::string_view>, 11> counts{{
        {sung ? summary.inTune : 0, l10n::pluralKey("review.summary.in-tune")},
        {sung ? summary.sharp : 0, l10n::pluralKey("review.summary.sharp")},
        {sung ? summary.flat : 0, l10n::pluralKey("review.summary.flat")},
        {sung ? otherCorrect : summary.correct, l10n::pluralKey("review.summary.correct")},
        {summary.wrong, l10n::pluralKey("review.summary.wrong")},
        {summary.missed, l10n::pluralKey("review.summary.missed")},
        {sung ? summary.uncertain : 0, l10n::pluralKey("review.summary.uncertain")},
        {summary.pitchNotJudged - (sung ? summary.uncertain : 0),
         l10n::pluralKey("review.summary.played")},
        {summary.extra, l10n::pluralKey("review.summary.extra")},
        {moments.early, l10n::pluralKey("review.summary.early")},
        {moments.late, l10n::pluralKey("review.summary.late")},
    }};

    juce::StringArray parts;
    for (const auto& [count, name] : counts)
    {
        if (count != 0)
        {
            parts.add(l10n::plural(name, static_cast<std::int64_t>(count)));
        }
    }
    return parts.isEmpty() ? l10n::text("review.summary.nothing") : joined(parts);
}
} // namespace l10n
