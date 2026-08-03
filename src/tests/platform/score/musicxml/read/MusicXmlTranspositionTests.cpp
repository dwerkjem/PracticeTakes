#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "platform/score/musicxml/MusicXmlImporter.h"
#include "support/MusicXmlFixtures.h"

// Transposing instruments.
//
// A B-flat clarinet's written C sounds a B-flat; a horn in F sounds a fifth
// below what it reads. The model stores **both** pitches on every note rather
// than storing the transposition and asking consumers to apply it, because a
// consumer that forgot would put a whole wind section a tone out with nothing
// crashing to say so.
//
// Every case here asserts the *spelling* as well as the sounding number. That
// is the half a chromatic-only transposition gets wrong: shifting a written C
// down two semitones without also moving it down a diatonic step yields
// A-sharp, which sounds identical and sits on the wrong staff line with the
// wrong accidental. MusicXML carries two numbers for exactly this reason.
using namespace score;
using namespace score::musicxml;
using namespace testing::musicxml;

namespace
{
// An <attributes> block carrying a transposition, which the plain fixture
// helper does not.
std::string transposingAttributes(
    int diatonic,
    int chromatic,
    int octaveChange = 0,
    const std::string& staffAttribute = {})
{
    return "      <attributes>\n"
           "        <divisions>1</divisions>\n"
           "        <key><fifths>0</fifths></key>\n"
           "        <time><beats>4</beats><beat-type>4</beat-type></time>\n"
           "        <clef><sign>G</sign><line>2</line></clef>\n"
           "        <transpose" +
           staffAttribute + "><diatonic>" + std::to_string(diatonic) + "</diatonic><chromatic>" +
           std::to_string(chromatic) + "</chromatic>" +
           (octaveChange != 0
                ? "<octave-change>" + std::to_string(octaveChange) + "</octave-change>"
                : "") +
           "</transpose>\n"
           "      </attributes>\n";
}

const Note& onlyNote(const Score& score, std::size_t measureIndex = 0)
{
    return score.parts.front().measures[measureIndex].voices.front().events.front().notes.front();
}

std::shared_ptr<const Score> importOrFail(const std::string& document)
{
    const MusicXmlImportResult result = importMusicXmlDocument(document);

    REQUIRE(succeeded(result.status));
    REQUIRE(result.score != nullptr);

    return result.score;
}
} // namespace

TEST_CASE("a B-flat instrument sounds a tone below what it reads", "[score][musicxml][transpose]")
{
    // The commonest transposition there is: clarinet, trumpet, tenor sax.
    const std::string body = measure("1", transposingAttributes(-1, -2) + note("C", 4, 4));

    const auto score = importOrFail(scoreDocument(body));
    const Note& note = onlyNote(*score);

    // Written: middle C, exactly as the file says.
    CHECK(note.written.step == Step::c);
    CHECK(note.written.alter == 0);
    CHECK(note.written.octave == 4);
    CHECK(note.written.midiNoteNumber == 60);

    // Sounding: B-flat below it -- **spelled** B-flat, not A-sharp. Same key on
    // a piano, different staff line and different accidental, and only the
    // diatonic half of the transposition gets it right.
    CHECK(note.sounding.step == Step::b);
    CHECK(note.sounding.alter == -1);
    CHECK(note.sounding.octave == 3);
    CHECK(note.sounding.midiNoteNumber == 58);

    CHECK_FALSE(soundsAsWritten(note));
    CHECK(isConsistent(note.written));
    CHECK(isConsistent(note.sounding));
}

TEST_CASE("a horn in F sounds a fifth below what it reads", "[score][musicxml][transpose]")
{
    const std::string body = measure("1", transposingAttributes(-4, -7) + note("C", 4, 4));

    const auto score = importOrFail(scoreDocument(body));
    const Note& note = onlyNote(*score);

    CHECK(note.written.midiNoteNumber == 60);
    CHECK(note.sounding.step == Step::f);
    CHECK(note.sounding.alter == 0);
    CHECK(note.sounding.octave == 3);
    CHECK(note.sounding.midiNoteNumber == 53);
}

TEST_CASE("an octave change is applied on top of the interval", "[score][musicxml][transpose]")
{
    // A tenor saxophone reads like a B-flat instrument but sounds a ninth
    // below, not a second. <octave-change> is what separates the two, and
    // ignoring it puts the part an octave out while every accidental still
    // looks right.
    const std::string body = measure("1", transposingAttributes(-1, -2, -1) + note("C", 4, 4));

    const auto score = importOrFail(scoreDocument(body));
    const Note& note = onlyNote(*score);

    CHECK(note.sounding.step == Step::b);
    CHECK(note.sounding.alter == -1);
    CHECK(note.sounding.octave == 2);
    CHECK(note.sounding.midiNoteNumber == 46);
}

TEST_CASE("a non-transposing part sounds as it is written", "[score][musicxml][transpose]")
{
    const std::string body = measure(
        "1", attributes(1, 4, 4) + note("F", 4, 2) +
                 "      <note><pitch><step>G</step><alter>1</alter>"
                 "<octave>4</octave></pitch><duration>2</duration>"
                 "<voice>1</voice></note>\n");

    const auto score = importOrFail(scoreDocument(body));
    const Voice& voice = score->parts.front().measures.front().voices.front();

    for (const ScoreEvent& event : voice.events)
    {
        for (const Note& note : event.notes)
        {
            CHECK(soundsAsWritten(note));
            CHECK(note.written == note.sounding);
        }
    }
}

TEST_CASE(
    "a transposition stays in force after the measure that declares it",
    "[score][musicxml][transpose]")
{
    const std::string body = measure("1", transposingAttributes(-1, -2) + note("C", 4, 4)) +
                             measure("2", note("D", 4, 4)) + measure("3", note("E", 4, 4));

    const auto score = importOrFail(scoreDocument(body));

    CHECK(onlyNote(*score, 0).sounding.midiNoteNumber == 58);
    CHECK(onlyNote(*score, 1).sounding.midiNoteNumber == 60);
    CHECK(onlyNote(*score, 2).sounding.midiNoteNumber == 62);

    // Written pitches are untouched throughout.
    CHECK(onlyNote(*score, 1).written.step == Step::d);
    CHECK(onlyNote(*score, 1).written.octave == 4);
}

TEST_CASE(
    "a mid-score transposition change applies from where it appears",
    "[score][musicxml][transpose]")
{
    // A clarinettist swapping from the A instrument to the B-flat one partway
    // through a movement. Both bars are written the same and must sound a
    // semitone apart.
    const std::string body =
        measure("1", transposingAttributes(-2, -3) + note("C", 4, 4)) +
        measure(
            "2", "      <attributes><transpose><diatonic>-1</diatonic>"
                 "<chromatic>-2</chromatic></transpose></attributes>\n" +
                     note("C", 4, 4));

    const auto score = importOrFail(scoreDocument(body));

    // Clarinet in A: written C sounds A.
    CHECK(onlyNote(*score, 0).sounding.step == Step::a);
    CHECK(onlyNote(*score, 0).sounding.midiNoteNumber == 57);

    // Clarinet in B-flat: the same written C now sounds B-flat.
    CHECK(onlyNote(*score, 1).sounding.step == Step::b);
    CHECK(onlyNote(*score, 1).sounding.alter == -1);
    CHECK(onlyNote(*score, 1).sounding.midiNoteNumber == 58);

    // The change is recorded on the measure it happened in, for a renderer that
    // wants to relabel the staff.
    REQUIRE(score->parts.front().measures[1].attributes.transpositions.size() == 1);
    CHECK(score->parts.front().measures[1].attributes.transpositions.front().chromatic == -2);
}

TEST_CASE("a per-staff transposition applies only to its own staff", "[score][musicxml][transpose]")
{
    // MusicXML allows <transpose number="..."> so one part can hold staves that
    // transpose differently.
    const std::string attributesNode =
        "      <attributes>\n"
        "        <divisions>1</divisions>\n"
        "        <staves>2</staves>\n"
        "        <time><beats>4</beats><beat-type>4</beat-type></time>\n"
        "        <transpose number=\"2\"><diatonic>-1</diatonic>"
        "<chromatic>-2</chromatic></transpose>\n"
        "      </attributes>\n";
    const std::string onStaffOne =
        "      <note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration>"
        "<voice>1</voice><staff>1</staff></note>\n";
    const std::string onStaffTwo =
        "      <note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration>"
        "<voice>2</voice><staff>2</staff></note>\n";

    const auto score = importOrFail(
        scoreDocument(measure("1", attributesNode + onStaffOne + backup(4) + onStaffTwo)));
    const Measure& bar = score->parts.front().measures.front();

    REQUIRE(bar.voices.size() == 2);

    // Staff 1 is untransposed; staff 2 is the B-flat instrument.
    const Note& first = bar.voices[0].events.front().notes.front();
    const Note& second = bar.voices[1].events.front().notes.front();

    CHECK(soundsAsWritten(first));
    CHECK(first.sounding.midiNoteNumber == 60);

    CHECK_FALSE(soundsAsWritten(second));
    CHECK(second.written.midiNoteNumber == 60);
    CHECK(second.sounding.midiNoteNumber == 58);
}

TEST_CASE("a doubling instrument sounds an extra octave down", "[score][musicxml][transpose]")
{
    // <double/> means "and also an octave lower", used where an instrument is
    // notated at one octave and played at another.
    const std::string attributesNode =
        "      <attributes>\n"
        "        <divisions>1</divisions>\n"
        "        <time><beats>4</beats><beat-type>4</beat-type></time>\n"
        "        <transpose><diatonic>0</diatonic><chromatic>0</chromatic><double/></transpose>\n"
        "      </attributes>\n";

    const std::string body = measure("1", attributesNode + note("C", 4, 4));
    const auto score = importOrFail(scoreDocument(body));
    const Note& doubled = onlyNote(*score);

    CHECK(doubled.written.midiNoteNumber == 60);
    CHECK(doubled.sounding.midiNoteNumber == 48);
    CHECK(doubled.sounding.step == Step::c);
    CHECK(doubled.sounding.octave == 3);
}

TEST_CASE("a tie on a transposing part matches on sounding pitch", "[score][musicxml][transpose]")
{
    // Invariant 4 matches ties by sounding pitch. Both ends transpose by the
    // same amount here, so the tie must survive -- but it is worth pinning,
    // because matching on the wrong pitch would break every tie in every wind
    // part at once.
    const std::string body =
        measure(
            "1",
            transposingAttributes(-1, -2) + note("C", 4, 4, 1, "        <tie type=\"start\"/>\n")) +
        measure("2", note("C", 4, 4, 1, "        <tie type=\"stop\"/>\n"));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    const Note& start = onlyNote(*result.score, 0);
    const Note& stop = onlyNote(*result.score, 1);

    REQUIRE(start.tiedTo.has_value());
    REQUIRE(stop.tiedFrom.has_value());
    CHECK(start.sounding.midiNoteNumber == 58);
    CHECK(stop.sounding.midiNoteNumber == 58);
}

TEST_CASE(
    "a transposing part no longer reports itself as unsupported",
    "[score][musicxml][transpose]")
{
    // This used to be a documented gap: <transpose> was dropped and every
    // affected part carried a diagnostic warning that it would sound in the
    // wrong key. Now that it is read, that diagnostic must not appear -- a
    // warning about a problem that has been fixed is worse than none.
    const std::string body = measure("1", transposingAttributes(-1, -2) + note("C", 4, 4));

    const MusicXmlImportResult result = importMusicXmlDocument(scoreDocument(body));

    REQUIRE(succeeded(result.status));

    for (const Diagnostic& diagnostic : result.diagnostics)
    {
        CHECK(diagnostic.elementName != "transpose");
    }
}
