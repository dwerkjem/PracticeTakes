#pragma once

#include <JuceHeader.h>

#include <functional>
#include <utility>

class ToolOptionsButton final : public juce::TextButton
{
  public:
    ToolOptionsButton(
        juce::String presentationActionText,
        std::function<void()> presentationHandler,
        std::function<void()> feedbackHandler,
        std::function<void()> closeHandler)
        : presentationAction(std::move(presentationActionText)),
          onPresentation(std::move(presentationHandler)), onFeedback(std::move(feedbackHandler)),
          onClose(std::move(closeHandler))
    {
        setButtonText("...");
        setTitle("Tool options");
        setTooltip("Show tool options");
        onClick = [this] { showOptions(); };
    }

  private:
    enum MenuItem
    {
        changePresentation = 1,
        giveFeedback,
        close
    };

    void showOptions()
    {
        juce::PopupMenu menu;
        menu.addItem(changePresentation, presentationAction);
        menu.addItem(giveFeedback, "Give feedback");
        menu.addSeparator();
        menu.addItem(close, "Close tool");

        const auto safeThis = juce::Component::SafePointer<ToolOptionsButton>(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(180),
            [safeThis](int selectedItemId)
            {
                if (safeThis == nullptr)
                {
                    return;
                }

                std::function<void()> action;
                if (selectedItemId == changePresentation)
                {
                    action = safeThis->onPresentation;
                }
                else if (selectedItemId == giveFeedback)
                {
                    action = safeThis->onFeedback;
                }
                else if (selectedItemId == close)
                {
                    action = safeThis->onClose;
                }

                if (action)
                {
                    action();
                }
            });
    }

    juce::String presentationAction;
    std::function<void()> onPresentation;
    std::function<void()> onFeedback;
    std::function<void()> onClose;
};
