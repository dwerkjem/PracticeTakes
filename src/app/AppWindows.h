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
    class Content final : public juce::Component
    {
      private:
        class CategoryPanel : public juce::Component
        {
          public:
            void paint(juce::Graphics& graphics) override
            {
                graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
            }

          protected:
            void configureHeading(juce::Label& heading, const juce::String& text)
            {
                heading.setText(text, juce::dontSendNotification);
                heading.setFont(juce::FontOptions(22.0f, juce::Font::bold));
                addAndMakeVisible(heading);
            }

            void configureDescription(juce::Label& description, const juce::String& text)
            {
                description.setText(text, juce::dontSendNotification);
                description.setJustificationType(juce::Justification::topLeft);
                addAndMakeVisible(description);
            }
        };

        class AppearancePanel final : public CategoryPanel
        {
          public:
            AppearancePanel(Theme initialTheme, std::function<void(Theme)> appearanceHandler)
                : onAppearanceChanged(std::move(appearanceHandler))
            {
                configureHeading(heading, "Appearance");
                configureDescription(description,
                                     "Choose the application theme used by every open window.");

                themeLabel.setText("Theme", juce::dontSendNotification);
                addAndMakeVisible(themeLabel);

                themeBox.addItem("Light", static_cast<int>(Theme::light));
                themeBox.addItem("Dark", static_cast<int>(Theme::dark));
                themeBox.setSelectedId(static_cast<int>(initialTheme), juce::dontSendNotification);
                themeBox.onChange = [this]
                {
                    if (onAppearanceChanged)
                        onAppearanceChanged(static_cast<Theme>(themeBox.getSelectedId()));
                };
                addAndMakeVisible(themeBox);
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(34));
                description.setBounds(bounds.removeFromTop(42));
                bounds.removeFromTop(18);

                auto themeRow = bounds.removeFromTop(36);
                themeLabel.setBounds(themeRow.removeFromLeft(130));
                themeBox.setBounds(themeRow.removeFromLeft(240));
            }

            void setTheme(Theme theme)
            {
                themeBox.setSelectedId(static_cast<int>(theme), juce::dontSendNotification);
            }

          private:
            juce::Label heading;
            juce::Label description;
            juce::Label themeLabel;
            juce::ComboBox themeBox;
            std::function<void(Theme)> onAppearanceChanged;
        };

        class AudioPanel final : public CategoryPanel, private juce::ChangeListener
        {
          public:
            explicit AudioPanel(AudioInputService& inputService)
                : audioInputService(inputService),
                  deviceSelector(inputService.deviceManager(), 1, 2, 0, 0, false, false, false,
                                 true)
            {
                configureHeading(heading, "Audio");
                configureDescription(
                    description,
                    "Control the shared microphone signal used by every analysis tool.");

                microphoneButton.onClick = [this] { audioInputService.toggleMuted(); };
                microphoneButton.setTitle("Microphone mute control");
                addAndMakeVisible(microphoneButton);

                inputGainLabel.setText("Input volume", juce::dontSendNotification);
                addAndMakeVisible(inputGainLabel);
                inputGainSlider.setRange(0.0, 200.0, 1.0);
                inputGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
                inputGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
                inputGainSlider.setTextValueSuffix("%");
                inputGainSlider.setValue(inputService.inputGain() * 100.0,
                                         juce::dontSendNotification);
                inputGainSlider.setTooltip(
                    "Adjust the software gain used by every analysis tool; 100% preserves the "
                    "captured microphone level");
                inputGainSlider.onValueChange = [this]
                {
                    audioInputService.setInputGain(
                        static_cast<float>(inputGainSlider.getValue() / 100.0));
                };
                addAndMakeVisible(inputGainSlider);

                inputLevelLabel.setText("Input level", juce::dontSendNotification);
                addAndMakeVisible(inputLevelLabel);
                inputLevelMeter.setPercentageDisplay(false);
                inputLevelMeter.setTitle("Live microphone input level");
                addAndMakeVisible(inputLevelMeter);

                bufferingStatusLabel.setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(bufferingStatusLabel);
                deviceViewport.setViewedComponent(&deviceSelector, false);
                deviceViewport.setScrollBarsShown(true, false);
                addAndMakeVisible(deviceViewport);

                audioInputService.addChangeListener(this);
                updateMicrophoneControls();
            }

            ~AudioPanel() override
            {
                audioInputService.removeChangeListener(this);
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(34));
                description.setBounds(bounds.removeFromTop(42));
                bounds.removeFromTop(12);

                microphoneButton.setBounds(bounds.removeFromTop(36).removeFromLeft(260));
                bounds.removeFromTop(10);

                auto gainRow = bounds.removeFromTop(36);
                inputGainLabel.setBounds(gainRow.removeFromLeft(130));
                inputGainSlider.setBounds(gainRow.removeFromLeft(360));
                bounds.removeFromTop(8);

                auto levelRow = bounds.removeFromTop(24);
                inputLevelLabel.setBounds(levelRow.removeFromLeft(130));
                inputLevelMeter.setBounds(levelRow.removeFromLeft(360));
                bounds.removeFromTop(6);
                bufferingStatusLabel.setBounds(bounds.removeFromTop(26));
                bounds.removeFromTop(14);

                deviceViewport.setBounds(bounds);
                const auto selectorWidth = juce::jmax(
                    1, deviceViewport.getWidth() - deviceViewport.getScrollBarThickness());
                deviceSelector.setBounds(
                    0, 0, selectorWidth,
                    juce::jmax(deviceSelector.getHeight(), deviceViewport.getHeight()));
            }

          private:
            void changeListenerCallback(juce::ChangeBroadcaster* source) override
            {
                if (source == &audioInputService)
                    updateMicrophoneControls();
            }

            void updateMicrophoneControls()
            {
                const auto isMuted = audioInputService.isMuted();
                microphoneButton.setButtonText(isMuted ? "Unmute microphone" : "Mute microphone");
                microphoneButton.setTooltip(isMuted
                                                ? "Resume audio analysis using the selected input"
                                                : "Pause microphone audio for every analysis tool");
                inputGainSlider.setValue(audioInputService.inputGain() * 100.0,
                                         juce::dontSendNotification);
                inputLevelProgress = audioInputService.inputLevel();
                inputLevelMeter.repaint();

                const auto droppedBlocks = audioInputService.droppedAnalysisBlocks();
                bufferingStatusLabel.setText(
                    droppedBlocks == 0 ? "Analysis buffers healthy - no dropped blocks"
                                       : juce::String(droppedBlocks) +
                                             " analysis block(s) dropped; a tool is not keeping up",
                    juce::dontSendNotification);
            }

            juce::Label heading;
            juce::Label description;
            AudioInputService& audioInputService;
            juce::TextButton microphoneButton;
            juce::Label inputGainLabel;
            juce::Slider inputGainSlider;
            juce::Label inputLevelLabel;
            double inputLevelProgress = 0.0;
            juce::ProgressBar inputLevelMeter{inputLevelProgress};
            juce::Label bufferingStatusLabel;
            juce::AudioDeviceSelectorComponent deviceSelector;
            juce::Viewport deviceViewport;
        };

        class PracticePanel final : public CategoryPanel
        {
          public:
            explicit PracticePanel(std::function<void(AppDefaults::Preset)> presetHandler)
                : onPreset(std::move(presetHandler))
            {
                configureHeading(heading, "Practice");
                configureDescription(
                    description,
                    "Apply a coordinated set of tuner controls for the material you are "
                    "practicing.");

                presetLabel.setText("Practice preset", juce::dontSendNotification);
                addAndMakeVisible(presetLabel);
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
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(34));
                description.setBounds(bounds.removeFromTop(48));
                bounds.removeFromTop(18);

                auto presetRow = bounds.removeFromTop(36);
                presetLabel.setBounds(presetRow.removeFromLeft(150));
                presetBox.setBounds(presetRow.removeFromLeft(280));
            }

          private:
            juce::Label heading;
            juce::Label description;
            juce::Label presetLabel;
            juce::ComboBox presetBox;
            std::function<void(AppDefaults::Preset)> onPreset;
        };

        class ResetPanel final : public CategoryPanel
        {
          public:
            ResetPanel(std::function<void()> feedbackHandler,
                       std::function<void()> resetToolHandler,
                       std::function<void()> resetAudioHandler,
                       std::function<void()> resetLayoutHandler,
                       std::function<void()> resetAllHandler)
            {
                configureHeading(heading, "Reset & support");
                configureDescription(
                    description,
                    "Restore one category without affecting the others, or send feedback.");

                actionsHeading.setText("Support", juce::dontSendNotification);
                actionsHeading.setFont(juce::FontOptions(17.0f, juce::Font::bold));
                addAndMakeVisible(actionsHeading);
                feedbackButton.setButtonText("Send feedback");
                feedbackButton.setTitle("Open the feedback form");
                feedbackButton.onClick = std::move(feedbackHandler);
                addAndMakeVisible(feedbackButton);

                resetHeading.setText("Reset category", juce::dontSendNotification);
                resetHeading.setFont(juce::FontOptions(17.0f, juce::Font::bold));
                addAndMakeVisible(resetHeading);
                configureResetButton(resetToolButton, "Current tool", std::move(resetToolHandler));
                configureResetButton(resetAudioButton, "Audio", std::move(resetAudioHandler));
                configureResetButton(resetLayoutButton, "Layout", std::move(resetLayoutHandler));
                configureResetButton(resetAllButton, "All settings", std::move(resetAllHandler));
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(34));
                description.setBounds(bounds.removeFromTop(48));
                bounds.removeFromTop(18);

                actionsHeading.setBounds(bounds.removeFromTop(28));
                bounds.removeFromTop(8);
                feedbackButton.setBounds(bounds.removeFromTop(36).removeFromLeft(190));

                bounds.removeFromTop(28);
                resetHeading.setBounds(bounds.removeFromTop(28));
                bounds.removeFromTop(8);
                auto firstRow = bounds.removeFromTop(36);
                resetToolButton.setBounds(firstRow.removeFromLeft(190));
                firstRow.removeFromLeft(10);
                resetAudioButton.setBounds(firstRow.removeFromLeft(190));
                bounds.removeFromTop(10);
                auto secondRow = bounds.removeFromTop(36);
                resetLayoutButton.setBounds(secondRow.removeFromLeft(190));
                secondRow.removeFromLeft(10);
                resetAllButton.setBounds(secondRow.removeFromLeft(190));
            }

          private:
            void configureResetButton(juce::TextButton& button, const juce::String& text,
                                      std::function<void()> action)
            {
                button.setButtonText(text);
                button.onClick = [action = std::move(action), text]
                {
                    juce::AlertWindow::showOkCancelBox(
                        juce::MessageBoxIconType::WarningIcon, "Confirm reset",
                        "Reset " + text.toLowerCase() + "? This cannot be undone.", "Reset",
                        "Cancel", nullptr,
                        juce::ModalCallbackFunction::create(
                            [action](int result)
                            {
                                if (result != 0 && action)
                                    action();
                            }));
                };
                addAndMakeVisible(button);
            }

            juce::Label heading;
            juce::Label description;
            juce::Label actionsHeading;
            juce::TextButton feedbackButton;
            juce::Label resetHeading;
            juce::TextButton resetToolButton;
            juce::TextButton resetAudioButton;
            juce::TextButton resetLayoutButton;
            juce::TextButton resetAllButton;
        };

      public:
        Content(AudioInputService& inputService, Theme initialTheme,
                std::function<void(Theme)> appearanceHandler,
                std::function<void(AppDefaults::Preset)> presetHandler,
                std::function<void()> saveHandler, std::function<void()> feedbackHandler,
                std::function<void()> resetToolHandler, std::function<void()> resetAudioHandler,
                std::function<void()> resetLayoutHandler, std::function<void()> resetAllHandler)
            : appearancePanel(initialTheme, std::move(appearanceHandler)), audioPanel(inputService),
              practicePanel(std::move(presetHandler)),
              resetPanel(std::move(feedbackHandler), std::move(resetToolHandler),
                         std::move(resetAudioHandler), std::move(resetLayoutHandler),
                         std::move(resetAllHandler))
        {
            categoryTabs.setTabBarDepth(42);
            categoryTabs.addTab("Appearance", juce::Colours::transparentBlack, &appearancePanel,
                                false);
            categoryTabs.addTab("Audio", juce::Colours::transparentBlack, &audioPanel, false);
            categoryTabs.addTab("Practice", juce::Colours::transparentBlack, &practicePanel, false);
            categoryTabs.addTab("Reset & support", juce::Colours::transparentBlack, &resetPanel,
                                false);
            addAndMakeVisible(categoryTabs);

            saveButton.setButtonText("Save settings");
            saveButton.setTitle("Save the current settings");
            saveButton.onClick = std::move(saveHandler);
            addAndMakeVisible(saveButton);
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(12);
            auto footer = bounds.removeFromBottom(48);
            categoryTabs.setBounds(bounds);
            footer.removeFromTop(10);
            saveButton.setBounds(footer.removeFromRight(180));
        }

        void setTheme(Theme theme)
        {
            appearancePanel.setTheme(theme);
            categoryTabs.sendLookAndFeelChange();
            repaint();
        }

      private:
        AppearancePanel appearancePanel;
        AudioPanel audioPanel;
        PracticePanel practicePanel;
        ResetPanel resetPanel;
        juce::TextButton saveButton;
        juce::TabbedComponent categoryTabs{juce::TabbedButtonBar::TabsAtTop};
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
    FeedbackWindow(juce::PropertiesFile& propertiesFile, const juce::String& initialContext,
                   std::function<void()> closeHandler)
        : DocumentWindow("Send feedback", juce::Colours::darkgrey,
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
