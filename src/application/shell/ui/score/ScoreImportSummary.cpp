#include "ScoreImportSummary.h"

#include <array>
#include <cmath>
#include <sstream>
#include <string>

#include "platform/score/Score.h"

namespace
{
using score::Diagnostic;
using score::DiagnosticSeverity;
using score::Score;

// Append `field` only when it has something to say. Task: a score with no
// composer must not show "Composer:" followed by nothing.
void appendIfPresent(
    std::vector<ScoreImportField>& fields,
    std::string label,
    const std::string& value)
{
    if (value.empty())
    {
        return;
    }

    fields.push_back({std::move(label), value});
}

// Seconds as a musician reads a stopwatch: m:ss, with an hours field only when
// there is one. Rounded to the nearest second -- the summary is a sanity check
// against the page, not a measurement.
[[nodiscard]] std::string formatClockTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
    {
        seconds = 0.0;
    }

    const long long total = std::llround(seconds);
    const long long hours = total / 3600;
    const long long minutes = (total % 3600) / 60;
    const long long remainingSeconds = total % 60;

    std::ostringstream text;

    if (hours > 0)
    {
        text << hours << ':' << (minutes < 10 ? "0" : "") << minutes;
    }
    else
    {
        text << minutes;
    }

    text << ':' << (remainingSeconds < 10 ? "0" : "") << remainingSeconds;

    return text.str();
}

// A tempo marking the way a score prints one: whole numbers stay whole, and a
// tempo that is not a whole number keeps one decimal rather than being rounded
// into a different marking.
[[nodiscard]] std::string formatBeatsPerMinute(double beatsPerMinute)
{
    const double rounded = std::round(beatsPerMinute);

    std::ostringstream text;

    if (std::abs(beatsPerMinute - rounded) < 0.05)
    {
        text << static_cast<long long>(rounded);
    }
    else
    {
        text.setf(std::ios::fixed, std::ios::floatfield);
        text.precision(1);
        text << beatsPerMinute;
    }

    text << " BPM";

    return text.str();
}

// How a part is named in the summary, and in a diagnostic's location.
[[nodiscard]] std::string displayNameOf(const score::Part& part)
{
    return part.name.empty() ? part.id : part.name;
}

// "Soprano, bar 12, voice 1", or empty for a document-level diagnostic.
//
// The location's tick position is deliberately left out: model ticks are not a
// unit anyone can act on, and the bar number -- printed as the file printed it,
// so "0" for a pickup and "12a" for a split bar survive -- is what a user opens
// the score and looks up. If a finer position is ever wanted it belongs as a
// beat within the bar, which needs the measure's metre and is a separate piece
// of work.
[[nodiscard]] std::string formatLocation(const Diagnostic& diagnostic, const Score* score)
{
    if (!score::hasLocation(diagnostic.location))
    {
        return {};
    }

    std::vector<std::string> pieces;

    if (diagnostic.location.partId.has_value())
    {
        const std::string& partId = *diagnostic.location.partId;
        std::string name = partId;

        // Resolve the id to the name the user sees. A failed import has no
        // score to resolve against, so the id stands in -- which is still the
        // id the file used, and so still findable.
        if (score != nullptr)
        {
            for (const score::Part& part : score->parts)
            {
                if (part.id == partId)
                {
                    name = displayNameOf(part);
                    break;
                }
            }
        }

        pieces.push_back(name);
    }

    if (diagnostic.location.measureNumber.has_value())
    {
        pieces.push_back("bar " + *diagnostic.location.measureNumber);
    }

    if (diagnostic.location.voice.has_value())
    {
        pieces.push_back("voice " + std::to_string(*diagnostic.location.voice));
    }

    std::string location;

    for (std::size_t index = 0; index < pieces.size(); ++index)
    {
        if (index > 0)
        {
            location += ", ";
        }

        location += pieces[index];
    }

    return location;
}

// What each group of diagnostics is called on screen. Named for what the
// severity means for the score in front of the user rather than with the enum's
// own word: "repaired" alone does not say that the score is complete but no
// longer faithful to the file.
[[nodiscard]] const char* severityHeading(const DiagnosticSeverity severity)
{
    switch (severity)
    {
    case DiagnosticSeverity::unsupported:
        return "Content this importer does not support, so the score differs from the file:";

    case DiagnosticSeverity::repaired:
        return "Structures repaired to make the score usable:";

    case DiagnosticSeverity::info:
        return "Notes about the file:";
    }

    return "Notes about the file:";
}

// Severities in the order `DiagnosticSeverity` declares them. Grouping needs
// *an* order; taking the model's own avoids inventing a ranking between
// "unsupported" and "repaired" that nothing in the model states.
constexpr std::array<DiagnosticSeverity, 3> severityOrder{
    DiagnosticSeverity::unsupported, DiagnosticSeverity::repaired, DiagnosticSeverity::info};

[[nodiscard]] std::vector<ScoreImportDiagnosticGroup>
groupBySeverity(const std::vector<Diagnostic>& diagnostics, const Score* score)
{
    std::vector<ScoreImportDiagnosticGroup> groups;

    for (const DiagnosticSeverity severity : severityOrder)
    {
        ScoreImportDiagnosticGroup group;
        group.severity = severity;

        // One pass per severity, so the importer's order is preserved within
        // each group without sorting -- a stable sort would do the same thing
        // less obviously and at the cost of a comparator.
        for (const Diagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.severity != severity)
            {
                continue;
            }

            group.lines.push_back(
                {diagnostic.severity, diagnostic.message, formatLocation(diagnostic, score),
                 diagnostic.elementName, diagnostic.occurrences});
        }

        if (!group.lines.empty())
        {
            groups.push_back(std::move(group));
        }
    }

    return groups;
}
} // namespace

std::string scoreImportHeadline(const score::musicxml::MusicXmlImportStatus status)
{
    using score::musicxml::MusicXmlImportStatus;

    // No `default`: a new importer status should stop compiling here rather
    // than quietly inherit a generic message, which is the failure this whole
    // summary exists to prevent.
    switch (status)
    {
    case MusicXmlImportStatus::imported:
        return "The score was imported.";

    case MusicXmlImportStatus::importedWithDiagnostics:
        return "The score was imported, with notes about the file.";

    case MusicXmlImportStatus::notFound:
        return "That file no longer exists.";

    case MusicXmlImportStatus::unreadable:
        return "That file could not be read.";

    case MusicXmlImportStatus::tooLarge:
        return "That file is too large to import.";

    case MusicXmlImportStatus::notMusicXml:
        return "That file is not a MusicXML score.";

    case MusicXmlImportStatus::malformedXml:
        return "That file is not valid XML.";

    case MusicXmlImportStatus::invalidContainer:
        return "That compressed score's container is inconsistent.";

    case MusicXmlImportStatus::unsupportedDocumentType:
        return "That is a MusicXML document of a kind this importer does not read.";

    case MusicXmlImportStatus::structurallyInvalid:
        return "That MusicXML score's structure could not be read.";
    }

    return "That file could not be imported.";
}

ScoreImportSummary summariseScoreImport(const score::musicxml::MusicXmlImportResult& result)
{
    ScoreImportSummary summary;

    summary.status = result.status;
    summary.succeeded = score::musicxml::succeeded(result.status);
    summary.headline = scoreImportHeadline(result.status);

    // The importer's message, not a paraphrase of it. It is already written for
    // a user and it names specifics -- the container entry that was missing,
    // the element that was not understood -- that no re-wording here could add.
    summary.detail = result.error;

    // Diagnostics come from the result rather than from `Score::diagnostics`,
    // because the result carries them on failure too and a malformed document
    // still has something to say about where it went wrong.
    const Score* const score = result.score.get();

    summary.cleanImport = result.diagnostics.empty();
    summary.diagnostics = groupBySeverity(result.diagnostics, score);

    if (summary.cleanImport && summary.succeeded)
    {
        summary.diagnosticsMessage = "Nothing was dropped or repaired.";
    }

    if (score == nullptr)
    {
        return summary;
    }

    appendIfPresent(summary.fields, "Work", score->metadata.workTitle);
    appendIfPresent(summary.fields, "Movement", score->metadata.movementTitle);
    appendIfPresent(summary.fields, "Composer", score->metadata.composer);
    appendIfPresent(summary.fields, "Lyricist", score->metadata.lyricist);

    // Shown with the metadata rather than tucked away: exporter dialects differ
    // more than the format suggests, so this is the first thing anyone needs
    // when a file misbehaves.
    appendIfPresent(summary.fields, "Written by", score->metadata.encodingSoftware);

    summary.parts.reserve(score->parts.size());

    for (const score::Part& part : score->parts)
    {
        summary.parts.push_back({displayNameOf(part), part.staffCount});
    }

    summary.measureCount = score::measureCount(*score);
    summary.length.bars = summary.measureCount;
    summary.length.seconds = score->tempoMap.tickToSeconds(score::totalLength(*score));
    summary.startingBeatsPerMinute = score->tempoMap.beatsPerMinuteAt(0);

    summary.fields.push_back({"Parts", std::to_string(summary.parts.size())});
    summary.fields.push_back({"Measures", std::to_string(summary.measureCount)});
    summary.fields.push_back(
        {"Length", std::to_string(summary.length.bars) + " bars, " +
                       formatClockTime(summary.length.seconds)});
    summary.fields.push_back({"Tempo", formatBeatsPerMinute(summary.startingBeatsPerMinute)});

    return summary;
}

std::string scoreImportReport(const ScoreImportSummary& summary)
{
    std::ostringstream report;

    // The importer's message leads, because on a failure it is the only thing
    // that says anything specific about the file.
    if (!summary.detail.empty())
    {
        report << summary.detail << "\n";
    }

    for (const ScoreImportField& field : summary.fields)
    {
        report << field.label << ": " << field.value << "\n";
    }

    if (!summary.parts.empty())
    {
        report << "\n";

        for (const ScoreImportPartLine& part : summary.parts)
        {
            report << "  " << part.name;

            // Only worth saying for a part that has more than one staff: "1
            // staff" on every vocal line is noise.
            if (part.staffCount > 1)
            {
                report << " (" << part.staffCount << " staves)";
            }

            report << "\n";
        }
    }

    if (!summary.diagnosticsMessage.empty())
    {
        report << "\n" << summary.diagnosticsMessage << "\n";
    }

    for (const ScoreImportDiagnosticGroup& group : summary.diagnostics)
    {
        report << "\n" << severityHeading(group.severity) << "\n";

        for (const ScoreImportDiagnosticLine& line : group.lines)
        {
            report << "  ";

            // The element the diagnostic is about, when it names one.
            //
            // Not decoration. A recognised-but-dropped element describes
            // itself in prose ("Slurs are phrasing marks..."), but an
            // *unrecognised* one shares a single generic message with every
            // other unrecognised element, and `elementName` is then the only
            // thing telling two lines apart. Without this a MuseScore export
            // renders as seven identical rows of "The importer does not read
            // this element, so it was ignored" -- which names nothing the user
            // could go and look at.
            if (!line.elementName.empty())
            {
                report << "<" << line.elementName << "> ";
            }

            report << line.message;

            if (!line.location.empty())
            {
                report << " [" << line.location << "]";
            }

            if (line.occurrences > 1)
            {
                report << " (seen " << line.occurrences << " times)";
            }

            report << "\n";
        }
    }

    return report.str();
}
