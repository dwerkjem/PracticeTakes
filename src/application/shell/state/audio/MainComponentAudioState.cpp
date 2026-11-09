#include "../../../localisation/Localisation.h"
#include "../../MainComponent.h"

#include "../../ui/main_window/MicrophoneWarning.h"

bool MainComponent::hasUsableMicrophone() const
{
#if PRACTICE_TAKES_ENABLE_TEST_CONTROL
    // Development-only: lets the manual harness reach the no-input warning.
    if (testControlNoMicrophone)
    {
        return false;
    }
#endif

    return audioInputService.hasUsableInput();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioInputService)
    {
        updateMicrophoneStateControl();
        updateMicrophoneWarning();

        // The muted frame (paintMutedFrame) is drawn by this component, so a
        // change of input state has to repaint it, not only the button.
        repaint();
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
        text = l10n::text("microphone.button.disconnected");
        tooltip = l10n::text("microphone.button.disconnected.tooltip");
        colour = palette.muted;
        break;
    case AudioInputService::InputState::opening:
        // Not the same as having none, and the difference is what somebody
        // needs told: nothing to plug in would fix this, and the application
        // has not stopped working.
        text = l10n::text("microphone.button.opening");
        tooltip = l10n::text("microphone.button.opening.tooltip");
        colour = palette.warning;
        break;
    case AudioInputService::InputState::muted:
        text = l10n::text("microphone.button.muted");
        tooltip = l10n::text("microphone.button.muted.tooltip");
        colour = palette.warning;
        break;
    case AudioInputService::InputState::clipping:
        text = l10n::text("microphone.button.clipping");
        tooltip = l10n::text("microphone.button.clipping.tooltip");
        colour = juce::Colours::red;
        break;
    case AudioInputService::InputState::active:
        text = l10n::text("microphone.button.active");
        tooltip = l10n::text("microphone.button.active.tooltip");
        break;
    }

    microphoneButton.setButtonText(text);
    microphoneButton.setTooltip(tooltip);
    microphoneButton.setColour(juce::TextButton::buttonColourId, colour.withAlpha(0.75f));
    microphoneButton.setColour(juce::TextButton::buttonOnColourId, colour);
    microphoneButton.setColour(juce::TextButton::textColourOffId, palette.foreground);
    microphoneButton.setColour(juce::TextButton::textColourOnId, palette.foreground);
}

void MainComponent::paintMutedFrame(juce::Graphics& graphics) const
{
    // #114: muting was a change of text on one button, which does not register
    // out of the corner of your eye, and muting is a state you can stay in for
    // a long time without noticing. So while the microphone is muted the whole
    // workspace is framed in the palette's warning colour, the same amber the
    // button turns, with a label saying why. It is drawn over the tools rather
    // than by them, so no tool needs to know, and it goes the moment the mute
    // is lifted.
    if (audioInputService.inputState() != AudioInputService::InputState::muted)
    {
        return;
    }

    const auto palette = appPaletteFor(currentTheme);
    constexpr float frameInset = 6.0f;
    constexpr float frameThickness = 3.0f;
    constexpr float cornerRadius = 6.0f;
    const auto frame = workspaceArea().toFloat().expanded(frameInset);

    graphics.setColour(palette.warning);
    graphics.drawRoundedRectangle(frame, cornerRadius, frameThickness);

    // The label sits on the frame's top edge, at the left, where the eye
    // starts reading the workspace. Its text takes black or white, whichever
    // reads on the warning colour of the current theme.
    const juce::String label{l10n::text("microphone.muted-frame")};
    const juce::Font font{
        appearance::fontFor(*this, appearance::TextRole::small, juce::Font::bold)};
    const auto labelHeight = static_cast<float>(scaled(18));
    constexpr float labelPadding = 8.0f;
    const auto labelWidth =
        juce::GlyphArrangement::getStringWidth(font, label) + 2.0f * labelPadding;
    const juce::Rectangle<float> labelBounds{
        frame.getX() + 16.0f, frame.getY() - labelHeight / 2.0f, labelWidth, labelHeight};

    graphics.fillRoundedRectangle(labelBounds, labelHeight / 2.0f);
    graphics.setColour(palette.warning.contrasting(1.0f));
    graphics.setFont(font);
    graphics.drawText(label, labelBounds, juce::Justification::centred, false);
}

void MainComponent::updateMicrophoneWarning()
{
    if (microphoneWarning == nullptr)
    {
        return;
    }

    // The workspace is laid out below the warning while it shows (see
    // workspaceArea), so a change in whether it shows moves the workspace.
    // Relaid out only on a change: this runs on every audio state change, and
    // most of those leave the warning exactly where it was.
    const bool wasVisible = microphoneWarning->isVisible();

    if (hasUsableMicrophone())
    {
        isMicrophoneWarningDismissed = false;
        microphoneWarning->setVisible(false);
    }
    else
    {
        microphoneWarning->describe(
            audioInputService.inputState() == AudioInputService::InputState::opening);
        microphoneWarning->setVisible(!isMicrophoneWarningDismissed);
        if (microphoneWarning->isVisible())
        {
            microphoneWarning->toFront(false);
        }
    }

    if (microphoneWarning->isVisible() != wasVisible)
    {
        resized();
    }
}

void MainComponent::dismissMicrophoneWarning()
{
    isMicrophoneWarningDismissed = true;
    if (microphoneWarning != nullptr && microphoneWarning->isVisible())
    {
        microphoneWarning->setVisible(false);

        // Give the reserved space back to the workspace.
        resized();
    }
}
