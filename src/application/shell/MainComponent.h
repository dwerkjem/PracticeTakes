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
#include <vector>

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
        spectrogram,
        harmonics
    };

    // The single place a new tool needs to be registered for the drag/drop
    // and tab/tile system below to know it exists. Everything else in that
    // system (rebuildWorkspaceContainer, layoutWorkspace, itemDropped, ...)
    // loops over this list generically instead of naming tools directly.
    static constexpr ToolType allToolTypes[] = {
        ToolType::tuner, ToolType::spectrogram, ToolType::harmonics};

    // What a drag-and-drop point in the workspace resolves to: a zone, plus
    // (if the point landed on a specific docked tool's pane rather than open
    // space) which tool's pane it's relative to. A tiling zone here splits
    // *that pane* -- this is what lets tools keep subdividing the workspace
    // fractally instead of only ever affecting one fixed top-level layout.
    struct DropTarget
    {
        WorkspaceLayoutState::DropZone zone = WorkspaceLayoutState::DropZone::none;
        std::optional<ToolType> pane;
    };

    class ToolWindow;
    class DockedToolPanel;
    class WorkspaceSplitPane;
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
    void beginToolDrag(
        ToolType tool,
        juce::Component& source,
        juce::ScaledImage dragImage = juce::ScaledImage());
    void setWorkspaceLayout(WorkspaceLayoutState::Layout layout);
    void rebuildWorkspaceContainer();
    void layoutWorkspace(juce::Rectangle<int> bounds);
    [[nodiscard]] juce::Component* buildWorkspaceNode(const WorkspaceLayoutState::Node* node);
    [[nodiscard]] std::vector<ToolType> dockedTools();
    [[nodiscard]] std::optional<ToolType> dockedToolAt(juce::Point<int> position);
    [[nodiscard]] DropTarget resolveDropTarget(juce::Point<int> position);
    [[nodiscard]] std::optional<ToolType>
    draggedTool(const juce::DragAndDropTarget::SourceDetails& details) const;
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
    std::unique_ptr<ToolWindow> harmonicWindow;
    std::unique_ptr<DockedToolPanel> tunerDock;
    std::unique_ptr<DockedToolPanel> spectrogramDock;
    std::unique_ptr<DockedToolPanel> harmonicDock;
    // Owns every split-pane/tab-strip container built for the current shape
    // of workspaceLayoutState's tree; torn down and rebuilt from scratch on
    // every change. The per-tool docks above are NOT stored here -- they're
    // independently owned and merely reparented as this tree is rebuilt.
    std::vector<std::unique_ptr<juce::Component>> workspaceContainers;
    // Non-owning: whichever single component (a dock, or an entry in
    // workspaceContainers) is currently the top of the workspace tree.
    juce::Component* workspaceRoot = nullptr;
    std::unique_ptr<juce::Component> tunerComponent;
    std::unique_ptr<juce::Component> spectrogramComponent;
    std::unique_ptr<juce::Component> harmonicComponent;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<FeedbackWindow> feedbackWindow;
    std::unique_ptr<MicrophoneWarning> microphoneWarning;

    Theme currentTheme = Theme::light;
    ToolType currentTool = ToolType::tuner;
    WorkspaceToolState tunerState;
    WorkspaceToolState spectrogramState;
    WorkspaceToolState harmonicState;
    WorkspaceLayoutState workspaceLayoutState;
    DropTarget activeDropTarget;
    AppDefaults::TunerSettings savedTunerSettings = AppDefaults::tunerDefaults();
    AppSettings::FullscreenMode selectedFullscreenMode = AppSettings::FullscreenMode::normal;
    juce::Rectangle<int> savedTunerBounds;
    juce::Rectangle<int> savedSpectrogramBounds;
    juce::Rectangle<int> savedHarmonicBounds;
    juce::Rectangle<int> savedSettingsBounds;
    bool isMicrophoneWarningDismissed = false;
    bool automaticSettingsSaveEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
