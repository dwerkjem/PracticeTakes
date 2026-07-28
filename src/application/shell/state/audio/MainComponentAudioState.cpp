#include "../../MainComponent.h"

#include "../../ui/main_window/MicrophoneWarning.h"

bool MainComponent::hasUsableMicrophone() const
{
    return audioInputService.hasUsableInput();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioInputService)
    {
        updateMicrophoneStateControl();
        updateMicrophoneWarning();
    }
}

void MainComponent::updateMicrophoneStateControl()
{
    const auto palette = appPaletteFor(currentTheme);
    auto colour = palette.accent;
    juce::String text;
    juce::String tooltip;

    switch (audioInputService.inputState())
    {
    case AudioInputService::InputState::disconnected:
        text = "Mic disconnected";
        tooltip = "No microphone is available; open Settings to choose an input";
        colour = palette.muted;
        break;
    case AudioInputService::InputState::muted:
        text = "Mic muted - Unmute";
        tooltip = "Resume audio analysis using the selected microphone";
        colour = palette.warning;
        break;
    case AudioInputService::InputState::clipping:
        text = "Mic clipping - Mute";
        tooltip = "The microphone level is clipping; click to mute all analysis tools";
        colour = juce::Colours::red;
        break;
    case AudioInputService::InputState::active:
        text = "Mic active - Mute";
        tooltip = "Mute the microphone for every analysis tool";
        break;
    }

    microphoneButton.setButtonText(text);
    microphoneButton.setTooltip(tooltip);
    microphoneButton.setColour(juce::TextButton::buttonColourId, colour.withAlpha(0.75f));
    microphoneButton.setColour(juce::TextButton::buttonOnColourId, colour);
    microphoneButton.setColour(juce::TextButton::textColourOffId, palette.foreground);
    microphoneButton.setColour(juce::TextButton::textColourOnId, palette.foreground);
}

void MainComponent::updateMicrophoneWarning()
{
    if (microphoneWarning == nullptr)
    {
        return;
    }

    if (hasUsableMicrophone())
    {
        isMicrophoneWarningDismissed = false;
        microphoneWarning->setVisible(false);
        return;
    }

    microphoneWarning->setVisible(!isMicrophoneWarningDismissed);
    if (microphoneWarning->isVisible())
    {
        microphoneWarning->toFront(false);
    }
}

void MainComponent::dismissMicrophoneWarning()
{
    isMicrophoneWarningDismissed = true;
    if (microphoneWarning != nullptr)
    {
        microphoneWarning->setVisible(false);
    }
}
