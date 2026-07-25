#pragma once

#include <JuceHeader.h>

#include "../audio/AudioInputService.h"
#include "../feedback/FeedbackComponent.h"
#include "AppDefaults.h"
#include "SettingsPersistence.h"
#include "Theme.h"
#include "WorkspaceToolState.h"

#include <functional>
#include <memory>

class MainTitleBar;

// MainComponent is the application's central coordinator. It owns the shared
// audio device, the top-level controls, and the docked/floating tool workspace.
class MainComponent final : public juce::Component, private juce::ChangeListener
{
  public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    [[nodiscard]] std::unique_ptr<MainTitleBar> createTitleBar(
        const juce::String& title,
        std::function<void()> minimiseHandler,
        std::function<void()> fullscreenHandler,
        std::function<void()> closeHandler);

  private:
    enum class ToolType
    {
        tuner,
        spectrogram
    };

    class ToolWindow;
    class DockedToolPanel;
    class SettingsWindow;
    class FeedbackWindow;
    class MicrophoneWarning;

    // Initial setup ---------------------------------------------------------
    void configureTopButtons();
    void createMicrophoneWarning();

    // Tool and settings windows --------------------------------------------
    void showToolsMenu();
    void showSettingsMenu();
    void openTool(
        ToolType tool,
        WorkspaceToolState::Presentation presentation = WorkspaceToolState::Presentation::docked);
    void presentTool(ToolType tool, WorkspaceToolState::Presentation presentation);
    void focusTool(ToolType tool);
    void closeTool(ToolType tool);
    void showSettings();
    void closeSettings();
    void showHelpMenu();
    void showFeedback(const juce::String& context = {});
    void recordSuccessfulToolUse();
    void maybeOfferFeedbackInvitation();
    void setFeedbackInvitationsDisabled(bool disabled);
    [[nodiscard]] bool feedbackInvitationsDisabled();
    void closeFeedback();
    void resetCurrentTool();
    void resetAudio();
    void resetLayout();
    void resetAll();
    void applyPreset(AppDefaults::Preset preset);
    void saveSettings(bool explicitSave = false);
    void loadSettings();
    [[nodiscard]] AppSettings::State captureSettingsState();

    [[nodiscard]] std::unique_ptr<juce::Component> createToolComponent(ToolType tool);
    [[nodiscard]] juce::String toolName(ToolType tool) const;
    [[nodiscard]] juce::Point<int> preferredToolWindowSize(ToolType tool) const;
    [[nodiscard]] bool toolIsOpen(ToolType tool) const;
    [[nodiscard]] WorkspaceToolState& stateFor(ToolType tool);
    [[nodiscard]] const WorkspaceToolState& stateFor(ToolType tool) const;
    [[nodiscard]] std::unique_ptr<juce::Component>& componentFor(ToolType tool);
    [[nodiscard]] std::unique_ptr<ToolWindow>& windowFor(ToolType tool);
    [[nodiscard]] std::unique_ptr<DockedToolPanel>& dockFor(ToolType tool);
    void detachToolPresentation(ToolType tool);
    void applyAppearanceToTool(ToolType tool);

    // Appearance ------------------------------------------------------------
    void setTheme(Theme theme);
    void applyAppearance();
    void configureLookAndFeelColours();
    void applyAppearanceToTopButtons();
    void applyAppearanceToOpenWindows();

    // Microphone state ------------------------------------------------------
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    [[nodiscard]] bool hasUsableMicrophone() const;
    void updateMicrophoneStateControl();
    void updateMicrophoneWarning();
    void dismissMicrophoneWarning();

    // One audio device manager is shared by every open analysis tool.
    AudioInputService audioInputService;
    juce::ApplicationProperties applicationProperties;
    juce::LookAndFeel_V4 appLookAndFeel;

    juce::TextButton fileButton{"File"};
    juce::TextButton settingsButton{"Settings"};
    juce::TextButton toolsButton{"Tools"};
    juce::TextButton helpButton{"Help"};
    juce::TextButton microphoneButton;

    std::unique_ptr<ToolWindow> tunerWindow;
    std::unique_ptr<ToolWindow> spectrogramWindow;
    std::unique_ptr<DockedToolPanel> tunerDock;
    std::unique_ptr<DockedToolPanel> spectrogramDock;
    std::unique_ptr<juce::Component> tunerComponent;
    std::unique_ptr<juce::Component> spectrogramComponent;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<FeedbackWindow> feedbackWindow;
    std::unique_ptr<MicrophoneWarning> microphoneWarning;

    Theme currentTheme = Theme::light;
    ToolType currentTool = ToolType::tuner;
    WorkspaceToolState tunerState;
    WorkspaceToolState spectrogramState;
    AppDefaults::TunerSettings savedTunerSettings = AppDefaults::tunerDefaults();
    juce::Rectangle<int> savedTunerBounds;
    juce::Rectangle<int> savedSpectrogramBounds;
    juce::Rectangle<int> savedSettingsBounds;
    bool isMicrophoneWarningDismissed = false;
    bool automaticSettingsSaveEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
