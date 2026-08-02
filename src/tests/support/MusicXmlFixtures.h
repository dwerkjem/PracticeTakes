#pragma once

#include <string>
#include <vector>

// Hand-written MusicXML for the importer tests.
//
// Per design decision 6 the synthetic fixtures are string literals, matching
// how every other test in this repository that needs a document builds one.
// They are what fails informatively when a rule breaks, because each one
// contains exactly the construct under test and nothing else. The committed
// MuseScore corpus covers the other half -- the assumptions we did not know we
// were making -- and lives in MusicXmlCorpusTests.
namespace testing::musicxml
{
// Wrap `body` in a minimal single-part score-partwise document.
//
// The DOCTYPE is the real one, external URL and all, because a real export
// carries it and the importer must not go anywhere near the network to resolve
// it. Leaving it out of the fixtures would mean never testing that.
inline std::string scoreDocument(
    const std::string& body,
    const std::string& partName = "Voice",
    const std::string& partId = "P1")
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">
<score-partwise version="4.0">
  <part-list>
    <score-part id=")" +
           partId + R"(">
      <part-name>)" +
           partName + R"(</part-name>
    </score-part>
  </part-list>
  <part id=")" +
           partId + R"(">
)" + body + R"(
  </part>
</score-partwise>
)";
}

// A score with several parts. `partBodies` supplies each part's measures; part
// ids are P1, P2, ... in order.
inline std::string multiPartDocument(const std::vector<std::string>& partBodies)
{
    std::string partList;
    std::string parts;

    for (std::size_t index = 0; index < partBodies.size(); ++index)
    {
        const std::string id = "P" + std::to_string(index + 1);

        partList += "    <score-part id=\"" + id + "\"><part-name>Part " +
                    std::to_string(index + 1) + "</part-name></score-part>\n";
        parts += "  <part id=\"" + id + "\">\n" + partBodies[index] + "\n  </part>\n";
    }

    return R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0">
  <part-list>
)" + partList +
           R"(  </part-list>
)" + parts +
           R"(</score-partwise>
)";
}

// <attributes> declaring divisions and a time signature, which nearly every
// fixture's first measure needs.
inline std::string attributes(int divisions, int beats, int beatType)
{
    return "      <attributes>\n"
           "        <divisions>" +
           std::to_string(divisions) +
           "</divisions>\n"
           "        <key><fifths>0</fifths></key>\n"
           "        <time><beats>" +
           std::to_string(beats) + "</beats><beat-type>" + std::to_string(beatType) +
           "</beat-type></time>\n"
           "        <clef><sign>G</sign><line>2</line></clef>\n"
           "      </attributes>\n";
}

// One <note>: a pitch, a duration in source divisions, and whatever extra
// child elements the test needs (a <tie>, a <chord/>, a <lyric>).
inline std::string note(
    const std::string& step,
    int octave,
    int duration,
    int voice = 1,
    const std::string& extra = {})
{
    return "      <note>\n" + extra + "        <pitch><step>" + step + "</step><octave>" +
           std::to_string(octave) +
           "</octave></pitch>\n"
           "        <duration>" +
           std::to_string(duration) +
           "</duration>\n"
           "        <voice>" +
           std::to_string(voice) +
           "</voice>\n"
           "      </note>\n";
}

inline std::string rest(int duration, int voice = 1)
{
    return "      <note>\n"
           "        <rest/>\n"
           "        <duration>" +
           std::to_string(duration) +
           "</duration>\n"
           "        <voice>" +
           std::to_string(voice) +
           "</voice>\n"
           "      </note>\n";
}

inline std::string backup(int duration)
{
    return "      <backup><duration>" + std::to_string(duration) + "</duration></backup>\n";
}

inline std::string forward(int duration)
{
    return "      <forward><duration>" + std::to_string(duration) + "</duration></forward>\n";
}

// A <measure> wrapper. `implicit` marks a pickup bar.
inline std::string
measure(const std::string& number, const std::string& body, bool implicit = false)
{
    return "    <measure number=\"" + number + "\"" + (implicit ? " implicit=\"yes\"" : "") +
           ">\n" + body + "    </measure>\n";
}
} // namespace testing::musicxml
