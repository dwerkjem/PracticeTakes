#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/NoteHit.h"
#include "features/score/engraving/ScoreLayoutEngine.h"

#include "support/ScoreFixtures.h"

#include <memory>

using namespace engraving;

TEST_CASE(
    "a point on a notehead finds it, and a point between finds nothing",
    "[score][engraving][hit]")
{
    auto source = std::make_shared<score::Score>();
    score::Part part;
    part.id = "P1";
    score::Measure measure;
    measure.printedNumber = "1";
    measure.nominalDuration = 4 * score::ticksPerQuarterNote;
    measure.attributes.clefs = {score::Clef{"G", 2, 0, 1}};
    measure.attributes.time = score::TimeSignature{4, 4};
    score::Voice voice;
    voice.number = 1;
    for (int beat = 0; beat < 4; ++beat)
    {
        score::ScoreEvent event;
        event.kind = score::EventKind::note;
        event.onset = beat * score::ticksPerQuarterNote;
        event.duration = score::ticksPerQuarterNote;
        event.notes = testing::score::notesOf({score::pitchFromSpelling(score::Step::b, 0, 4)});
        voice.events.push_back(event);
    }
    measure.voices.push_back(voice);
    part.measures.push_back(measure);
    source->parts.push_back(part);

    LayoutOptions options;
    options.availableWidth = 400.0;
    const ScoreLayout layout = layOutScore(source, options);
    REQUIRE(layout.systems.size() == 1);
    const System& system = layout.systems.front();
    REQUIRE(system.noteheads.size() == 4);

    // B4 sits on the middle line, two spaces below the top one.
    const NoteheadGlyph& third = system.noteheads[2];
    const double middle = LayoutOptions{}.noteheadWidth / 2.0;
    CHECK(noteheadAt(system, third.x + middle, 2.0) == std::optional<std::size_t>{2});
    CHECK(noteheadAt(system, third.x + middle + 0.5, 2.4) == std::optional<std::size_t>{2});

    // Well above the staff, or between two heads, is nothing.
    CHECK_FALSE(noteheadAt(system, third.x + middle, -3.0).has_value());
    const double between = (system.noteheads[1].x + third.x) / 2.0 + middle;
    if (third.x - system.noteheads[1].x > 3.0)
    {
        CHECK_FALSE(noteheadAt(system, between, 2.0).has_value());
    }
}
