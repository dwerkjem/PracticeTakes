#pragma once

#include "../../MainComponent.h"

#include "ScoreImportSummary.h"

#include <functional>
#include <utility>

// The window that says what happened to a file the user opened.
//
// One window for both outcomes, per `design.md` § Open Questions: the
// diagnostics on a *successful* import matter as much as the error on a failed
// one, and putting the two halves of "what happened to my file" in different
// places would make the successful-but-lossy case the easiest one to miss.
//
// It contains **no musical drawing of any kind**. A summary that grows a staff
// preview would undo the reason the score model landed before the renderer: the
// model must not be shaped by a paint routine. Notation is #32's.
class MainComponent::ScoreImportWindow final : public juce::DocumentWindow
{
  public:
    explicit ScoreImportWindow(std::function<void()> closeHandler)
        : DocumentWindow("Score import", juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Content(), true);
        setResizable(true, true);
        setResizeLimits(420, 320, 1200, 1200);
        centreWithSize(620, 560);
        setVisible(true);
    }

    // An import has started. Shown rather than left blank so a large file does
    // not look like a command that did nothing.
    void showProgress(const juce::String& fileName)
    {
        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        {
            content->showProgress(fileName);
        }
    }

    void showSummary(const ScoreImportSummary& summary)
    {
        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        {
            content->showSummary(summary);
        }
    }

    // Follow a theme change. The look and feel is the application's one, so the
    // palette is already in it; what this adds is telling the window and its
    // contents to go and read it again, and giving the window chrome the
    // background the rest of the application is using.
    void applyAppearance(juce::LookAndFeel* appearance, juce::Colour background)
    {
        setLookAndFeel(appearance);
        setBackgroundColour(background);
        sendLookAndFeelChange();

        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        {
            content->refreshTextColour();
        }

        repaint();
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

  private:
    // The contents are a headline plus one read-only multi-line editor.
    //
    // An editor rather than a stack of labels because the diagnostics list has
    // no bound -- a Sibelius export can report hundreds of distinct elements --
    // and an editor scrolls. A truncated list that does not say it is truncated
    // would be worse than a long one, and this never truncates.
    class Content final : public juce::Component
    {
      public:
        Content()
        {
            headline.setJustificationType(juce::Justification::topLeft);
            headline.setFont(juce::FontOptions(18.0f, juce::Font::bold));
            addAndMakeVisible(headline);

            body.setMultiLine(true, false);
            body.setReadOnly(true);
            body.setScrollbarsShown(true);
            body.setCaretVisible(false);
            body.setPopupMenuEnabled(true);
            addAndMakeVisible(body);
        }

        void showProgress(const juce::String& fileName)
        {
            headline.setText(
                fileName.isEmpty() ? "Reading the score..." : "Reading " + fileName + "...",
                juce::dontSendNotification);
            body.setText({}, false);
        }

        void showSummary(const ScoreImportSummary& summary)
        {
            headline.setText(juce::String(summary.headline), juce::dontSendNotification);
            body.setText(juce::String::fromUTF8(scoreImportReport(summary).c_str()), false);
            body.moveCaretToTop(false);
        }

        // A TextEditor colours each run of text as it is inserted and does not
        // re-colour it when the look and feel changes, so a report written
        // before a theme change keeps the palette it was written under -- which
        // in the dark theme is dark grey on near-black, and unreadable. The
        // import is asynchronous, so whether the report or the theme arrived
        // first was a race: two captures of the same surface disagreed.
        void refreshTextColour()
        {
            // Read from the look and feel rather than through
            // `body.findColour`, and applied without pinning: a Component's own
            // colour map wins over the look and feel, so recolouring the text
            // *and* setting the editor's current colour would freeze it at
            // today's palette and make the next theme change a no-op. That is
            // not hypothetical -- it is what the first attempt at this did, and
            // the picture of it was the dark theme's report in the light
            // theme's grey.
            const juce::Colour colour =
                body.getLookAndFeel().findColour(juce::TextEditor::textColourId);
            body.applyColourToAllText(colour, false);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(16);
            headline.setBounds(bounds.removeFromTop(52));
            body.setBounds(bounds);
        }

      private:
        juce::Label headline;
        juce::TextEditor body;
    };

    std::function<void()> onClose;
};
