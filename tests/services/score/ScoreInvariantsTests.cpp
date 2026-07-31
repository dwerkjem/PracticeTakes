#include <catch2/catch_test_macros.hpp>

#include "services/score/ScoreBuilder.h"
#include "services/score/ScoreInvariants.h"

using namespace score;

namespace
{
constexpr Tick quarter = ticksPerQuarterNote;
constexpr Tick bar = ticksPerQuarterNote * 4;

[[nodiscard]] ScoreEvent noteAt(Tick onset, Tick duration, Step step = Step::c, int octave = 4)
{
    ScoreEvent event;
    event.kind = EventKind::note;
    event.onset = onset;
    event.duration = duration;
    event.pitches = {pitchFromSpelling(step, 0, octave)};

    return event;
}

[[nodiscard]] Measure barOf(std::vector<ScoreEvent> events, std::string number = "1")
{
    Voice voice;
    voice.number = 1;
    voice.events = std::move(events);

    Measure measure;
    measure.printedNumber = std::move(number);
    measure.nominalDuration = bar;
    measure.voices = {voice};

    return measure;
}

[[nodiscard]] Score scoreOf(std::vector<Measure> measures, std::string partId = "P1")
{
    Part part;
    part.id = std::move(partId);

    for (std::size_t index = 0; index < measures.size(); ++index)
    {
        measures[index].index = index;
        measures[index].start = static_cast<Tick>(index) * bar;
    }

    part.measures = std::move(measures);

    Score score;
    score.parts = {std::move(part)};

    return score;
}

[[nodiscard]] const Voice& firstVoice(const Score& score, std::size_t measureIndex = 0)
{
    return score.parts.front().measures[measureIndex].voices.front();
}
} // namespace

// --- Invariant 5: pitch consistency ------------------------------------------

TEST_CASE("a correctly spelled score needs no pitch repair", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, quarter)})});

    CHECK(enforceConsistentPitches(score) == 0);
    CHECK(score.diagnostics.empty());
}

TEST_CASE("a pitch contradicting its spelling is recomputed", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, quarter)})});

    // A note that renders on one line and sounds as another.
    score.parts.front().measures[0].voices[0].events[0].pitches[0].midiNoteNumber = 61;

    CHECK(enforceConsistentPitches(score) == 1);

    const Pitch& repaired = firstVoice(score).events[0].pitches.front();

    CHECK(repaired.midiNoteNumber == 60);
    CHECK(isConsistent(repaired));
    REQUIRE(score.diagnostics.size() == 1);
    CHECK(score.diagnostics.front().severity == DiagnosticSeverity::repaired);
}

// --- Invariant 3: durations ---------------------------------------------------

TEST_CASE("non-negative durations need no repair", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, quarter), noteAt(quarter, quarter)})});

    CHECK(enforceNonNegativeDurations(score) == 0);
}

TEST_CASE("a negative duration is clamped to zero", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, -quarter)})});

    CHECK(enforceNonNegativeDurations(score) > 0);

    // Clamped rather than dropped, so the note itself survives.
    REQUIRE(firstVoice(score).events.size() == 1);
    CHECK(firstVoice(score).events[0].duration == 0);
    CHECK_FALSE(score.diagnostics.empty());
}

TEST_CASE("a zero-length non-grace event is dropped", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, 0), noteAt(quarter, quarter)})});

    CHECK(enforceNonNegativeDurations(score) > 0);

    // It occupies no time, so nothing can render or play it.
    REQUIRE(firstVoice(score).events.size() == 1);
    CHECK(firstVoice(score).events[0].onset == quarter);
}

TEST_CASE("a grace note keeps its zero duration", "[score][invariants]")
{
    ScoreEvent grace = noteAt(0, 0, Step::d, 5);
    grace.isGrace = true;

    Score score = scoreOf({barOf({grace, noteAt(0, quarter)})});

    CHECK(enforceNonNegativeDurations(score) == 0);
    CHECK(firstVoice(score).events.size() == 2);
}

// --- Invariant 2: voice ordering and overlap ----------------------------------

TEST_CASE("an ordered voice needs no repair", "[score][invariants]")
{
    Score score = scoreOf(
        {barOf({noteAt(0, quarter), noteAt(quarter, quarter), noteAt(quarter * 2, quarter)})});

    CHECK(enforceVoiceOrdering(score) == 0);
}

TEST_CASE("out-of-order events are reordered", "[score][invariants]")
{
    // MusicXML writes voices with a <backup>/<forward> cursor, so events are
    // genuinely not in time order in the file.
    Score score = scoreOf({barOf({noteAt(quarter * 2, quarter), noteAt(0, quarter)})});

    CHECK(enforceVoiceOrdering(score) > 0);

    const Voice& voice = firstVoice(score);

    REQUIRE(voice.events.size() == 2);
    CHECK(voice.events[0].onset == 0);
    CHECK(voice.events[1].onset == quarter * 2);
}

TEST_CASE("overlapping events in one voice are shortened", "[score][invariants]")
{
    // A mis-handled backup shifts a voice by a fraction of a bar and nothing
    // crashes -- the score just plays wrong. This is the check that catches it.
    Score score = scoreOf({barOf({noteAt(0, quarter * 3), noteAt(quarter, quarter)})});

    CHECK(enforceVoiceOrdering(score) > 0);

    const Voice& voice = firstVoice(score);

    REQUIRE(voice.events.size() == 2);
    CHECK(endOf(voice.events[0]) == voice.events[1].onset);
}

TEST_CASE("a chord does not count as overlapping itself", "[score][invariants]")
{
    ScoreEvent chord;
    chord.kind = EventKind::chord;
    chord.onset = 0;
    chord.duration = bar;
    chord.pitches = {
        pitchFromSpelling(Step::c, 0, 4), pitchFromSpelling(Step::e, 0, 4),
        pitchFromSpelling(Step::g, 0, 4)};

    Score score = scoreOf({barOf({chord})});

    CHECK(enforceVoiceOrdering(score) == 0);
    CHECK(firstVoice(score).events[0].duration == bar);
}

TEST_CASE("a grace note sharing an onset is not treated as an overlap", "[score][invariants]")
{
    ScoreEvent grace = noteAt(0, 0, Step::d, 5);
    grace.isGrace = true;

    Score score = scoreOf({barOf({grace, noteAt(0, quarter)})});

    CHECK(enforceVoiceOrdering(score) == 0);
    CHECK(firstVoice(score).events.size() == 2);
}

// --- Invariant 7: measure bounds ----------------------------------------------

TEST_CASE("a voice that fits its measure needs no repair", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    CHECK(enforceMeasureBounds(score) == 0);
}

TEST_CASE("an event running past the barline is shortened", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(quarter * 3, quarter * 4)})});

    CHECK(enforceMeasureBounds(score) > 0);
    CHECK(endOf(firstVoice(score).events[0]) == bar);
}

TEST_CASE("events starting past the end of the measure are dropped", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, quarter), noteAt(bar + quarter, quarter)})});

    CHECK(enforceMeasureBounds(score) > 0);
    REQUIRE(firstVoice(score).events.size() == 1);
    CHECK(firstVoice(score).events[0].onset == 0);
}

TEST_CASE("a pickup measure may hold less than its time signature", "[score][invariants]")
{
    Measure pickup = barOf({noteAt(0, quarter)}, "0");
    pickup.isPickup = true;

    Score score = scoreOf({pickup});

    // Being short is exactly what a pickup is; it must not be repaired.
    CHECK(enforceMeasureBounds(score) == 0);
    CHECK(firstVoice(score).events.size() == 1);
}

// --- Invariant 1: cross-part measure alignment --------------------------------

TEST_CASE("aligned parts need no repair", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)}), barOf({noteAt(0, bar)}, "2")});

    Part second = score.parts.front();
    second.id = "P2";
    score.parts.push_back(second);

    CHECK(enforceMeasureAlignment(score) == 0);
}

TEST_CASE("a part short of measures is padded with rests", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)}), barOf({noteAt(0, bar)}, "2")});

    Part second;
    second.id = "P2";
    second.measures = {barOf({noteAt(0, bar)})};
    second.measures[0].index = 0;
    score.parts.push_back(second);

    CHECK(enforceMeasureAlignment(score) > 0);

    REQUIRE(score.parts[1].measures.size() == 2);

    const Voice& padded = score.parts[1].measures[1].voices.front();

    REQUIRE(padded.events.size() == 1);
    CHECK(padded.events[0].kind == EventKind::rest);
    CHECK(padded.events[0].duration == bar);
}

TEST_CASE("measure starts are recomputed so they cannot drift", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)}), barOf({noteAt(0, bar)}, "2")});

    // A start tick out of step with the measure lengths preceding it.
    score.parts.front().measures[1].start = 999;

    CHECK(enforceMeasureAlignment(score) > 0);
    CHECK(score.parts.front().measures[1].start == bar);
    CHECK(score.parts.front().measures[1].index == 1);
}

TEST_CASE("a disagreeing nominal duration takes the longest claim", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    Part second;
    second.id = "P2";
    second.measures = {barOf({noteAt(0, quarter * 3)})};
    second.measures[0].nominalDuration = quarter * 3;
    score.parts.push_back(second);

    CHECK(enforceMeasureAlignment(score) > 0);

    // No part's music gets truncated by another part's shorter bar.
    CHECK(score.parts[0].measures[0].nominalDuration == bar);
    CHECK(score.parts[1].measures[0].nominalDuration == bar);
}

// --- Invariant 6: part identifiers --------------------------------------------

TEST_CASE("unique non-empty part ids need no repair", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    Part second;
    second.id = "P2";
    score.parts.push_back(second);

    CHECK(enforceUniquePartIds(score) == 0);
}

TEST_CASE("a missing part id is generated", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})}, "");

    CHECK(enforceUniquePartIds(score) == 1);
    CHECK_FALSE(score.parts.front().id.empty());
    CHECK_FALSE(score.diagnostics.empty());
}

TEST_CASE("a duplicate part id is replaced", "[score][invariants]")
{
    // #39's session file names a selected part by this id, so two parts
    // sharing one is not survivable.
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    Part duplicate;
    duplicate.id = "P1";
    score.parts.push_back(duplicate);

    CHECK(enforceUniquePartIds(score) == 1);
    CHECK(score.parts[0].id == "P1");
    CHECK(score.parts[1].id != score.parts[0].id);
}

// --- Invariant 4: tie integrity -----------------------------------------------

TEST_CASE("a properly matched tie survives", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)}), barOf({noteAt(0, bar)}, "2")});

    score.parts.front().measures[0].voices[0].events[0].tiedTo = EventRef{1, 0, 0};
    score.parts.front().measures[1].voices[0].events[0].tiedFrom = EventRef{0, 0, 0};

    CHECK(enforceTieIntegrity(score) == 0);
    CHECK(score.parts.front().measures[0].voices[0].events[0].tiedTo.has_value());
}

TEST_CASE("a tie is matched by sounding pitch, not spelling", "[score][invariants]")
{
    // A file may legitimately tie a G-sharp to an A-flat across a barline.
    Score score = scoreOf({barOf({noteAt(0, bar)}), barOf({noteAt(0, bar)}, "2")});

    score.parts.front().measures[0].voices[0].events[0].pitches = {
        pitchFromSpelling(Step::g, 1, 4)};
    score.parts.front().measures[1].voices[0].events[0].pitches = {
        pitchFromSpelling(Step::a, -1, 4)};

    score.parts.front().measures[0].voices[0].events[0].tiedTo = EventRef{1, 0, 0};
    score.parts.front().measures[1].voices[0].events[0].tiedFrom = EventRef{0, 0, 0};

    CHECK(enforceTieIntegrity(score) == 0);
}

TEST_CASE("a tie with no matching end is dropped", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    score.parts.front().measures[0].voices[0].events[0].tiedTo = EventRef{7, 0, 0};

    CHECK(enforceTieIntegrity(score) > 0);
    CHECK_FALSE(score.parts.front().measures[0].voices[0].events[0].tiedTo.has_value());
    CHECK_FALSE(score.diagnostics.empty());
}

TEST_CASE("a tie whose ends disagree on pitch is dropped", "[score][invariants]")
{
    Score score =
        scoreOf({barOf({noteAt(0, bar, Step::c)}), barOf({noteAt(0, bar, Step::e)}, "2")});

    score.parts.front().measures[0].voices[0].events[0].tiedTo = EventRef{1, 0, 0};
    score.parts.front().measures[1].voices[0].events[0].tiedFrom = EventRef{0, 0, 0};

    CHECK(enforceTieIntegrity(score) > 0);
    CHECK_FALSE(score.parts.front().measures[0].voices[0].events[0].tiedTo.has_value());
}

TEST_CASE("a one-sided tie link is dropped", "[score][invariants]")
{
    // A half-linked tie is a null dereference waiting to happen in every
    // consumer, so it does not survive.
    Score score = scoreOf({barOf({noteAt(0, bar)}), barOf({noteAt(0, bar)}, "2")});

    score.parts.front().measures[0].voices[0].events[0].tiedTo = EventRef{1, 0, 0};

    CHECK(enforceTieIntegrity(score) > 0);
    CHECK_FALSE(score.parts.front().measures[0].voices[0].events[0].tiedTo.has_value());
}

// --- Invariant 8: tempo map ----------------------------------------------------

TEST_CASE("a well-formed tempo map needs no repair", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});
    score.tempoMap = TempoMap::build({{0, 90.0}});

    CHECK(enforceTempoMap(score) == 0);
}

TEST_CASE("an empty tempo map gains the default", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    CHECK(score.tempoMap.entries().size() == 1);
    CHECK(score.tempoMap.beatsPerMinuteAt(0) == defaultBeatsPerMinute);
}

// --- Invariant 9: diagnostic locations -----------------------------------------

TEST_CASE("a diagnostic naming real entities is left alone", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    Diagnostic diagnostic;
    diagnostic.location.partId = "P1";
    diagnostic.location.measureNumber = "1";
    diagnostic.location.voice = 1;
    score.diagnostics.push_back(diagnostic);

    CHECK(enforceDiagnosticLocations(score) == 0);
    CHECK(score.diagnostics.front().location.partId.has_value());
}

TEST_CASE("a diagnostic naming a missing entity loses that field", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, bar)})});

    Diagnostic diagnostic;
    diagnostic.location.partId = "P99";
    diagnostic.location.measureNumber = "404";
    diagnostic.location.voice = 12;
    diagnostic.message = "still worth showing";
    score.diagnostics.push_back(diagnostic);

    CHECK(enforceDiagnosticLocations(score) == 3);

    const Diagnostic& repaired = score.diagnostics.front();

    // The message survives -- it just cannot be pinned to a location.
    CHECK_FALSE(hasLocation(repaired.location));
    CHECK(repaired.message == "still worth showing");
}

// --- The whole pass and the builder --------------------------------------------

TEST_CASE("a valid score passes every invariant untouched", "[score][invariants]")
{
    Score score = scoreOf({barOf({noteAt(0, quarter), noteAt(quarter, quarter * 3)})});

    enforceInvariants(score);

    CHECK(score.diagnostics.empty());
    CHECK(firstVoice(score).events.size() == 2);
}

TEST_CASE("the full pass repairs a thoroughly broken score without throwing", "[score][invariants]")
{
    // A score arrives from a stranger's file, so "this file is wrong" has to
    // produce something a musician can still practise with.
    Score score = scoreOf({barOf({noteAt(quarter * 2, quarter * 9), noteAt(0, -quarter)})}, "");

    score.parts.front().measures[0].voices[0].events[0].pitches[0].midiNoteNumber = 7;
    score.parts.front().measures[0].voices[0].events[0].tiedTo = EventRef{9, 9, 9};

    REQUIRE_NOTHROW(enforceInvariants(score));

    CHECK_FALSE(score.diagnostics.empty());
    CHECK_FALSE(score.parts.front().id.empty());

    for (const ScoreEvent& event : firstVoice(score).events)
    {
        CHECK(event.duration >= 0);
        CHECK(endOf(event) <= score.parts.front().measures[0].nominalDuration);
        CHECK_FALSE(event.tiedTo.has_value());

        for (const Pitch& pitch : event.pitches)
        {
            CHECK(isConsistent(pitch));
        }
    }
}

TEST_CASE("the builder hands back an immutable shared score", "[score][builder]")
{
    ScoreBuilder builder;

    builder.draft() = scoreOf({barOf({noteAt(0, bar)})});
    builder.draft().metadata.workTitle = "Wachet auf";

    const std::shared_ptr<const Score> built = builder.build();

    REQUIRE(built != nullptr);
    CHECK(built->metadata.workTitle == "Wachet auf");
    CHECK(built->parts.size() == 1);

    // Sharing is a copy of the handle, not of the score, and every reader sees
    // the same immutable value.
    const std::shared_ptr<const Score> reader = built;

    CHECK(reader.get() == built.get());
    CHECK(built.use_count() == 2);
}

TEST_CASE("the builder applies the invariants on the way out", "[score][builder]")
{
    ScoreBuilder builder;

    builder.draft() = scoreOf({barOf({noteAt(0, bar * 2)})});

    const std::shared_ptr<const Score> built = builder.build();

    REQUIRE(built != nullptr);
    CHECK(built->parts.front().measures[0].voices[0].events[0].duration == bar);
    CHECK_FALSE(built->diagnostics.empty());
}

TEST_CASE("the builder is empty after building", "[score][builder]")
{
    // A second build must not hand out another mutable handle on the same data.
    ScoreBuilder builder;

    builder.draft() = scoreOf({barOf({noteAt(0, bar)})});

    const std::shared_ptr<const Score> first = builder.build();

    CHECK(first->parts.size() == 1);
    CHECK(builder.draft().parts.empty());
}

TEST_CASE("diagnostics recorded during construction survive the build", "[score][builder]")
{
    ScoreBuilder builder;

    builder.draft() = scoreOf({barOf({noteAt(0, bar)})});

    Diagnostic unsupported;
    unsupported.severity = DiagnosticSeverity::unsupported;
    unsupported.elementName = "transpose";
    unsupported.message = "transposing instruments are not supported";
    unsupported.location.partId = "P1";
    builder.addDiagnostic(unsupported);

    const std::shared_ptr<const Score> built = builder.build();

    REQUIRE(built->diagnostics.size() == 1);
    CHECK(built->diagnostics.front().elementName == "transpose");
    CHECK(built->diagnostics.front().severity == DiagnosticSeverity::unsupported);
}

TEST_CASE("freezing a hand-built score applies the same rules", "[score][builder]")
{
    const std::shared_ptr<const Score> built = freeze(scoreOf({barOf({noteAt(0, bar)})}, ""));

    REQUIRE(built != nullptr);
    CHECK_FALSE(built->parts.front().id.empty());
}
