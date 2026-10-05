#include "../../MainComponent.h"

namespace
{
// The File menu holds one item. `design.md` § Open Questions records why it is
// not being filled out speculatively: the menu's second item should be a
// command someone needs, not a slot filled because a menu with one item looks
// empty.
constexpr int openScoreMenuItemId = 1;

constexpr int fileMenuWidth = 220;

// Offered together rather than as separate filters. The importer decides what a
// file is by its content, not its extension, so a chooser stricter than the
// importer would hide files the importer would happily read.
constexpr const char* scoreFilePatterns = "*.musicxml;*.xml;*.mxl";
} // namespace

// The shell's half of the Open Score command: starting the import thread and
// taking its result. The summary window that renders the result is task group
// 4; until it exists the outcome is reported in an alert, which is the same
// thing the workspace actions do for a result that has no window of its own.
//
// Nothing here parses MusicXML. The importer lives in src/platform/score and
// runs on ScoreImportJob's thread; this file only starts it and files the
// answer.

void MainComponent::showFileMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&appLookAndFeel);
    menu.addItem(openScoreMenuItemId, "Open score...");

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&fileButton).withMinimumWidth(fileMenuWidth),
        [safeThis](int selectedItemId)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            if (selectedItemId == openScoreMenuItemId)
            {
                safeThis->openScore();
            }
        });
}

void MainComponent::openScore()
{
    scoreChooser = std::make_unique<juce::FileChooser>(
        "Open score", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        scoreFilePatterns, true);

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    scoreChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
            {
                return;
            }

            const juce::File chosen = chooser.getResult();
            safeThis->scoreChooser.reset();

            // Dismissed. Not an error, not a message, and emphatically not a
            // reason to disturb whatever score is already open.
            if (chosen == juce::File())
            {
                return;
            }

            safeThis->startScoreImport(chosen);
        });
}

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
