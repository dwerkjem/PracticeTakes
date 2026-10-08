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
