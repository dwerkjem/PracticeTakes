#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "platform/score/Diagnostic.h"

// Where the importer says what it did to a file.
//
// Two kinds of report go through here and they are handled differently on
// purpose:
//
//  - **Identical diagnostics collapse.** Two reports agreeing on severity,
//    element name, code *and* arguments are the same finding seen twice, so
//    the first keeps its location and the rest only increment its count.
//    Distinct codes stay distinct, so "a tie with no end" never merges into "a
//    tie with no start" -- and whether two findings are the same no longer
//    depends on how either is worded.
//  - **Unrecognised elements** are aggregated by element name alone, since
//    there is nothing to say about them beyond how often they appeared.
//
// Without this, volume buries meaning. A Sibelius export carries thousands of
// layout elements; a piano score has a slur on nearly every phrase; and a
// Renaissance edition whose bars all exceed their mensuration sign produced 865
// identical repair messages, one per bar in one file. That is task 7.2, and it
// is why this is a sink rather than a bare vector.
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
    void addUnsupported(
        DiagnosticLocation location,
        std::string elementName,
        std::string_view code,
        l10n::NamedArguments arguments = {});

    // A structural problem the importer fixed while reading.
    void addRepair(
        DiagnosticLocation location,
        std::string elementName,
        std::string_view code,
        l10n::NamedArguments arguments = {});

    void addInfo(
        DiagnosticLocation location,
        std::string elementName,
        std::string_view code,
        l10n::NamedArguments arguments = {});

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
    void
    add(DiagnosticSeverity severity,
        DiagnosticLocation location,
        std::string elementName,
        std::string_view code,
        l10n::NamedArguments arguments);

    std::vector<Diagnostic> diagnostics_;

    // Severity, element name, code and arguments to that diagnostic's index in
    // `diagnostics_`, so an identical finding increments a count instead of
    // appending a duplicate.
    std::map<std::string, std::size_t> seen_;

    // Ordered by name so the summary is stable across runs -- a test that
    // asserts on diagnostics should not depend on hash ordering.
    std::map<std::string, int> unrecognisedCounts_;
};
} // namespace score::musicxml
