#pragma once

#include "../../../../features/feedback/FeedbackComponent.h"
#include "../../MainComponent.h"

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
        {
            feedback->setContextTag(context);
        }
    }

    // Follow a theme change. The palette is already in the application's one
    // look and feel; what this adds is telling the window and the form inside
    // it to read it again, and giving the window chrome the background the rest
    // of the application is using.
    void applyAppearance(juce::LookAndFeel* appearance, juce::Colour background)
    {
        setLookAndFeel(appearance);
        setBackgroundColour(background);

        // Reaches FeedbackComponent::lookAndFeelChanged, which re-colours what
        // has already been typed into the form.
        sendLookAndFeelChange();
        repaint();
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

  private:
    std::function<void()> onClose;
};
