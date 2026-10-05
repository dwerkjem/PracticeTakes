#include "../../MainComponent.h"

#include "ScoreImportWindow.h"

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

// The shell's half of the Open Score command: the File menu, the chooser,
// starting the import thread, and taking its result into the summary window.
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
        // nothing. The window is already open showing that file's progress, so
        // bringing it forward is the whole answer.
        showScoreImportWindow();
        scoreImportWindow->showProgress(scoreImportJob->fileBeingRead().getFileName());
        scoreImportWindow->toFront(true);

        return;
    }

    // Shown before the import finishes, so a large file does not look like a
    // command that did nothing.
    showScoreImportWindow();
    scoreImportWindow->showProgress(file.getFileName());
}

void MainComponent::finishScoreImport(const score::musicxml::MusicXmlImportResult& result)
{
    // A successful import becomes the current score; a failed one leaves the
    // score already open exactly as it was. ScoreImportState owns that rule and
    // is tested on it.
    const ScoreImportSummary& summary = scoreImport.apply(result);

    // The window may have been closed while the import ran -- the result is
    // still worth showing, and reopening it is what the user asked for by
    // opening the file.
    showScoreImportWindow();
    scoreImportWindow->showSummary(summary);
}

void MainComponent::showScoreImportWindow()
{
    if (scoreImportWindow != nullptr)
    {
        scoreImportWindow->setVisible(true);

        return;
    }

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    scoreImportWindow = std::make_unique<ScoreImportWindow>(
        [safeThis]
        {
            if (safeThis != nullptr)
            {
                safeThis->closeScoreImportWindow();
            }
        });

    // Both themes come from the one LookAndFeel the application owns, so the
    // window follows a theme change without knowing a theme exists.
    scoreImportWindow->setLookAndFeel(&appLookAndFeel);
    scoreImportWindow->toFront(true);
}

void MainComponent::closeScoreImportWindow()
{
    scoreImportWindow.reset();
}
