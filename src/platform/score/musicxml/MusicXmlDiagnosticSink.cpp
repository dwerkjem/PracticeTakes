#include "MusicXmlDiagnosticSink.h"

#include <utility>

namespace score::musicxml
{
namespace
{
Diagnostic make(
    DiagnosticSeverity severity,
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.location = std::move(location);
    diagnostic.elementName = std::move(elementName);
    diagnostic.message = std::move(message);

    return diagnostic;
}
} // namespace

void MusicXmlDiagnosticSink::add(
    DiagnosticSeverity severity,
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    // Keyed on all three fields, so only genuinely identical findings collapse.
    // Keying on the element name alone would merge "a tie with no end" into "a
    // tie with no start", which are different problems that happen to be about
    // the same element.
    const std::string key =
        std::to_string(static_cast<int>(severity)) + '\0' + elementName + '\0' + message;
    const auto existing = seen_.find(key);

    if (existing != seen_.end())
    {
        ++diagnostics_[existing->second].occurrences;

        return;
    }

    seen_.emplace(key, diagnostics_.size());
    diagnostics_.push_back(
        make(severity, std::move(location), std::move(elementName), std::move(message)));
}

void MusicXmlDiagnosticSink::addUnsupported(
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    add(DiagnosticSeverity::unsupported, std::move(location), std::move(elementName),
        std::move(message));
}

void MusicXmlDiagnosticSink::addRepair(
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    add(DiagnosticSeverity::repaired, std::move(location), std::move(elementName),
        std::move(message));
}

void MusicXmlDiagnosticSink::addInfo(
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    add(DiagnosticSeverity::info, std::move(location), std::move(elementName), std::move(message));
}

void MusicXmlDiagnosticSink::noteUnrecognisedElement(const std::string& elementName)
{
    if (elementName.empty())
    {
        return;
    }

    ++unrecognisedCounts_[elementName];
}

std::vector<Diagnostic> MusicXmlDiagnosticSink::release()
{
    std::vector<Diagnostic> released = std::move(diagnostics_);
    diagnostics_.clear();
    seen_.clear();

    released.reserve(released.size() + unrecognisedCounts_.size());

    for (const auto& [elementName, count] : unrecognisedCounts_)
    {
        Diagnostic diagnostic = make(
            DiagnosticSeverity::info, {}, elementName,
            "The importer does not read this element, so it was ignored.");
        diagnostic.occurrences = count;
        released.push_back(std::move(diagnostic));
    }

    unrecognisedCounts_.clear();

    return released;
}
} // namespace score::musicxml
