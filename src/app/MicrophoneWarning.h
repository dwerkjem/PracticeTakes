#pragma once

#include "MainComponent.h"

#include <functional>
#include <utility>

class MainComponent::MicrophoneWarning final : public juce::Component
{
  public:
    MicrophoneWarning(std::function<void()> settingsHandler, std::function<void()> dismissHandler)
        : onOpenSettings(std::move(settingsHandler)), onDismiss(std::move(dismissHandler))
    {
        setInterceptsMouseClicks(true, true);

        title.setText("No microphone detected", juce::dontSendNotification);
        title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        addAndMakeVisible(title);

        message.setText(
            "Choose an input device in Settings to use the tuner and "
            "spectrogram.",
            juce::dontSendNotification);
        message.setFont(juce::FontOptions(13.0f));
        message.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(message);

        settingsButton.setButtonText("Open Settings");
        settingsButton.onClick = [this]
        {
            if (onOpenSettings)
            {
                onOpenSettings();
            }
        };
        addAndMakeVisible(settingsButton);

        dismissButton.setButtonText("Dismiss");
        dismissButton.onClick = [this]
        {
            if (onDismiss)
            {
                onDismiss();
            }
        };
        addAndMakeVisible(dismissButton);
    }

    void setTheme(Theme theme)
    {
        currentTheme = theme;
        const auto palette = appPaletteFor(currentTheme);

        title.setColour(juce::Label::textColourId, palette.foreground);
        message.setColour(juce::Label::textColourId, palette.muted);
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto palette = appPaletteFor(currentTheme);
        const auto card = getLocalBounds().toFloat().reduced(6.0f);

        // A subtle shadow and rounded panel make the warning visible without
        // blocking or visually dominating the application.
        graphics.setColour(
            juce::Colours::black.withAlpha(isDarkTheme(currentTheme) ? 0.35f : 0.14f));
        graphics.fillRoundedRectangle(card.translated(0.0f, 3.0f), 13.0f);

        graphics.setColour(palette.panel);
        graphics.fillRoundedRectangle(card, 13.0f);

        graphics.setColour(palette.warning.withAlpha(0.95f));
        graphics.fillRoundedRectangle(card.withWidth(5.0f), 3.0f);

        graphics.setColour(palette.outline.withAlpha(0.85f));
        graphics.drawRoundedRectangle(card, 13.0f, 1.0f);

        graphics.setColour(palette.warning);
        graphics.fillEllipse(20.0f, 23.0f, 24.0f, 24.0f);

        graphics.setColour(
            isDarkTheme(currentTheme) ? juce::Colour::fromRGB(35, 29, 18) : juce::Colours::white);
        graphics.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        graphics.drawText("!", 20, 22, 24, 25, juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(18, 14);
        bounds.removeFromLeft(42); // Reserve room for the warning icon.

        auto buttons = bounds.removeFromBottom(30);
        dismissButton.setBounds(buttons.removeFromRight(88));
        buttons.removeFromRight(8);
        settingsButton.setBounds(buttons.removeFromRight(122));

        title.setBounds(bounds.removeFromTop(24));
        message.setBounds(bounds);
    }

  private:
    juce::Label title;
    juce::Label message;
    juce::TextButton settingsButton;
    juce::TextButton dismissButton;
    std::function<void()> onOpenSettings;
    std::function<void()> onDismiss;
    Theme currentTheme = Theme::light;
};
