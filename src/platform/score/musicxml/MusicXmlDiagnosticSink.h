#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "platform/score/Diagnostic.h"

// Where the importer says what it did to a file.
//
// Two kinds of report go through here and they are handled differently on
// purpose:
//
//  - **Repairs** are recorded once each, as they happen, with whatever musical
//    location is known at the time. There is one per thing actually changed, and
//    a file that needs many of them is a file worth complaining about at length.
//  - **Unsupported constructs** and **unrecognised elements** are *aggregated by
//    element name*: one diagnostic per name, carrying an occurrence count and
//    the location of the first sighting. A Sibelius export contains thousands of
//    layout elements this importer has no use for, and a piano score has a slur
//    on nearly every phrase; one diagnostic per occurrence would bury every real
//    finding under them. That is task 7.2, and it is why this is a sink rather
//    than a bare vector.
//
// Nothing recorded here can fail an import. Dropping content is a diagnostic,
// never a status change (task 7.3).
namespace score::musicxml
{
class MusicXmlDiagnosticSink
{
  public:
    // A construct this importer recognises but does not support. The score no
    // longer says exactly what the file said, and this is what admits it.
    //
    // Deduplicated by element name: the first call fixes the message and the
    // location, and every later call for the same name only increments the
    // count. The message describes the *construct*, so repeating it per
    // occurrence adds nothing a count does not.
    void addUnsupported(DiagnosticLocation location, std::string elementName, std::string message);

    // A structural problem the importer fixed while reading.
    void addRepair(DiagnosticLocation location, std::string elementName, std::string message);

    void addInfo(DiagnosticLocation location, std::string elementName, std::string message);

    // An element this importer has no knowledge of at all. Counted by name; the
    // location is not kept, because the useful summary is "347 <print> elements
    // were ignored", not where each one was.
    void noteUnrecognisedElement(const std::string& elementName);

    // Everything recorded, with the unrecognised-element counts folded in as
    // one diagnostic per name. Named problems come first, in the order they
    // were found; the aggregated summaries follow, so the specific outranks the
    // bulk when a caller shows only the first few.
    [[nodiscard]] std::vector<Diagnostic> release();

    [[nodiscard]] bool empty() const noexcept
    {
        return diagnostics_.empty() && unrecognisedCounts_.empty();
    }

  private:
    std::vector<Diagnostic> diagnostics_;

    // Element name to its index in `diagnostics_`, for the unsupported
    // constructs that are deduplicated rather than repeated.
    std::map<std::string, std::size_t> unsupportedIndices_;

    // Ordered by name so the summary is stable across runs -- a test that
    // asserts on diagnostics should not depend on hash ordering.
    std::map<std::string, int> unrecognisedCounts_;
};
} // namespace score::musicxml
