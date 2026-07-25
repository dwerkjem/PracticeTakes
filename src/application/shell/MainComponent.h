#pragma once

#include <JuceHeader.h>

#include "../../services/audio/AudioInputService.h"
#include "../configuration/AppDefaults.h"
#include "../configuration/SettingsPersistence.h"
#include "../theme/Theme.h"
#include "ui/workspace/WorkspaceLayoutState.h"
#include "ui/workspace/WorkspaceToolState.h"

#include <functional>
#include <memory>
#include <optional>

class MainTitleBar;

// MainComponent is the application's central coordinator. It owns the shared
// audio device, the top-level controls, and the docked/floating tool workspace.
class MainComponent final
    : public juce::Component,
      public juce::DragAndDropContainer,
      public juce::DragAndDropTarget,
      private juce::ChangeListener
{
  public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    [[nodiscard]] std::unique_ptr<MainTitleBar> createTitleBar(
        const juce::String& title,
        std::function<void()> minimiseHandler,
        std::function<void()> fullscreenHandler,
        std::function<void()> closeHandler);
    [[nodiscard]] AppSettings::FullscreenMode fullscreenMode() const noexcept;

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
    void beginToolDrag(ToolType tool, juce::Component& source);
    void setWorkspaceLayout(WorkspaceLayoutState::Layout layout);
    void rebuildWorkspaceContainer();
    void layoutWorkspace(juce::Rectangle<int> bounds);
    [[nodiscard]] WorkspaceLayoutState::Tool layoutTool(ToolType tool) const;
    [[nodiscard]] ToolType toolType(WorkspaceLayoutState::Tool tool) const;
    [[nodiscard]] std::optional<ToolType>
    draggedTool(const juce::DragAndDropTarget::SourceDetails& details) const;
    [[nodiscard]] WorkspaceLayoutState::DropZone dropZoneAt(juce::Point<int> position) const;
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;

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
    std::unique_ptr<juce::TabbedComponent> workspaceTabs;
    std::unique_ptr<juce::StretchableLayoutResizerBar> workspaceDivider;
    std::unique_ptr<juce::Component> tunerComponent;
    std::unique_ptr<juce::Component> spectrogramComponent;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<FeedbackWindow> feedbackWindow;
    std::unique_ptr<MicrophoneWarning> microphoneWarning;

    Theme currentTheme = Theme::light;
    ToolType currentTool = ToolType::tuner;
    WorkspaceToolState tunerState;
    WorkspaceToolState spectrogramState;
    WorkspaceLayoutState workspaceLayoutState;
    juce::StretchableLayoutManager workspaceLayoutManager;
    WorkspaceLayoutState::DropZone activeDropZone = WorkspaceLayoutState::DropZone::none;
    AppDefaults::TunerSettings savedTunerSettings = AppDefaults::tunerDefaults();
    AppSettings::FullscreenMode selectedFullscreenMode = AppSettings::FullscreenMode::normal;
    juce::Rectangle<int> savedTunerBounds;
    juce::Rectangle<int> savedSpectrogramBounds;
    juce::Rectangle<int> savedSettingsBounds;
    bool isMicrophoneWarningDismissed = false;
    bool automaticSettingsSaveEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
