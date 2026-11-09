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
    std::string_view code,
    l10n::NamedArguments arguments)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.location = std::move(location);
    diagnostic.elementName = std::move(elementName);
    diagnostic.code = std::string(code);
    diagnostic.arguments = std::move(arguments);

    return diagnostic;
}
} // namespace

void MusicXmlDiagnosticSink::add(
    DiagnosticSeverity severity,
    DiagnosticLocation location,
    std::string elementName,
    std::string_view code,
    l10n::NamedArguments arguments)
{
    // Keyed on everything but the location, so only genuinely identical
    // findings collapse. Keying on the element name alone would merge "a tie
    // with no end" into "a tie with no start", which are different problems
    // that happen to be about the same element.
    std::string key = std::to_string(static_cast<int>(severity)) + '\0' + elementName + '\0';
    key += code;
    for (const auto& [name, value] : arguments)
    {
        key += '\0';
        key += name;
        key += '=';
        key += value;
    }
    const auto existing = seen_.find(key);

    if (existing != seen_.end())
    {
        ++diagnostics_[existing->second].occurrences;

        return;
    }

    seen_.emplace(key, diagnostics_.size());
    diagnostics_.push_back(
        make(severity, std::move(location), std::move(elementName), code, std::move(arguments)));
}

void MusicXmlDiagnosticSink::addUnsupported(
    DiagnosticLocation location,
    std::string elementName,
    std::string_view code,
    l10n::NamedArguments arguments)
{
    add(DiagnosticSeverity::unsupported, std::move(location), std::move(elementName), code,
        std::move(arguments));
}

void MusicXmlDiagnosticSink::addRepair(
    DiagnosticLocation location,
    std::string elementName,
    std::string_view code,
    l10n::NamedArguments arguments)
{
    add(DiagnosticSeverity::repaired, std::move(location), std::move(elementName), code,
        std::move(arguments));
}

void MusicXmlDiagnosticSink::addInfo(
    DiagnosticLocation location,
    std::string elementName,
    std::string_view code,
    l10n::NamedArguments arguments)
{
    add(DiagnosticSeverity::info, std::move(location), std::move(elementName), code,
        std::move(arguments));
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
            l10n::key("score.import.unrecognised-element"), {});
        diagnostic.occurrences = count;
        released.push_back(std::move(diagnostic));
    }

    unrecognisedCounts_.clear();

    return released;
}
} // namespace score::musicxml
