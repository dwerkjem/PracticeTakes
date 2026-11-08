#include "ScoreCursor.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace engraving
{
namespace
{
// The measure of `part` holding `tick`: the last starting at or before it.
// Measures are contiguous and in order (invariant 1), so a binary search finds
// it. Nothing for a part with no measures or a tick before the first.
[[nodiscard]] std::optional<std::size_t> measureHolding(const score::Part& part, double tick)
{
    const auto& measures = part.measures;
    if (measures.empty() || tick < static_cast<double>(measures.front().start))
    {
        return std::nullopt;
    }

    const auto after = std::upper_bound(
        measures.begin(), measures.end(), tick, [](double value, const score::Measure& measure)
        { return value < static_cast<double>(measure.start); });
    return static_cast<std::size_t>(std::distance(measures.begin(), after) - 1);
}

[[nodiscard]] bool isTarget(const score::ScoreEvent& event)
{
    return score::isPitched(event) && !event.isGrace;
}
} // namespace

std::optional<CursorPlace> placeOfTick(const ScoreLayout& layout, double tick)
{
    std::optional<std::size_t> holding;
    for (std::size_t index = 0; index < layout.systems.size(); ++index)
    {
        const auto& anchors = layout.systems.at(index).timeAnchors;
        if (anchors.empty())
        {
            continue;
        }
        if (!holding.has_value() || static_cast<double>(anchors.front().tick) <= tick)
        {
            holding = index;
        }
        if (static_cast<double>(anchors.front().tick) > tick)
        {
            break;
        }
    }

    if (!holding.has_value())
    {
        return std::nullopt;
    }

    const auto& anchors = layout.systems.at(*holding).timeAnchors;
    const auto after = std::upper_bound(
        anchors.begin(), anchors.end(), tick, [](double value, const TimeAnchor& anchor)
        { return value < static_cast<double>(anchor.tick); });

    if (after == anchors.begin())
    {
        return CursorPlace{*holding, anchors.front().x};
    }
    if (after == anchors.end())
    {
        return CursorPlace{*holding, anchors.back().x};
    }

    const TimeAnchor& from = *std::prev(after);
    const TimeAnchor& to = *after;
    const double span = static_cast<double>(to.tick - from.tick);
    const double fraction = span > 0.0 ? (tick - static_cast<double>(from.tick)) / span : 0.0;
    return CursorPlace{*holding, from.x + (to.x - from.x) * fraction};
}

std::optional<score::Tick> onsetAtOrBefore(const System& system, double x)
{
    const auto& anchors = system.timeAnchors;
    if (anchors.empty())
    {
        return std::nullopt;
    }

    // Anchors are in tick order, and x rises with tick on a system, so the last
    // anchor at or left of `x` is found the same way. The final barline is an
    // anchor too, but it is the end of the music rather than an onset in it,
    // so a click past the last note starts from that note.
    const std::size_t onsets = anchors.size() > 1 ? anchors.size() - 1 : 1;
    std::size_t chosen = 0;
    for (std::size_t index = 0; index < onsets; ++index)
    {
        if (anchors.at(index).x <= x)
        {
            chosen = index;
        }
    }
    return anchors.at(chosen).tick;
}

Targets targetsAt(
    const score::Score& score,
    const std::vector<std::size_t>& parts,
    double tick,
    int lookAhead)
{
    Targets targets;
    const int wanted = std::clamp(lookAhead, 0, maximumLookAhead);

    // The next onsets after the tick, across every shown part, ascending.
    std::set<score::Tick> nextOnsets;

    for (const std::size_t partIndex : parts)
    {
        if (partIndex >= score.parts.size())
        {
            continue;
        }
        const score::Part& part = score.parts.at(partIndex);
        const std::optional<std::size_t> start = measureHolding(part, tick);
        const std::size_t first = start.value_or(0);

        // Sounding: an event never outlasts its own measure in the model (a
        // longer note is tied events), so the measure holding the tick has
        // every note sounding at it.
        if (start.has_value())
        {
            const score::Measure& measure = part.measures.at(first);
            for (std::size_t voice = 0; voice < measure.voices.size(); ++voice)
            {
                const auto& events = measure.voices.at(voice).events;
                for (std::size_t event = 0; event < events.size(); ++event)
                {
                    const score::ScoreEvent& candidate = events.at(event);
                    const double onset = static_cast<double>(measure.start + candidate.onset);
                    if (!isTarget(candidate) || onset > tick ||
                        tick >= onset + static_cast<double>(candidate.duration))
                    {
                        continue;
                    }
                    for (std::size_t note = 0; note < candidate.notes.size(); ++note)
                    {
                        targets.sounding.push_back(
                            TargetNote{partIndex, score::NoteRef{first, voice, event, note}});
                    }
                }
            }
        }

        if (wanted == 0)
        {
            continue;
        }

        // Upcoming: this part's next `wanted` attacks. Merged with the other
        // parts' below, and the earliest `wanted` of all of them kept.
        std::set<score::Tick> partOnsets;
        for (std::size_t index = first; index < part.measures.size(); ++index)
        {
            const score::Measure& measure = part.measures.at(index);
            if (static_cast<int>(partOnsets.size()) >= wanted &&
                measure.start > *partOnsets.rbegin())
            {
                break;
            }
            for (const score::Voice& voice : measure.voices)
            {
                for (const score::ScoreEvent& candidate : voice.events)
                {
                    const score::Tick onset = measure.start + candidate.onset;
                    const bool attacks = std::any_of(
                        candidate.notes.begin(), candidate.notes.end(),
                        [](const score::Note& note) { return !note.tiedFrom.has_value(); });
                    if (isTarget(candidate) && attacks && static_cast<double>(onset) > tick)
                    {
                        partOnsets.insert(onset);
                    }
                }
            }
        }
        nextOnsets.insert(partOnsets.begin(), partOnsets.end());
    }

    if (wanted == 0 || nextOnsets.empty())
    {
        return targets;
    }

    auto last = nextOnsets.begin();
    std::advance(
        last, std::min<std::size_t>(static_cast<std::size_t>(wanted), nextOnsets.size()) - 1);
    const score::Tick through = *last;

    for (const std::size_t partIndex : parts)
    {
        if (partIndex >= score.parts.size())
        {
            continue;
        }
        const score::Part& part = score.parts.at(partIndex);
        const std::size_t first = measureHolding(part, tick).value_or(0);
        for (std::size_t index = first; index < part.measures.size(); ++index)
        {
            const score::Measure& measure = part.measures.at(index);
            if (measure.start > through)
            {
                break;
            }
            for (std::size_t voice = 0; voice < measure.voices.size(); ++voice)
            {
                const auto& events = measure.voices.at(voice).events;
                for (std::size_t event = 0; event < events.size(); ++event)
                {
                    const score::ScoreEvent& candidate = events.at(event);
                    const score::Tick onset = measure.start + candidate.onset;
                    if (!isTarget(candidate) || static_cast<double>(onset) <= tick ||
                        onset > through || nextOnsets.count(onset) == 0)
                    {
                        continue;
                    }
                    for (std::size_t note = 0; note < candidate.notes.size(); ++note)
                    {
                        if (!candidate.notes.at(note).tiedFrom.has_value())
                        {
                            targets.upcoming.push_back(
                                TargetNote{partIndex, score::NoteRef{index, voice, event, note}});
                        }
                    }
                }
            }
        }
    }

    return targets;
}

std::vector<std::size_t> drawnPartsOf(const ScoreLayout& layout)
{
    std::vector<std::size_t> parts;
    if (layout.systems.empty())
    {
        return parts;
    }
    for (const std::size_t part : layout.systems.front().staffParts)
    {
        if (std::find(parts.begin(), parts.end(), part) == parts.end())
        {
            parts.push_back(part);
        }
    }
    return parts;
}
} // namespace engraving
