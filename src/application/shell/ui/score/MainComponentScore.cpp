#include "../../MainComponent.h"

// The shell's half of the Open Score command: starting the import thread and
// taking its result. The summary window that renders the result is task group
// 4; until it exists the outcome is reported in an alert, which is the same
// thing the workspace actions do for a result that has no window of its own.
//
// Nothing here parses MusicXML. The importer lives in src/platform/score and
// runs on ScoreImportJob's thread; this file only starts it and files the
// answer.

void MainComponent::startScoreImport(const juce::File& file)
{
    if (scoreImportJob == nullptr)
    {
        // Created on first use: a session that never opens a score never
        // creates an import thread. `*this` is the owner the job holds a
        // SafePointer to, and the job is a member of it, so it cannot outlive
        // the component it calls back into.
        scoreImportJob = std::make_unique<ScoreImportJob>(
            *this, [this](const score::musicxml::MusicXmlImportResult& result)
            { finishScoreImport(result); });
    }

    if (!scoreImportJob->start(file))
    {
        // One import at a time. Naming the file being read is the point --
        // refusing the second command silently would look like the click did
        // nothing.
        const juce::File running = scoreImportJob->fileBeingRead();

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon, "Still reading a score",
            running == juce::File()
                ? "A score is already being read. Try again in a moment."
                : "Still reading " + running.getFileName() + ". Try again once it has finished.",
            "OK");

        return;
    }
}

void MainComponent::finishScoreImport(const score::musicxml::MusicXmlImportResult& result)
{
    // A successful import becomes the current score; a failed one leaves the
    // score already open exactly as it was. ScoreImportState owns that rule and
    // is tested on it.
    const ScoreImportSummary& summary = scoreImport.apply(result);

    juce::String message = summary.headline;

    if (!summary.detail.empty())
    {
        message += "\n\n" + juce::String(summary.detail);
    }

    if (summary.succeeded)
    {
        for (const ScoreImportField& field : summary.fields)
        {
            message += "\n" + juce::String(field.label) + ": " + juce::String(field.value);
        }

        if (!summary.diagnosticsMessage.empty())
        {
            message += "\n\n" + juce::String(summary.diagnosticsMessage);
        }

        for (const ScoreImportDiagnosticGroup& group : summary.diagnostics)
        {
            for (const ScoreImportDiagnosticLine& line : group.lines)
            {
                message += "\n" + juce::String(line.message);

                if (!line.location.empty())
                {
                    message += " (" + juce::String(line.location) + ")";
                }

                if (line.occurrences > 1)
                {
                    message += " x" + juce::String(line.occurrences);
                }
            }
        }
    }

    juce::AlertWindow::showMessageBoxAsync(
        summary.succeeded ? juce::MessageBoxIconType::InfoIcon
                          : juce::MessageBoxIconType::WarningIcon,
        summary.succeeded ? "Score opened" : "That score could not be opened", message, "OK");
}
