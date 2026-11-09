#include "NoteMatching.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>

namespace evaluation
{
namespace
{
[[nodiscard]] NoteResult
resultFor(const ExpectedNote& note, const TimingResult& moment, std::size_t part)
{
    NoteResult result;
    result.part = part;
    result.ref = note.ref;
    result.tick = moment.attack.tick;
    result.writtenMidi = note.midiNoteNumber;
    result.timing = moment.verdict;
    result.timingErrorMs = moment.errorMs;
    return result;
}

// One matched moment with pitches: exact pairs, then nearest pairs, then what
// is left (decisions 1 and 2).
void compareMoment(const TimingResult& moment, std::size_t part, NoteReport& report)
{
    const PerformedOnset& onset = *moment.onset;
    std::vector<int> played = onset.pitches;
    std::vector<const ExpectedNote*> written;
    written.reserve(moment.attack.notes.size());
    for (const ExpectedNote& note : moment.attack.notes)
    {
        written.push_back(&note);
    }

    // 1. Exact pitches.
    std::vector<const ExpectedNote*> unmatched;
    for (const ExpectedNote* note : written)
    {
        const auto found = std::find(played.begin(), played.end(), note->midiNoteNumber);
        if (found != played.end())
        {
            played.erase(found);
            NoteResult result = resultFor(*note, moment, part);
            result.outcome = NoteOutcome::correct;
            report.results.push_back(result);
            continue;
        }
        unmatched.push_back(note);
    }

    // 2. Nearest pitch, closest pair first; ties to the lower written note.
    // A choice that had an equal alternative, or a moment where more notes
    // were written than played or the other way round, is ambiguous.
    const bool unevenLeftovers =
        !unmatched.empty() && !played.empty() && unmatched.size() != played.size();
    std::sort(
        unmatched.begin(), unmatched.end(), [](const ExpectedNote* left, const ExpectedNote* right)
        { return left->midiNoteNumber < right->midiNoteNumber; });
    while (!unmatched.empty() && !played.empty())
    {
        // The closest pair; scanning written notes from the lowest, so a tie
        // goes to the lower one.
        int best = std::numeric_limits<int>::max();
        std::size_t bestNote = 0;
        std::size_t bestPlayed = 0;
        for (std::size_t note = 0; note < unmatched.size(); ++note)
        {
            for (std::size_t pitch = 0; pitch < played.size(); ++pitch)
            {
                const int distance = std::abs(unmatched[note]->midiNoteNumber - played[pitch]);
                if (distance < best)
                {
                    best = distance;
                    bestNote = note;
                    bestPlayed = pitch;
                }
            }
        }

        // Tied when another written note was as near this played pitch, or
        // this written note as near another played pitch.
        bool tied = false;
        for (std::size_t note = 0; note < unmatched.size(); ++note)
        {
            if (note != bestNote &&
                std::abs(unmatched[note]->midiNoteNumber - played[bestPlayed]) == best)
            {
                tied = true;
            }
        }
        for (std::size_t pitch = 0; pitch < played.size(); ++pitch)
        {
            if (pitch != bestPlayed &&
                std::abs(unmatched[bestNote]->midiNoteNumber - played[pitch]) == best)
            {
                tied = true;
            }
        }

        NoteResult result = resultFor(*unmatched[bestNote], moment, part);
        result.outcome = NoteOutcome::wrong;
        result.playedMidi = played[bestPlayed];
        result.ambiguous = tied || unevenLeftovers;
        report.results.push_back(result);
        unmatched.erase(unmatched.begin() + static_cast<std::ptrdiff_t>(bestNote));
        played.erase(played.begin() + static_cast<std::ptrdiff_t>(bestPlayed));
    }

    // 3. What is left: written and not played, played and not written.
    for (const ExpectedNote* note : unmatched)
    {
        NoteResult result = resultFor(*note, moment, part);
        result.outcome = NoteOutcome::missed;
        report.results.push_back(result);
    }
    for (const int pitch : played)
    {
        report.extra.push_back(ExtraNote{onset.frame, pitch});
    }
}
} // namespace

NoteReport matchNotes(const TimingReport& timing, std::size_t part)
{
    NoteReport report;
    for (const TimingResult& moment : timing.results)
    {
        if (!moment.onset.has_value())
        {
            for (const ExpectedNote& note : moment.attack.notes)
            {
                NoteResult result = resultFor(note, moment, part);
                result.outcome = NoteOutcome::missed;
                report.results.push_back(result);
            }
            continue;
        }
        if (moment.onset->pitches.empty())
        {
            for (const ExpectedNote& note : moment.attack.notes)
            {
                NoteResult result = resultFor(note, moment, part);
                result.outcome = NoteOutcome::pitchNotJudged;
                report.results.push_back(result);
            }
            continue;
        }
        compareMoment(moment, part, report);
    }

    for (const PerformedOnset& onset : timing.extra)
    {
        if (onset.pitches.empty())
        {
            report.extra.push_back(ExtraNote{onset.frame, std::nullopt});
            continue;
        }
        for (const int pitch : onset.pitches)
        {
            report.extra.push_back(ExtraNote{onset.frame, pitch});
        }
    }
    std::stable_sort(
        report.extra.begin(), report.extra.end(),
        [](const ExtraNote& left, const ExtraNote& right) { return left.frame < right.frame; });

    summarise(report);
    return report;
}

NoteReport judgeNotes(
    const take::Take& take,
    std::size_t part,
    const std::vector<PerformedOnset>& onsets,
    const TimingSettings& settings)
{
    return matchNotes(judgeTake(take, part, onsets, settings), part);
}

PitchContour contourOf(const take::Take& take)
{
    if (judgedFromKeyboard(take) || take.audio.empty())
    {
        return {};
    }
    return pitchContourOf(take.audio.data(), take.audio.size(), take.sampleRate, take.audioStart);
}

TakeEvidence evidenceOf(const take::Take& take, const OnsetSettings& onsets)
{
    TakeEvidence evidence;
    evidence.onsets = onsetsOf(take, onsets);
    evidence.contour = contourOf(take);
    return evidence;
}

void summarise(NoteReport& report)
{
    NoteSummary summary;
    for (const NoteResult& result : report.results)
    {
        switch (result.outcome)
        {
        case NoteOutcome::correct:
            ++summary.correct;
            break;
        case NoteOutcome::wrong:
            ++summary.wrong;
            break;
        case NoteOutcome::missed:
            ++summary.missed;
            break;
        case NoteOutcome::pitchNotJudged:
            ++summary.pitchNotJudged;
            break;
        }
        if (!result.intonation.has_value())
        {
            continue;
        }
        switch (result.intonation->verdict)
        {
        case IntonationVerdict::inTune:
            ++summary.inTune;
            break;
        case IntonationVerdict::sharp:
            ++summary.sharp;
            break;
        case IntonationVerdict::flat:
            ++summary.flat;
            break;
        case IntonationVerdict::uncertain:
            ++summary.uncertain;
            break;
        case IntonationVerdict::wrong:
            break;
        }
    }
    summary.extra = report.extra.size();
    report.summary = summary;
}

void judgeIntonations(
    NoteReport& report,
    const TimingReport& timing,
    const take::Take& take,
    std::size_t part,
    const PitchContour& contour,
    const IntonationSettings& settings)
{
    if (contour.frames.empty() || take.table == nullptr || take.table->empty() ||
        take.score == nullptr || part >= take.score->parts.size())
    {
        return;
    }
    const score::Part& written = take.score->parts[part];
    const std::int64_t latency = latencyFramesOf(take);

    // Where each moment's sound begins: its onset, or where one was expected.
    const auto soundAt = [&](std::size_t moment) -> std::int64_t
    {
        const TimingResult& result = timing.results[moment];
        return result.onset.has_value() ? result.onset->frame : result.expectedFrame + latency;
    };

    std::map<score::Tick, std::size_t> momentAt;
    for (std::size_t moment = 0; moment < timing.results.size(); ++moment)
    {
        momentAt.emplace(timing.results[moment].attack.tick, moment);
    }

    for (NoteResult& result : report.results)
    {
        if (result.outcome != NoteOutcome::pitchNotJudged)
        {
            continue;
        }
        const auto found = momentAt.find(result.tick);
        if (found == momentAt.end())
        {
            continue;
        }
        const std::size_t moment = found->second;
        const TimingResult& matched = timing.results[moment];
        if (!matched.onset.has_value() || matched.attack.notes.size() != 1)
        {
            // A chord, or two voices together: one contour hears one pitch.
            continue;
        }

        // The span: from the onset to the next moment's sound or the note's
        // written end, whichever is first (decision 2).
        const score::NoteRef& ref = result.ref;
        if (ref.measureIndex >= written.measures.size())
        {
            continue;
        }
        const score::Measure& measure = written.measures[ref.measureIndex];
        if (ref.voiceIndex >= measure.voices.size() ||
            ref.eventIndex >= measure.voices[ref.voiceIndex].events.size())
        {
            continue;
        }
        const score::ScoreEvent& event = measure.voices[ref.voiceIndex].events[ref.eventIndex];
        const score::Tick endTick = measure.start + event.onset + event.duration;
        std::int64_t to =
            transport::frameOfTick(*take.table, static_cast<double>(endTick)) + latency;
        if (moment + 1 < timing.results.size())
        {
            to = std::min(to, soundAt(moment + 1));
        }

        Intonation intonation = judgeIntonation(
            contour, matched.onset->frame, to, result.writtenMidi, take.referenceHz,
            take.sampleRate, settings);
        switch (intonation.verdict)
        {
        case IntonationVerdict::inTune:
        case IntonationVerdict::sharp:
        case IntonationVerdict::flat:
            result.outcome = NoteOutcome::correct;
            break;
        case IntonationVerdict::wrong:
            result.outcome = NoteOutcome::wrong;
            result.playedMidi = intonation.sungMidi;
            break;
        case IntonationVerdict::uncertain:
            break;
        }
        result.intonation = std::move(intonation);
    }

    for (ExtraNote& extra : report.extra)
    {
        if (!extra.midi.has_value())
        {
            extra.midi =
                pitchAfterOnset(contour, extra.frame, take.referenceHz, take.sampleRate, settings);
        }
    }
    summarise(report);
}

NoteReport judgeNotes(
    const take::Take& take,
    std::size_t part,
    const TakeEvidence& evidence,
    const TimingSettings& timing,
    const IntonationSettings& intonation)
{
    const TimingReport timed = judgeTake(take, part, evidence.onsets, timing);
    NoteReport report = matchNotes(timed, part);
    judgeIntonations(report, timed, take, part, evidence.contour, intonation);
    return report;
}

NoteReport judgeNotes(
    const take::Take& take,
    std::size_t part,
    const TimingSettings& timing,
    const IntonationSettings& intonation)
{
    return judgeNotes(take, part, evidenceOf(take, timing.onsets), timing, intonation);
}
} // namespace evaluation
