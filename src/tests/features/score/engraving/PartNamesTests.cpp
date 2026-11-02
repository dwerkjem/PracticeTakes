#include <catch2/catch_test_macros.hpp>

#include "features/score/engraving/PartNames.h"

using engraving::abbreviatedPartName;
using engraving::displayedPartName;

TEST_CASE("a part's printed name is the file's, trimmed", "[score][engraving][part-names]")
{
    CHECK(displayedPartName("Voice") == "Voice");
    CHECK(displayedPartName("  PIANO ") == "PIANO");

    // The corpus has parts named with a single space; that is no name at all.
    CHECK(displayedPartName(" ").empty());
    CHECK(displayedPartName("").empty());
}

TEST_CASE("the file's own abbreviation wins", "[score][engraving][part-names]")
{
    CHECK(abbreviatedPartName("Treble", "Tr.") == "Tr.");
    CHECK(abbreviatedPartName("Piano", "Klav.") == "Klav.");

    // A blank abbreviation is none, and one is derived instead.
    CHECK(abbreviatedPartName("Piano", "  ") == "Pno.");
}

TEST_CASE(
    "a known instrument takes its conventional abbreviation",
    "[score][engraving][part-names]")
{
    // The bundled sample: neither part carries an abbreviation.
    CHECK(abbreviatedPartName("Voice", "") == "Vo.");
    CHECK(abbreviatedPartName("Piano", "") == "Pno.");

    CHECK(abbreviatedPartName("PIANO", "") == "Pno.");
    CHECK(abbreviatedPartName("Pianoforte", "") == "Pf.");
    CHECK(abbreviatedPartName("Klavier", "") == "Kl.");
    CHECK(abbreviatedPartName("Gesang", "") == "Ges.");
    CHECK(abbreviatedPartName("Violoncello", "") == "Vc.");
    CHECK(abbreviatedPartName("Soprano", "") == "S.");
}

TEST_CASE("a part's number survives abbreviation", "[score][engraving][part-names]")
{
    // Two violins have to stay told apart on every system, not only the first.
    CHECK(abbreviatedPartName("Violin 1", "") == "Vln. 1");
    CHECK(abbreviatedPartName("Violin 2", "") == "Vln. 2");
    CHECK(abbreviatedPartName("Horn II", "") == "Hn. II");
    CHECK(abbreviatedPartName("Flugelhorn 2", "") == "Flu. 2");
}

TEST_CASE("an unknown name is shortened to its first letters", "[score][engraving][part-names]")
{
    CHECK(abbreviatedPartName("Theremin", "") == "The.");

    // Letters only, so bracketed names do not shorten to a bracket.
    CHECK(abbreviatedPartName("[CANTUS] [Falsetto]", "") == "CAN.");
}

TEST_CASE("a name that is already short stays as it is", "[score][engraving][part-names]")
{
    CHECK(abbreviatedPartName("Tr.", "") == "Tr.");
    CHECK(abbreviatedPartName("B.c.", "") == "B.c.");
    CHECK(abbreviatedPartName("Alt", "") == "Alt");
}

TEST_CASE("nothing to shorten gives nothing", "[score][engraving][part-names]")
{
    CHECK(abbreviatedPartName("", "").empty());
    CHECK(abbreviatedPartName("   ", "").empty());
}
