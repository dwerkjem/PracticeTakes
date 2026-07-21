#pragma once

#include "MainComponent.h"

#include "../feedback/FeedbackComponent.h"
#include "../spectrogram/SpectrogramComponent.h"
#include "../tuner/TunerComponent.h"

#include <functional>
#include <utility>

class MainComponent::ToolWindow final : public juce::DocumentWindow
{
  public:
    ToolWindow(const juce::String& title, std::unique_ptr<juce::Component> content,
               juce::Point<int> preferredSize, std::function<void()> closeHandler)
        : DocumentWindow(title, juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content.release(), true);
        setResizable(true, true);
        setResizeLimits(520, 420, 2400, 1600);
        centreWithSize(preferredSize.x, preferredSize.y);
        setVisible(true);
    }

    ~ToolWindow() override
    {
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

    void applyAppearance(juce::LookAndFeel* lookAndFeel, juce::Colour background, Theme theme)
    {
        setLookAndFeel(lookAndFeel);
        setBackgroundColour(background);

        // Tool components draw their own graphs and panels, so they receive
        // the selected appearance in addition to the shared LookAndFeel.
        if (auto* tuner = dynamic_cast<TunerComponent*>(getContentComponent()))
        {
            tuner->setTheme(theme);
        }

        if (auto* spectrogram = dynamic_cast<SpectrogramComponent*>(getContentComponent()))
        {
            spectrogram->setTheme(theme);
        }

        sendLookAndFeelChange();
        repaint();
    }

  private:
    std::function<void()> onClose;
};

//==============================================================================
// Settings remains separate from the main window so it can grow without
// crowding the top-level application shell.
class MainComponent::SettingsWindow final : public juce::DocumentWindow
{
  public:
    class Content final : public juce::Component, private juce::ChangeListener
    {
      public:
        Content(AudioInputService& inputService, Theme initialTheme,
                std::function<void(Theme)> appearanceHandler,
                std::function<void(AppDefaults::Preset)> presetHandler,
                std::function<void()> saveHandler, std::function<void()> feedbackHandler,
                std::function<void()> resetToolHandler, std::function<void()> resetAudioHandler,
                std::function<void()> resetLayoutHandler, std::function<void()> resetAllHandler)
            : audioInputService(inputService),
              deviceSelector(inputService.deviceManager(), 1, 2, 0, 0, false, false, false, true),
              onAppearanceChanged(std::move(appearanceHandler)), onPreset(std::move(presetHandler))
        {
            configureHeading(appearanceHeading, "Appearance");

            appearanceLabel.setText("Theme", juce::dontSendNotification);
            addAndMakeVisible(appearanceLabel);

            appearanceBox.addItem("Light", static_cast<int>(Theme::light));
            appearanceBox.addItem("Dark", static_cast<int>(Theme::dark));
            appearanceBox.setSelectedId(static_cast<int>(initialTheme), juce::dontSendNotification);
            appearanceBox.onChange = [this]
            {
                if (onAppearanceChanged)
                {
                    onAppearanceChanged(static_cast<Theme>(appearanceBox.getSelectedId()));
                }
            };
            addAndMakeVisible(appearanceBox);

            configureHeading(audioHeading, "Audio");
            microphoneButton.onClick = [this] { audioInputService.toggleMuted(); };
            microphoneButton.setTitle("Microphone mute control");
            addAndMakeVisible(microphoneButton);
            audioInputService.addChangeListener(this);
            updateMicrophoneControl();
            addAndMakeVisible(deviceSelector);

            configureHeading(presetsHeading, "Practice preset");
            presetBox.addItem("Voice practice", static_cast<int>(AppDefaults::Preset::voice));
            presetBox.addItem("General instrument",
                              static_cast<int>(AppDefaults::Preset::generalInstrument));
            presetBox.setTextWhenNothingSelected("Choose a preset");
            presetBox.onChange = [this]
            {
                if (onPreset)
                    onPreset(static_cast<AppDefaults::Preset>(presetBox.getSelectedId()));
            };
            addAndMakeVisible(presetBox);

            configureHeading(resetHeading, "Reset");
            saveButton.setButtonText("Save settings");
            saveButton.onClick = std::move(saveHandler);
            addAndMakeVisible(saveButton);
            feedbackButton.setButtonText("Send feedback");
            feedbackButton.setTitle("Open the feedback form");
            feedbackButton.onClick = std::move(feedbackHandler);
            addAndMakeVisible(feedbackButton);
            configureResetButton(resetToolButton, "Current tool", std::move(resetToolHandler));
            configureResetButton(resetAudioButton, "Audio", std::move(resetAudioHandler));
            configureResetButton(resetLayoutButton, "Layout", std::move(resetLayoutHandler));
            configureResetButton(resetAllButton, "All settings", std::move(resetAllHandler));
        }

        ~Content() override
        {
            audioInputService.removeChangeListener(this);
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(22);

            appearanceHeading.setBounds(bounds.removeFromTop(30));
            bounds.removeFromTop(8);

            auto appearanceRow = bounds.removeFromTop(34);
            appearanceLabel.setBounds(appearanceRow.removeFromLeft(130));
            appearanceBox.setBounds(appearanceRow.removeFromLeft(220));

            bounds.removeFromTop(24);
            audioHeading.setBounds(bounds.removeFromTop(30));
            bounds.removeFromTop(8);
            auto resetArea = bounds.removeFromBottom(186);
            auto presetArea = bounds.removeFromBottom(92);
            microphoneButton.setBounds(bounds.removeFromTop(34).removeFromLeft(260));
            bounds.removeFromTop(8);
            deviceSelector.setBounds(bounds);

            presetsHeading.setBounds(presetArea.removeFromTop(30));
            presetArea.removeFromTop(6);
            presetBox.setBounds(presetArea.removeFromTop(34).removeFromLeft(260));

            resetHeading.setBounds(resetArea.removeFromTop(30));
            feedbackButton.setBounds(resetHeading.getRight() - 150, resetHeading.getY(), 150, 30);
            resetArea.removeFromTop(6);
            saveButton.setBounds(resetArea.removeFromTop(34).removeFromLeft(180));
            resetArea.removeFromTop(8);
            auto firstRow = resetArea.removeFromTop(34);
            resetToolButton.setBounds(firstRow.removeFromLeft(150));
            firstRow.removeFromLeft(8);
            resetAudioButton.setBounds(firstRow.removeFromLeft(150));
            resetArea.removeFromTop(8);
            auto secondRow = resetArea.removeFromTop(34);
            resetLayoutButton.setBounds(secondRow.removeFromLeft(150));
            secondRow.removeFromLeft(8);
            resetAllButton.setBounds(secondRow.removeFromLeft(150));
        }

        void setTheme(Theme theme)
        {
            appearanceBox.setSelectedId(static_cast<int>(theme), juce::dontSendNotification);
            sendLookAndFeelChange();
            repaint();
        }

      private:
        void changeListenerCallback(juce::ChangeBroadcaster* source) override
        {
            if (source == &audioInputService)
                updateMicrophoneControl();
        }

        void updateMicrophoneControl()
        {
            const auto isMuted = audioInputService.isMuted();
            microphoneButton.setButtonText(isMuted ? "Unmute microphone" : "Mute microphone");
            microphoneButton.setTooltip(isMuted ? "Resume audio analysis using the selected input"
                                                : "Pause microphone audio for every analysis tool");
        }

        void configureHeading(juce::Label& heading, const juce::String& text)
        {
            heading.setText(text, juce::dontSendNotification);
            heading.setFont(juce::FontOptions(18.0f, juce::Font::bold));
            addAndMakeVisible(heading);
        }

        void configureResetButton(juce::TextButton& button, const juce::String& text,
                                  std::function<void()> action)
        {
            button.setButtonText(text);
            button.onClick = [action = std::move(action), text]
            {
                juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::WarningIcon, "Confirm reset",
                    "Reset " + text.toLowerCase() + "? This cannot be undone.", "Reset", "Cancel",
                    nullptr,
                    juce::ModalCallbackFunction::create(
                        [action](int result)
                        {
                            if (result != 0 && action)
                                action();
                        }));
            };
            addAndMakeVisible(button);
        }

        juce::Label appearanceHeading;
        juce::Label appearanceLabel;
        juce::ComboBox appearanceBox;
        juce::Label audioHeading;
        AudioInputService& audioInputService;
        juce::TextButton microphoneButton;
        juce::AudioDeviceSelectorComponent deviceSelector;
        juce::Label presetsHeading;
        juce::ComboBox presetBox;
        juce::Label resetHeading;
        juce::TextButton saveButton;
        juce::TextButton feedbackButton;
        juce::TextButton resetToolButton;
        juce::TextButton resetAudioButton;
        juce::TextButton resetLayoutButton;
        juce::TextButton resetAllButton;
        std::function<void(Theme)> onAppearanceChanged;
        std::function<void(AppDefaults::Preset)> onPreset;
    };

    SettingsWindow(AudioInputService& audioInputService, Theme initialTheme,
                   std::function<void(Theme)> appearanceHandler,
                   std::function<void(AppDefaults::Preset)> presetHandler,
                   std::function<void()> saveHandler, std::function<void()> feedbackHandler,
                   std::function<void()> resetToolHandler, std::function<void()> resetAudioHandler,
                   std::function<void()> resetLayoutHandler, std::function<void()> resetAllHandler,
                   std::function<void()> closeHandler)
        : DocumentWindow("Settings", juce::Colours::darkgrey, juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Content(audioInputService, initialTheme, std::move(appearanceHandler),
                                    std::move(presetHandler), std::move(saveHandler),
                                    std::move(feedbackHandler), std::move(resetToolHandler),
                                    std::move(resetAudioHandler), std::move(resetLayoutHandler),
                                    std::move(resetAllHandler)),
                        true);
        setResizable(true, true);
        setResizeLimits(760, 620, 1600, 1200);
        centreWithSize(900, 760);
        setVisible(true);
    }

    ~SettingsWindow() override
    {
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

    void applyAppearance(juce::LookAndFeel* lookAndFeel, juce::Colour background, Theme theme)
    {
        setLookAndFeel(lookAndFeel);
        setBackgroundColour(background);

        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        {
            content->setTheme(theme);
        }

        sendLookAndFeelChange();
        repaint();
    }

  private:
    std::function<void()> onClose;
};

//==============================================================================
class MainComponent::FeedbackWindow final : public juce::DocumentWindow
{
  public:
    FeedbackWindow(juce::PropertiesFile& propertiesFile, std::function<void()> closeHandler)
        : DocumentWindow("Send feedback", juce::Colours::darkgrey,
                         juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new FeedbackComponent(propertiesFile), true);
        setResizable(true, true);
        setResizeLimits(620, 700, 1200, 1100);
        centreWithSize(760, 780);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

  private:
    std::function<void()> onClose;
};

//==============================================================================
// A custom, nonmodal warning card. It stays inside the main window so missing
// audio hardware does not interrupt the user with a native modal dialog.
