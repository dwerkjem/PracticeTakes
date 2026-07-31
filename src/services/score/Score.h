#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Diagnostic.h"
#include "Measure.h"
#include "MusicalTime.h"
#include "TempoMap.h"

// The root of the normalized score model.
//
// Engraving-independent by construction: invariant 10 says the model holds no
// coordinates, fonts, page or system breaks, stem directions, or beam
// groupings. Anything positional is the renderer's to compute. That is #31's
// "independent of the rendering widget" criterion, stated as something a test
// can check.
//
// Ownership and threading (design.md § Ownership and threading):
//  - Import runs on a background thread and hands back a finished value.
//  - The result crosses to the message thread as std::shared_ptr<const Score>.
//    `const` is the enforcement: a score is fully built before its first share
//    and never mutated after, so any number of readers on any number of threads
//    need no lock.
//  - The audio thread never touches a Score. Not even to read it. A shared_ptr
//    copy is a contended atomic refcount write and the tree is pointer-chasing
//    over heap nodes -- neither belongs in a bounded callback. Playback
//    flattens what it needs into a preallocated POD array on the message thread
//    and publishes that instead, the same shape AudioSampleFifo already uses in
//    the other direction.
namespace score
{
// Where a part's source duration units changed, kept for diagnostics.
//
// MusicXML's <divisions> is declared per part and may be redeclared in any
// measure. The model rescales everything into `ticksPerQuarterNote`, so nothing
// downstream needs these -- but when a duration converts inexactly, the useful
// thing to tell a user is what the file actually said, so it is recorded.
struct SourceDivisionsChange
{
    std::size_t measureIndex = 0;
    int divisions = 1;
};

struct Part
{
    // The MusicXML <score-part> id, kept verbatim. #39's session file will need
    // to name a selected part stably across sessions, and the file's own id is
    // the only identifier that survives a re-import. Invariant 6: unique and
    // non-empty, generated with a diagnostic when the file's is not.
    std::string id;

    // Display name ("Soprano", "Piano") and its short form ("S.", "Pno.").
    std::string name;
    std::string abbreviation;

    // 1 for a vocal part, 2 for a typical piano part.
    int staffCount = 1;

    // Every <divisions> value this part declared, in source order. The first
    // entry is the part's initial declaration.
    std::vector<SourceDivisionsChange> sourceDivisions;

    std::vector<Measure> measures;
};

// Work and credit metadata from the head of the file.
struct ScoreMetadata
{
    std::string workTitle;
    std::string movementTitle;
    std::string composer;
    std::string lyricist;

    // MusicXML's <encoding><software>. Worth keeping and worth surfacing: it is
    // the first thing you want to know when a file misbehaves, because
    // exporter dialects differ more than the format suggests.
    std::string encodingSoftware;
};

struct Score
{
    ScoreMetadata metadata;

    // The tick base every duration and position in this score is denominated
    // in. Always `ticksPerQuarterNote`; stored explicitly so that a consumer
    // reading a Score never has to assume it, and so a future change of base is
    // detectable rather than silent.
    Tick ticksPerQuarterNote = score::ticksPerQuarterNote;

    std::vector<Part> parts;

    TempoMap tempoMap;

    // Everything the importer wanted to say about the file it read: constructs
    // dropped, structures repaired, elements not recognised. Invariant 9: these
    // never name a part, measure, or voice that is not in this score.
    std::vector<Diagnostic> diagnostics;
};

// Number of measures in the score. Invariant 1 requires every part to agree, so
// the first part's count is the score's count.
[[nodiscard]] inline std::size_t measureCount(const Score& score) noexcept
{
    return score.parts.empty() ? std::size_t{0} : score.parts.front().measures.size();
}

// Total musical length in ticks: the end of the last measure.
[[nodiscard]] inline Tick totalLength(const Score& score) noexcept
{
    if (score.parts.empty() || score.parts.front().measures.empty())
    {
        return 0;
    }

    const Measure& last = score.parts.front().measures.back();

    return last.start + last.nominalDuration;
}
} // namespace score
