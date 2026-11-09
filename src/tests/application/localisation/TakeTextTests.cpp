#include <catch2/catch_test_macros.hpp>

#include "application/localisation/TakeText.h"

#include <memory>
#include <vector>

// The English the take and its review are told in. The platform layer keeps
// the facts; these check the sentences built from them.

namespace
{
[[nodiscard]] juce::String utf8(const char* text)
{
    return juce::String::fromUTF8(text);
}
} // namespace

TEST_CASE("every state of a take has its phrase", "[take][localisation]")
{
    REQUIRE(l10n::setLanguage("en"));
    using take::Take;
    using take::TakeEvent;

    Take take;
    take.id = 3;
    take.sampleRate = 100.0;
    take.lastFrame = 4200;
    CHECK(l10n::takeStatus(take) == "Starting");

    take.lifecycle.apply(TakeEvent::countInStarted);
    CHECK(l10n::takeStatus(take) == "Counting in");
    take.lifecycle.apply(TakeEvent::performanceStarted);
    CHECK(l10n::takeStatus(take) == "Recording 0:42");
    take.lifecycle.apply(TakeEvent::paused);
    CHECK(l10n::takeStatus(take) == "Paused at 0:42");

    Take stopped = take;
    stopped.stoppedInBar = 13;
    stopped.lifecycle.apply(TakeEvent::stopped);
    CHECK(l10n::takeStatus(stopped) == "Take 3 stopped in bar 14, 0:42");

    Take complete = take;
    complete.lifecycle.apply(TakeEvent::reachedEnd);
    CHECK(l10n::takeStatus(complete) == "Take 3 complete, 0:42");

    Take failed = take;
    failed.lifecycle.apply(TakeEvent::failed, "take.failed.score-changed");
    CHECK(l10n::takeStatus(failed) == "Take 3 failed: another score was opened");

    Take discarded;
    discarded.id = 4;
    discarded.lifecycle.apply(TakeEvent::cancelled);
    CHECK(l10n::takeStatus(discarded) == "Take 4 discarded");
}

TEST_CASE("a review row says where, what and how", "[review][localisation]")
{
    REQUIRE(l10n::setLanguage("en"));
    using namespace evaluation;

    take::Take recorded;
    NoteReport report;

    NoteResult correct;
    correct.writtenMidi = 67;
    correct.outcome = NoteOutcome::correct;
    correct.timing = TimingVerdict::onTime;
    report.results.push_back(correct);

    NoteResult wrong;
    wrong.writtenMidi = 68;
    wrong.outcome = NoteOutcome::wrong;
    wrong.playedMidi = 71;
    wrong.timing = TimingVerdict::late;
    wrong.timingErrorMs = 50.4;
    report.results.push_back(wrong);

    NoteResult missed;
    missed.writtenMidi = 67;
    missed.outcome = NoteOutcome::missed;
    report.results.push_back(missed);

    NoteResult sharp;
    sharp.writtenMidi = 67;
    sharp.outcome = NoteOutcome::correct;
    sharp.intonation = Intonation{};
    sharp.intonation->verdict = IntonationVerdict::sharp;
    sharp.intonation->cents = 18.2;
    sharp.intonation->octave = -1;
    report.results.push_back(sharp);

    report.extra.push_back(ExtraNote{0, 80});

    const auto row = [&](std::int64_t bar, int beat, std::optional<std::size_t> result,
                         std::optional<std::size_t> extra)
    {
        ReviewRow made;
        made.bar = bar;
        made.beat = beat;
        made.result = result;
        made.extra = extra;
        return l10n::reviewRow(recorded, report, made);
    };

    CHECK(row(0, 0, 0, {}) == utf8("Bar 1, beat 1 \xC2\xB7 G4 \xC2\xB7 correct \xC2\xB7 on time"));
    CHECK(
        row(0, 2, 1, {}) ==
        utf8("Bar 1, beat 3 \xC2\xB7 G#4 \xC2\xB7 wrong, played B4 \xC2\xB7 50 ms late"));
    CHECK(row(0, 3, 2, {}) == utf8("Bar 1, beat 4 \xC2\xB7 G4 \xC2\xB7 missed"));
    CHECK(
        row(1, 0, 3, {}) ==
        utf8("Bar 2, beat 1 \xC2\xB7 G4 \xC2\xB7 18 \xC2\xA2 sharp \xC2\xB7 an octave low"));
    CHECK(row(0, 1, {}, 0) == utf8("Bar 1, beat 2 \xC2\xB7 G#5 \xC2\xB7 extra"));
}

TEST_CASE("a review's summary counts what is not zero", "[review][localisation]")
{
    REQUIRE(l10n::setLanguage("en"));
    using namespace evaluation;

    CHECK(l10n::reviewSummary(NoteReport{}) == "Nothing to judge");

    NoteReport report;
    for (int note = 0; note < 3; ++note)
    {
        NoteResult result;
        result.tick = 0;
        result.outcome = NoteOutcome::correct;
        result.timing = TimingVerdict::late;
        report.results.push_back(result);
    }
    report.summary.correct = 3;
    // A chord's late counts once.
    CHECK(l10n::reviewSummary(report) == utf8("3 correct \xC2\xB7 1 late"));

    report.summary.wrong = 1;
    report.summary.missed = 2;
    report.summary.extra = 1;
    CHECK(
        l10n::reviewSummary(report) ==
        utf8("3 correct \xC2\xB7 1 wrong \xC2\xB7 2 missed \xC2\xB7 1 extra \xC2\xB7 1 late"));
}
