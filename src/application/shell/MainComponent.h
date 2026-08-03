#pragma once

#include <JuceHeader.h>

#include "../../platform/audio/AudioInputService.h"
#include "../configuration/AppDefaults.h"
#include "../configuration/SettingsPersistence.h"
#include "../theme/Theme.h"
#include "../tools/BuiltInTools.h"
#include "../tools/ToolInstanceId.h"
#include "ui/workspace/model/NamedWorkspaceService.h"
#include "ui/workspace/model/WorkspaceDocuments.h"
#include "ui/workspace/model/WorkspaceLayoutState.h"
#include "ui/workspace/model/WorkspaceSessionLifecycle.h"
#include "ui/workspace/model/WorkspaceToolState.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#if PRACTICE_TAKES_ENABLE_TEST_CONTROL
#include <string>

#include "../testcontrol/ApprovedWindowStates.h"
#endif

class MainTitleBar;
struct SettingsImportResult;
struct SettingsTransferModel;

// MainComponent is the application's central coordinator. It owns the shared
// audio device, the top-level controls, and the docked/floating tool workspace.
class MainComponent final
    : public juce::Component,
      public juce::DragAndDropContainer,
      public juce::DragAndDropTarget,
      private juce::ChangeListener
{
  public:
    explicit MainComponent(bool muteMicrophoneForSession = false);
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
    void restoreLoadedWorkspace();
    void initialiseAudioAfterLaunch();
    void runUiValidationScenario(const juce::String& scenario);
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
    void automatePerformanceLab(std::function<void(bool)> completion);
#endif

#if PRACTICE_TAKES_ENABLE_TEST_CONTROL
    // Development-only control surface for the manual GUI harness. Absent from
    // release builds; see src/application/testcontrol/ for the vocabulary and
    // why nothing here synthesises input.
    //
    // Everything below runs on the message thread. The channel marshals to it
    // before calling, so these need no locking of their own.
    [[nodiscard]] bool applyTestControlState(const testcontrol::ApprovedWindowState& state);
    [[nodiscard]] bool clickTestControlTarget(const std::string& id);
    [[nodiscard]] bool applyTestControlGeometry(const std::string& geometry);
    [[nodiscard]] bool applyTestControlTheme(const std::string& theme);
    [[nodiscard]] std::string currentTestControlStateId() const;
#endif

  private:
    class ToolWindow;
    class DockedToolPanel;

    // What a drag-and-drop point in the workspace resolves to: a zone, plus
    // (if the point landed on a specific docked tool's pane rather than open
    // space) which tool's pane it's relative to. A tiling zone here splits
    // *that pane* -- this is what lets tools keep subdividing the workspace
    // fractally instead of only ever affecting one fixed top-level layout.
    struct DropTarget
    {
        WorkspaceLayoutState::DropZone zone = WorkspaceLayoutState::DropZone::none;
        std::optional<ToolInstanceId> pane;
    };

    // One tool instance and everything the shell holds on its behalf.
    //
    // An entry outlives its component: closing a tool releases the component
    // but keeps the entry, so a reopened tool comes back where it was, with the
    // settings it had. `component` being null is how "closed" is represented
    // alongside `state`.
    //
    // Presentation is deliberately not ownership -- `window` and `dock` hold
    // the chrome, never the tool itself, which is what lets a tool move between
    // docked and floating without its analysis restarting.
    struct LiveTool
    {
        ToolInstanceId id;

        // This instance's opaque handle in workspaceLayoutState's tiling tree.
        // Assigned once from a counter and never reused, so the tree can hold
        // a handle across a rebuild without it silently coming to mean a
        // different instance.
        WorkspaceLayoutState::Tool handle{};

        std::unique_ptr<ToolComponent> component;
        std::unique_ptr<ToolWindow> window;
        std::unique_ptr<DockedToolPanel> dock;
        WorkspaceToolState state;

        // Where this instance's floating window last was, kept across closing
        // so reopening restores the position rather than recentring.
        juce::Rectangle<int> savedBounds;

        // The settings this instance last reported, kept for the same reason.
        // Opaque here: only the tool that produced it can read it.
        std::optional<ToolSettingsPayload> savedSettings;
    };
    class SettingsWindow;
    class FeedbackWindow;
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
    class PerformanceLabWindow;
#endif
    class MicrophoneWarning;

    // Initial setup ---------------------------------------------------------
    void configureTopButtons();
    void createMicrophoneWarning();

    // Tool and settings windows --------------------------------------------
    void showToolsMenu();
    void applyWorkspace(const std::string& workspaceId);
    void showSaveWorkspaceDialog();
    void showRenameWorkspaceDialog();
    void confirmOverwriteWorkspace();
    void confirmDeleteWorkspace();
    void confirmResetWorkspace();
    void saveNamedWorkspace(const juce::String& name, bool confirmed);
    void renameNamedWorkspace(const juce::String& name, bool confirmed);
    void showWorkspaceResult(
        const juce::String& action,
        const NamedWorkspaceService::Result& result,
        const juce::String& requestedName = {});
    [[nodiscard]] const NamedWorkspace* activeNamedWorkspace() const;
    void showSettingsMenu();
    void openTool(
        const ToolInstanceId& instance,
        WorkspaceToolState::Presentation presentation = WorkspaceToolState::Presentation::docked);
    // Opens a tool by id, honouring its instance policy: a single-instance
    // tool that is already live is focused rather than duplicated.
    void openToolById(
        std::string_view toolId,
        WorkspaceToolState::Presentation presentation = WorkspaceToolState::Presentation::docked);
    void presentTool(const ToolInstanceId& instance, WorkspaceToolState::Presentation presentation);
    void constructToolPresentation(
        const ToolInstanceId& instance,
        WorkspaceToolState::Presentation presentation);
    void focusTool(const ToolInstanceId& instance);
    void recordToolFocus(const ToolInstanceId& instance);
    void closeTool(const ToolInstanceId& instance);
    void showSettings();
    void importSettings();
    void confirmSettingsImport(SettingsTransferModel model);
    void executeSettingsImport(const SettingsTransferModel& model);
    void showSettingsImportResult(const SettingsImportResult& result);
    void exportSettings();
    [[nodiscard]] bool applyTransferredSettings(const SettingsTransferModel& model);
    void closeSettings();
    void showHelpMenu();
    void showFeedback(const juce::String& context = {});
    void recordSuccessfulToolUse();
    void maybeOfferFeedbackInvitation();
    void setFeedbackInvitationsDisabled(bool disabled);
    [[nodiscard]] bool feedbackInvitationsDisabled();
    void closeFeedback();
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
    void showPerformanceLab();
    void closePerformanceLab();
#endif
    void resetCurrentTool();
    void resetAudio();
    void resetLayout();
    void resetAll();
    void applyPreset(AppDefaults::Preset preset);
    void saveSettings(bool explicitSave = false);
    void loadSettings();
    [[nodiscard]] AppSettings::State captureSettingsState();
    // Adapter between the id-keyed runtime and the fixed per-tool fields the
    // stored .ptsettings schema still uses. See MainComponentSettings.cpp.
    void applyLegacyToolState(const AppSettings::State& state);
    void captureLegacyToolState(AppSettings::State& state);
    void captureActiveWorkspace();
    [[nodiscard]] bool applyWorkspaceSnapshot(const WorkspaceSnapshot& snapshot);

    // Ensures an entry exists for this instance and returns it. Null only when
    // the instance names a tool that is not registered.
    [[nodiscard]] LiveTool* ensureLiveTool(const ToolInstanceId& instance);
    [[nodiscard]] LiveTool* findLiveTool(const ToolInstanceId& instance);
    [[nodiscard]] const LiveTool* findLiveTool(const ToolInstanceId& instance) const;
    [[nodiscard]] LiveTool* findLiveTool(WorkspaceLayoutState::Tool handle);
    [[nodiscard]] const ToolDefinition* definitionFor(const ToolInstanceId& instance) const;
    // The instance id a single-instance tool always uses, and the id the next
    // instance of a multi-instance tool would take.
    [[nodiscard]] ToolInstanceId instanceIdToOpen(std::string_view toolId) const;
    [[nodiscard]] juce::String toolName(const ToolInstanceId& instance) const;
    [[nodiscard]] bool toolIsOpen(const ToolInstanceId& instance) const;
    void detachToolPresentation(const ToolInstanceId& instance);
    void applyAppearanceToTool(const ToolInstanceId& instance);
    void beginToolDrag(
        const ToolInstanceId& instance,
        juce::Component& source,
        const juce::ScaledImage& dragImage = juce::ScaledImage());
    void setWorkspaceLayout(WorkspaceLayoutState::Layout layout);
    void detachWorkspaceContainer();
    void rebuildWorkspaceContainer();
    void layoutWorkspace(juce::Rectangle<int> bounds);
    [[nodiscard]] juce::Component* buildWorkspaceNode(const WorkspaceLayoutState::Node* node);
    [[nodiscard]] std::vector<ToolInstanceId> dockedTools();
    [[nodiscard]] std::optional<ToolInstanceId> dockedToolAt(juce::Point<int> position);
    [[nodiscard]] DropTarget resolveDropTarget(juce::Point<int> position);
    [[nodiscard]] std::optional<ToolInstanceId>
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

    // Every tool instance the session knows about, live or closed.
    //
    // Declared after audioInputService and appLookAndFeel on purpose: members
    // are destroyed in reverse declaration order, so this dies first and every
    // tool is torn down before the services it borrowed. That ordering is the
    // whole of how "a tool cannot outlive its shared services" is enforced --
    // there is no runtime check, so do not move this declaration above them.
    std::vector<LiveTool> liveTools;

    // Source of handle values for LiveTool::handle. Monotonic and never reset,
    // so a handle is never reused by a later instance.
    std::uint64_t nextToolHandle = 0;

    // Owns every split-pane/tab-strip container built for the current shape
    // of workspaceLayoutState's tree; torn down and rebuilt from scratch on
    // every change. The per-tool docks above are NOT stored here -- they're
    // independently owned and merely reparented as this tree is rebuilt.
    std::vector<std::unique_ptr<juce::Component>> workspaceContainers;
    // Non-owning: whichever single component (a dock, or an entry in
    // workspaceContainers) is currently the top of the workspace tree.
    juce::Component* workspaceRoot = nullptr;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<juce::FileChooser> settingsTransferChooser;
    std::unique_ptr<FeedbackWindow> feedbackWindow;
#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
    std::unique_ptr<PerformanceLabWindow> performanceLabWindow;
#endif
    std::unique_ptr<MicrophoneWarning> microphoneWarning;

    Theme currentTheme = Theme::light;
    ToolInstanceId currentTool;
    WorkspaceLayoutState workspaceLayoutState;
    WorkspaceCatalog workspaceCatalog;
    WorkspaceSnapshot activeWorkspaceSnapshot;
    DropTarget activeDropTarget;
    AppSettings::FullscreenMode selectedFullscreenMode = AppSettings::FullscreenMode::normal;
    juce::Rectangle<int> savedSettingsBounds;
    bool isMicrophoneWarningDismissed = false;

#if PRACTICE_TAKES_ENABLE_TEST_CONTROL
    // The approved state last applied, or empty when the application is not in
    // one -- at startup, and after a click changed something.
    std::string testControlStateId;

    // Makes hasUsableMicrophone report false so the no-input warning can be
    // seen on a machine whose microphone works, which is every machine a
    // release is cut from.
    //
    // Deliberately overrides at this level rather than in AudioInputService:
    // hasUsableInput there also drives device recovery, so forcing it false
    // would have the service continuously trying to reopen the device.
    bool testControlNoMicrophone = false;
#endif
    bool automaticSettingsSaveEnabled = true;
    bool feedbackInvitationsDisabledState = false;
    bool sessionMicrophoneMuteEnabled = false;
    bool persistedMicrophoneMuted = false;
    std::unique_ptr<juce::XmlElement> pendingAudioDeviceState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
