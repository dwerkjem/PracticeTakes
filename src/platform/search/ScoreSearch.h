#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../score/PartTimeline.h"
#include "../score/Score.h"

// What of the open score can be searched for (no JUCE dependency;
// openspec/changes/archive/2026-11-10-everything-search, decision 5): its sung lines, by their
// words, and its bars, by number. Choosing one moves the practice clock there.
//
// The words are the application's to put into a sentence ("Bar 12, Voice"),
// so this returns them as data and the shell formats them.
namespace search
{
// One phrase of one part's lyrics: from a note where the singer breathes to
// the next, its syllables joined into words.
struct LyricPhrase
{
    std::size_t partIndex = 0;
    std::string partName;
    score::Tick tick = 0;

    // The bar the phrase starts in, as printed: "12", or "1a" for a score that
    // numbers its own.
    std::string bar;

    // "Wenn mein Schatz Hochzeit macht" -- the hyphens a word is split with
    // taken out.
    std::string text;
};

// Every phrase of every part that has lyrics, part by part, in time order.
[[nodiscard]] inline std::vector<LyricPhrase> lyricPhrasesOf(const score::Score& source)
{
    std::vector<LyricPhrase> phrases;
    for (std::size_t partIndex = 0; partIndex < source.parts.size(); ++partIndex)
    {
        const score::Part& part = source.parts[partIndex];
        LyricPhrase current;
        bool joinNext = false;
        const auto finish = [&phrases, &current]
        {
            if (!current.text.empty())
            {
                phrases.push_back(current);
            }
            current.text.clear();
        };
        for (const score::TimelineItem& item : score::timelineOf(source, partIndex))
        {
            if (!item.isNote())
            {
                continue;
            }
            if (item.phraseStart)
            {
                finish();
                joinNext = false;
                current.partIndex = partIndex;
                current.partName = part.name;
                current.tick = item.start;
                current.bar.clear();
                if (item.ref.has_value() && item.ref->measureIndex < part.measures.size())
                {
                    current.bar = part.measures[item.ref->measureIndex].printedNumber;
                }
            }
            if (item.lyric.empty())
            {
                continue;
            }
            // A syllable ending in a hyphen goes on into the next one.
            std::string syllable = item.lyric;
            const bool continues = syllable.back() == '-';
            if (continues)
            {
                syllable.pop_back();
            }
            if (!current.text.empty() && !joinNext)
            {
                current.text += ' ';
            }
            current.text += syllable;
            joinNext = continues;
        }
        finish();
    }
    return phrases;
}

// A bar of the score, by the number it is printed with.
struct ScoreBar
{
    std::size_t measureIndex = 0;
    std::string bar;
    score::Tick tick = 0;
};

// Every bar, from the first part's measures, which every part shares.
[[nodiscard]] inline std::vector<ScoreBar> barsOf(const score::Score& source)
{
    std::vector<ScoreBar> bars;
    if (source.parts.empty())
    {
        return bars;
    }
    const auto& measures = source.parts.front().measures;
    bars.reserve(measures.size());
    for (std::size_t index = 0; index < measures.size(); ++index)
    {
        const auto& measure = measures[index];
        bars.push_back(
            {index,
             measure.printedNumber.empty() ? std::to_string(index + 1) : measure.printedNumber,
             measure.start});
    }
    return bars;
}
} // namespace search
