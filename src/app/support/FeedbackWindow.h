#pragma once

#include "../../feedback/FeedbackComponent.h"
#include "../MainComponent.h"

#include <functional>
#include <utility>

class MainComponent::FeedbackWindow final : public juce::DocumentWindow
{
  public:
    FeedbackWindow(
        juce::PropertiesFile& propertiesFile,
        const juce::String& initialContext,
        std::function<void()> closeHandler)
        : DocumentWindow(
              "Send feedback",
              juce::Colours::darkgrey,
              juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new FeedbackComponent(propertiesFile, initialContext), true);
        setResizable(true, true);
        setResizeLimits(620, 760, 1200, 1200);
        centreWithSize(760, 900);
        setVisible(true);
    }

    void setContextTag(const juce::String& context)
    {
        if (auto* feedback = dynamic_cast<FeedbackComponent*>(getContentComponent()))
            feedback->setContextTag(context);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

  private:
    std::function<void()> onClose;
};
