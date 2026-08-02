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

void MusicXmlDiagnosticSink::addUnsupported(
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    const auto existing = unsupportedIndices_.find(elementName);

    if (existing != unsupportedIndices_.end())
    {
        ++diagnostics_[existing->second].occurrences;

        return;
    }

    unsupportedIndices_.emplace(elementName, diagnostics_.size());
    diagnostics_.push_back(make(
        DiagnosticSeverity::unsupported, std::move(location), std::move(elementName),
        std::move(message)));
}

void MusicXmlDiagnosticSink::addRepair(
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    diagnostics_.push_back(make(
        DiagnosticSeverity::repaired, std::move(location), std::move(elementName),
        std::move(message)));
}

void MusicXmlDiagnosticSink::addInfo(
    DiagnosticLocation location,
    std::string elementName,
    std::string message)
{
    diagnostics_.push_back(make(
        DiagnosticSeverity::info, std::move(location), std::move(elementName), std::move(message)));
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
    unsupportedIndices_.clear();

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
