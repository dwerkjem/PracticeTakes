#pragma once

#include <optional>
#include <string>

#include "MusicalTime.h"

// What the importer has to say about a file it read (no JUCE dependency).
//
// Per decision 5, a diagnostic's location is *musical*, not textual: "part P1,
// measure 12, voice 1, event 3" rather than "line 4127". That is what a
// musician can act on -- they can open the score and look at bar 12 -- and it is
// what survives the file being re-exported. If the parser turns out to expose a
// source position (see tasks.md 3.2), it is added alongside this, not instead
// of it.
//
// The printed measure number is a string on purpose. MusicXML measure numbers
// are not integers: a pickup is "0", a split bar is "12a", and some exporters
// leave them blank. Storing what was printed means the diagnostic names the bar
// the user sees on the page.
namespace score
{
enum class DiagnosticSeverity
{
    // Something the file asked for that the model could not represent, and the
    // score is now different from what the file said. Import still succeeds.
    unsupported,

    // A structural problem that was repaired to satisfy an invariant -- an
    // over-full measure truncated, a dangling tie dropped, a part id generated.
    // Import still succeeds; the score is complete but not faithful.
    repaired,

    // Informational: an exact-but-notable observation, such as an unrecognised
    // element summarised by name and count.
    info
};

// Where in the score a diagnostic applies. Every field is optional because a
// diagnostic may be document-level (no part), part-level (no measure), or
// measure-level (no voice). Invariant 9 requires that whatever is set actually
// exists in the score.
struct DiagnosticLocation
{
    std::optional<std::string> partId;

    // The measure number as printed in the file, not the zero-based index.
    std::optional<std::string> measureNumber;

    std::optional<int> voice;

    // Onset within the measure, in model ticks.
    std::optional<Tick> position;
};

struct Diagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::info;

    // Empty for a document-level diagnostic.
    DiagnosticLocation location;

    // The MusicXML element this is about ("transpose", "backup", ...), or empty
    // when the diagnostic is not about one specific element.
    std::string elementName;

    // Human-readable, aimed at a musician rather than a developer.
    std::string message;

    // How many times this was seen. Unrecognised elements are summarised once
    // per element name with a count (task 7.2) rather than once per occurrence,
    // because a Sibelius export otherwise produces thousands of them.
    int occurrences = 1;
};

[[nodiscard]] inline bool hasLocation(const DiagnosticLocation& location) noexcept
{
    return location.partId.has_value() || location.measureNumber.has_value() ||
           location.voice.has_value() || location.position.has_value();
}
} // namespace score
