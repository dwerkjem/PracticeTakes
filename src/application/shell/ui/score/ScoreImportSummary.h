#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "platform/score/Diagnostic.h"
#include "platform/score/musicxml/MusicXmlImportResult.h"

// Pure logic (no JUCE dependency) for turning a MusicXML import result into the
// text the import summary window shows.
//
// Split out of the window per the `WorkspaceLayoutState` precedent: this is the
// only part of the Open Score command a unit test can reach without a display,
// so it is where the behaviour worth asserting lives -- which fields appear,
// how a length becomes bars and seconds, how diagnostics group, and what each
// failure status says. The window, the menu, and the import thread stay
// `Component`/`Thread` code outside `PracticeTakesTests`.
//
// Deliberately in the global namespace, matching the rest of the shell, so that
// `score::` keeps meaning the score model and nothing else.

// One labelled line of the summary. A label/value pair rather than a formatted
// paragraph because the window lays the pairs out and the test asserts on them;
// a field that has no value is left out of the vector entirely rather than
// rendered as a label with nothing after it.
struct ScoreImportField
{
    std::string label;
    std::string value;
};

// A part as the summary names it.
struct ScoreImportPartLine
{
    // The file's display name, falling back to the part id when the file names
    // no part. Invariant 6 guarantees the id is non-empty and unique, so this
    // is never blank -- an unnamed part still has to be identifiable, because
    // "two parts, neither named" is exactly the case someone is checking the
    // summary to understand.
    std::string name;

    int staffCount = 1;
};

// Total musical length, in both units the design asks for: bars because that is
// what a musician counts, seconds because it is the one number that proves the
// tempo map was read rather than defaulted.
struct ScoreImportLength
{
    // The measure count, not ticks divided by a bar length: with a mid-score
    // metre change there is no single bar length to divide by, and the number a
    // musician wants is the number of bars on the page.
    std::size_t bars = 0;

    double seconds = 0.0;
};

// One diagnostic, as a line of the summary.
struct ScoreImportDiagnosticLine
{
    score::DiagnosticSeverity severity = score::DiagnosticSeverity::info;

    std::string message;

    // "Soprano, bar 12, voice 1", or empty for a document-level diagnostic.
    // Resolved from the diagnostic's part id against the score's parts, so the
    // line names the part the user sees rather than "P2".
    std::string location;

    // The MusicXML element the diagnostic is about, or empty when it is not
    // about one specific element.
    std::string elementName;

    // Carried through rather than expanded into repeated lines: a Sibelius
    // export reports one unrecognised element thousands of times, and "seen
    // 2,114 times" is the useful form of that.
    int occurrences = 1;
};

// Diagnostics of one severity, in the order the importer reported them.
struct ScoreImportDiagnosticGroup
{
    score::DiagnosticSeverity severity = score::DiagnosticSeverity::info;
    std::vector<ScoreImportDiagnosticLine> lines;
};

struct ScoreImportSummary
{
    score::musicxml::MusicXmlImportStatus status =
        score::musicxml::MusicXmlImportStatus::structurallyInvalid;

    // Mirrors `score::musicxml::succeeded(status)`, carried so a consumer that
    // only has the summary does not have to re-derive it.
    bool succeeded = false;

    // A short sentence naming the outcome, different for every status. It does
    // not stand in for `detail`: collapsing the importer's nine failure
    // statuses into one generic line is what this summary exists to avoid.
    std::string headline;

    // The importer's own error message, verbatim and unparaphrased. Empty on
    // success.
    std::string detail;

    // Metadata and structural counts, already formatted, absent values omitted.
    // Empty when the import failed, because there is no score to describe.
    std::vector<ScoreImportField> fields;

    std::vector<ScoreImportPartLine> parts;

    std::size_t measureCount = 0;
    ScoreImportLength length;

    // Tempo in force at the start of the score. Invariant 8 guarantees the
    // tempo map is never empty, so this always has a value on success.
    double startingBeatsPerMinute = 0.0;

    // True when the import produced no diagnostics at all.
    bool cleanImport = false;

    // On a clean *successful* import, the explicit "nothing was dropped or
    // repaired" sentence. An empty diagnostics list and a failure to report
    // diagnostics look identical on screen, which would make the `imported`
    // versus `importedWithDiagnostics` distinction worth nothing. Empty when
    // there are diagnostics to show, and empty on a failure, where the headline
    // already says what happened and "nothing was dropped" would be nonsense.
    std::string diagnosticsMessage;

    // Grouped by severity in the order `score::DiagnosticSeverity` declares --
    // deterministic, and it invents no ranking between "unsupported" and
    // "repaired" that the model does not state. A severity with no diagnostics
    // produces no group.
    std::vector<ScoreImportDiagnosticGroup> diagnostics;
};

// The sentence shown for a status. Exhaustive over `MusicXmlImportStatus` on
// purpose: adding a status to the importer should fail to compile here rather
// than silently fall into a generic message.
[[nodiscard]] std::string scoreImportHeadline(score::musicxml::MusicXmlImportStatus status);

// Summarise an import result for display.
[[nodiscard]] ScoreImportSummary
summariseScoreImport(const score::musicxml::MusicXmlImportResult& result);
