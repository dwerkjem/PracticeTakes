#pragma once

#include <JuceHeader.h>

#include "../../platform/audio/AudioInputService.h"
#include "../../platform/audio/SharedPitchAnalysis.h"
#include "../../platform/midi/MidiInputService.h"
#include "../../platform/pitch/CurrentTarget.h"
#include "../../platform/pitch/ReferencePitch.h"
#include "../../platform/take/TakeRecorder.h"
#include "../../platform/transport/PracticeClock.h"
#include "../configuration/AppDefaults.h"
#include "../configuration/SettingsPersistence.h"
#include "../localisation/Localisation.h"
#include "../theme/AppLookAndFeel.h"
#include "../theme/Theme.h"
#include "../tools/AudioStreamUse.h"
#include "../tools/BuiltInTools.h"
#include "../tools/ToolInstanceAllocator.h"
#include "../tools/ToolInstanceId.h"
#include "state/following/TargetClockFollower.h"
#include "ui/main_window/TransportStrip.h"
#include "ui/score/ScoreImportJob.h"
#include "ui/score/ScoreImportState.h"
#include "ui/workspace/model/NamedWorkspaceService.h"
#include "ui/workspace/model/WorkspaceDocuments.h"
#include "ui/workspace/model/WorkspaceLayoutState.h"
#include "ui/workspace/model/WorkspaceSessionLifecycle.h"
#include "ui/workspace/model/WorkspaceToolState.h"
#include "ui/workspace/shell/WorkspaceActionText.h"

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
    // Where the settings file is, for anything that must read it before the
    // window exists -- the interface language, at startup.
    [[nodiscard]] static juce::PropertiesFile::Options settingsStorageOptions();

    // `declineAudioInput` is passed straight to AudioInputService: open no
    // input device at all, leaving the generated tone as the only producer.
    // See that constructor for why a capture instance wants it.
    explicit MainComponent(bool muteMicrophoneForSession = false, bool declineAudioInput = false);
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    // The application's control strip -- menus, microphone, fullscreen -- for
    // the window to place above this component. The window's title, minimise,
    // maximise and close come from the operating system's title bar.
    [[nodiscard]] std::unique_ptr<MainTitleBar>
    createTitleBar(std::function<void()> fullscreenHandler);
    [[nodiscard]] AppSettings::FullscreenMode fullscreenMode() const noexcept;
    void restoreLoadedWorkspace();
    void initialiseAudioAfterLaunch();
    void runUiValidationScenario(const juce::String& scenario);

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

    // This process is being driven by the harness rather than used by a person.
    // Called before the workspace restore lands, because that is early enough
    // for a tool to close and the feedback invitation to be raised over the
    // first surface -- see `testControlDriving`.
    void beginTestControlSession();
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

        // The dragged tab is over its own pane or its own tab strip: dropped,
        // it leaves its group and tiles beside the tab to its left (#355).
        bool tilesOut = false;
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

        // The user's name for this instance, already sanitised. Empty means it
        // shows its default name ("Tuner", "Tuner 2").
        std::string label;
    };
    class SettingsWindow;
    class FeedbackWindow;
    class ScoreImportWindow;
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
        const WorkspaceActionText& action,
        const NamedWorkspaceService::Result& result,
        const juce::String& requestedName = {});
    [[nodiscard]] const NamedWorkspace* activeNamedWorkspace() const;
    void showSettingsMenu();
    void openTool(
        const ToolInstanceId& instance,
        WorkspaceToolState::Presentation presentation = WorkspaceToolState::Presentation::docked);
    // Opens a tool by id, or focuses it: the first live instance when there is
    // one, otherwise the first instance. Never creates a second copy -- that is
    // openAnotherInstance.
    void openToolById(
        std::string_view toolId,
        WorkspaceToolState::Presentation presentation = WorkspaceToolState::Presentation::docked);
    // "Open another": a new instance of a multi-instance tool, when its policy
    // and the free audio consumer slots allow (multi-instance-tools).
    void openAnotherInstance(std::string_view toolId);
    [[nodiscard]] ToolInstanceAllocator::OpenAnother
    canOpenAnotherInstance(std::string_view toolId) const;
    // Which tools hold AudioInputService's sample streams right now, for the
    // message shown when they run out.
    [[nodiscard]] AudioStreamUse::Use audioStreamUse() const;
    void showNoAudioStreamAlert() const;
    void showRenameToolDialog(const ToolInstanceId& instance);

    // Leaves the application with no target note, from the Tools menu or any
    // tool's own menu, and saves that.
    void clearTarget();
    void renameTool(const ToolInstanceId& instance, const juce::String& requestedName);
    // The open instances of one tool, lowest ordinal first.
    [[nodiscard]] std::vector<ToolInstanceId> openInstancesOf(std::string_view toolId) const;
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
    // Language, note naming, the MIDI keyboard and pedal, metronome, playback,
    // reference pitch, intonation, the level floor and the target's flags:
    // what loading and importing settings both apply.
    void applyPracticePreferences(const AppSettings::State& state);
    void closeSettings();
    void showHelpMenu();

    // The application's first File menu, hung off the File button that has
    // existed unwired since the shell was built.
    void showFileMenu();

    // Ask for a file, then import it. Dismissing the chooser does nothing.
    void openScore();

    // Open the score the application ships, through the same import path as a
    // chosen file. Says so in an alert if the sample cannot be written out.
    void openSampleScore();

    // Start reading `file` on the import thread. Does nothing but report when
    // an import is already running -- see ScoreImportJob::start.
    void startScoreImport(const juce::File& file);

    // The import window saying `file` is being read: what every import shows
    // until its result arrives. Separate so a capture can show that moment
    // and hold it, which a real import of a small file passes in milliseconds.
    void showScoreImportProgress(const juce::File& file);

    // Deliver a finished import: the summary is shown, and a successful score
    // becomes the current one. Runs on the message thread.
    void finishScoreImport(const score::musicxml::MusicXmlImportResult& result);

    // Show the import window, creating it if it is not open. Re-openable and
    // non-modal: an import is not a question the user has to answer.
    void showScoreImportWindow();
    void closeScoreImportWindow();
    // The open score's import summary, again: the Score View's diagnostics
    // label (#350). Nothing when no score is open.
    void showCurrentScoreImportSummary();

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

    // The installed SoundFont's path, or empty when it is not beside the
    // application where a platform installs it (score-playback).
    [[nodiscard]] static std::string findSoundFont();
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
    // The instance "open or focus" means: the first live instance of the tool,
    // or its first instance when none is live. Every caller that wants "the
    // tuner" -- settings, presets, legacy state, the test channel -- wants this.
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
    // Where docked tools go: the window inside its margin, below the microphone
    // warning while that is showing. One function so the warning's reservation
    // and the empty-workspace guidance cannot disagree about it.
    [[nodiscard]] juce::Rectangle<int> workspaceArea() const;
    // Whether any tool is open, docked or floating. Not `liveTools.empty()`: a
    // closed tool keeps its entry, and its saved settings, so that reopening
    // brings it back as it was.
    [[nodiscard]] bool anyToolOpen() const;
    void paintEmptyWorkspaceGuidance(juce::Graphics& graphics) const;
    [[nodiscard]] juce::Component* buildWorkspaceNode(const WorkspaceLayoutState::Node* node);
    [[nodiscard]] std::vector<ToolInstanceId> dockedTools();
    [[nodiscard]] std::optional<ToolInstanceId> dockedToolAt(juce::Point<int> position);
    [[nodiscard]] DropTarget
    resolveDropTarget(juce::Point<int> position, std::optional<ToolInstanceId> dragged);
    [[nodiscard]] std::optional<ToolInstanceId>
    draggedTool(const juce::DragAndDropTarget::SourceDetails& details) const;
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;

    // Appearance ------------------------------------------------------------
    void setTheme(Theme theme);

    // The text size, as a percentage (global-text-size): the look-and-feel
    // scales every widget, each tool is told, and every window lays itself
    // out again at once.
    void setTextScale(int percent);

    // The note naming was changed in Settings: saved, and everything that names
    // a note redrawn.
    void noteNamingChanged();

    // The interface language chosen ("" for Automatic): saved, for the next
    // start -- every label takes its words when it is made.
    void chooseLanguage(const juce::String& code);
    void showLanguageRestartNote() const;

    // Language chosen in Settings, as stored; "" is Automatic.
    juce::String chosenLanguage;

    // A size laid out for 100 percent, grown with the text size.
    [[nodiscard]] int scaled(int base) const noexcept
    {
        return appearance::scaledHeight(base, appLookAndFeel.textScale());
    }
    void applyAppearance();
    void configureLookAndFeelColours();
    void applyAppearanceToTopButtons();
    void applyAppearanceToOpenWindows();

    // Microphone state ------------------------------------------------------
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

  public:
    // Public for the test control channel, which reports it so a capture can
    // wait for something to analyse rather than photograph a tool that has not
    // heard anything yet.
    [[nodiscard]] bool hasUsableMicrophone() const;

  private:
    void updateMicrophoneStateControl();
    void updateMicrophoneWarning();
    void paintMutedFrame(juce::Graphics& graphics) const;
    void dismissMicrophoneWarning();

    // One audio device manager is shared by every open analysis tool.
    AudioInputService audioInputService;
    // The one place pitch detection runs, shared by the tuner and the
    // harmonic analyzer instead of each duplicating it. Declared after
    // audioInputService, which its constructor registers a listener on -- see
    // the note on liveTools below for why declaration order here matters at
    // all.
    SharedPitchAnalysis pitchAnalysis{audioInputService};

    // The MIDI keyboard. Independent of audioInputService on purpose -- it
    // opens its own device rather than going through that service's
    // AudioDeviceManager -- so a pianist with a keyboard and no microphone is
    // unaffected by audio-device recovery, which is the one thing here that can
    // block without limit.
    //
    // Declared before liveTools for the reason the services above are: a member
    // declared after it would be destroyed while something still borrowed it,
    // and reverse-order destruction is the whole of the enforcement. Nothing in
    // src/features reads it yet; when one does, it arrives through ToolServices
    // and not by reaching in here.
    MidiInputService midiInputService;
    // Where the sustain pedal counts as down (1-127). Persisted, and applied by
    // whoever tracks note durations; the settings panel's tracker is the only one.
    int midiPedalDownThreshold = AppDefaults::Midi::pedalDownThreshold;
    juce::ApplicationProperties applicationProperties;
    // Declared here, before liveTools, on purpose -- see the note on liveTools
    // below. AppLookAndFeel differs from LookAndFeel_V4 only in carrying the
    // embedded UI typeface.
    AppLookAndFeel appLookAndFeel;

    // The title strip's: JUCE's own look plus the text size
    // (global-text-size). Outlives the strip, which the window destroys first.
    TitleStripLookAndFeel titleStripLookAndFeel;

    // The current score, and the summary of the import that produced it. The
    // shell stands in for #39's session file until it exists.
    //
    // Declared here, before liveTools, for the same reason audioInputService
    // and appLookAndFeel are: the Target Note tool borrows the CurrentScore
    // holder inside this through ToolServices, and a member declared after
    // liveTools would be destroyed while tools still held it. That ordering is
    // the whole of the enforcement -- there is no runtime check -- so do not
    // move this declaration below liveTools.
    ScoreImportState scoreImport;

    // The practice clock, shared by every tool through ToolServices. Declared
    // after audioInputService, whose callback runs its renderer, and after
    // scoreImport, whose score it plays -- so it is destroyed before both, and
    // its destructor takes the renderer back from the callback while the
    // service still exists. Declared before liveTools so every tool that
    // borrows it is gone first. The same ordering rule as the members above:
    // do not move it.
    // The application's A4 (system-wide-target-note). Before the live tools,
    // which read it.
    pitch::ReferencePitch referencePitch;

    // The note the player is aiming at, set from each score opened.
    pitch::CurrentTarget currentTarget;

    transport::PracticeClock practiceClock{
        [this](RealtimeRenderer* renderer) { audioInputService.setRealtimeRenderer(renderer); },
        scoreImport.currentScoreHolder()};

    // What a take reads the keyboard as: a consumer of its own, registered only
    // while a take records from the keyboard.
    struct TakeKeyboardConsumer final : MidiInputService::Consumer
    {
    };
    TakeKeyboardConsumer takeKeyboard;

    // Records takes from the microphone and the keyboard on the practice
    // clock's timeline (openspec/changes/archive/2026-11-08-takes). Declared after the clock, the
    // MIDI service and the audio service it records from, so it ends a live
    // take and lets go of them before they go; before the transport strip and
    // the live tools, which use it.
    take::TakeRecorder takeRecorder{practiceClock, scoreImport.currentScoreHolder(), takeSources()};
    [[nodiscard]] take::TakeSources takeSources();

    // Keeps a score's target on the note being sung while the clock plays
    // (openspec/changes/archive/2026-11-09-now-and-next, decision 5). After the clock it listens
    // to and the target it writes.
    TargetClockFollower targetFollower{
        practiceClock, scoreImport.currentScoreHolder(), currentTarget,
        [this] { return outputLatencyFrames(); }};
    [[nodiscard]] int outputLatencyFrames();

    // The clock's controls, which the window's control strip lays out beside
    // the menus. Declared after the clock it listens to, so it stops listening
    // before the clock goes. Saving the metronome switch goes through the
    // shell, which owns the settings file.
    TransportStrip transportStrip{practiceClock, takeRecorder, [this] { saveSettings(false); }};

    juce::TextButton fileButton{l10n::text("title.file")};

    // Shown in the empty workspace, under the guidance text, so a first launch
    // has a way to see a score without owning one. Hidden whenever a tool is
    // open: it is part of the guidance, not of the workspace.
    juce::TextButton openSampleButton{l10n::text("workspace.empty.open-sample")};
    juce::TextButton settingsButton{l10n::text("title.settings")};
    juce::TextButton toolsButton{l10n::text("title.tools")};
    juce::TextButton helpButton{l10n::text("title.help")};
    juce::TextButton microphoneButton;

    // Every tool instance the session knows about, live or closed.
    //
    // Declared after audioInputService, pitchAnalysis, and appLookAndFeel on
    // purpose: members are destroyed in reverse declaration order, so this
    // dies first and every tool is torn down before the services it
    // borrowed. That ordering is the whole of how "a tool cannot outlive its
    // shared services" is enforced -- there is no runtime check, so do not
    // move this declaration above them.
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
    std::unique_ptr<MicrophoneWarning> microphoneWarning;

    // Constructed on first use rather than at startup: a session that never
    // opens a score never creates an import thread. Declared after
    // scoreImport (far above, before liveTools), so the job -- whose
    // destructor stops the import thread -- is destroyed before both the
    // tools and the state its callback writes into.
    std::unique_ptr<ScoreImportJob> scoreImportJob;

    // Held as a member for the same reason settingsTransferChooser is: an
    // asynchronously launched FileChooser must outlive the call that launched
    // it.
    std::unique_ptr<juce::FileChooser> scoreChooser;

    // Opened when an import starts and reused for its result, so "reading..."
    // and "here is what was read" are the same surface.
    std::unique_ptr<ScoreImportWindow> scoreImportWindow;

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

    // Suppresses the score import summary window for the state being applied,
    // so a state that opens a score *into a tool* photographs the tool rather
    // than a report sitting over it. Set from the approved state on the way in
    // and cleared by every state that does not ask for it, so it cannot leak
    // from one capture to the next.
    bool testControlHideScoreImportSummary = false;

    // Start the practice clock once the state's score has imported. Set from
    // the approved state and cleared by every state that does not ask for it.
    bool testControlPlayClock = false;
    int testControlClockBar = -1;
    bool testControlRecordTake = false;
    bool testControlReviewTake = false;
    bool testControlSungTake = false;
    bool testControlOpenMix = false;
    void startClockForTestControl(int attemptsLeft);

    // Set the first time a state is applied, and never cleared: this process is
    // being driven by the harness rather than used by a person.
    //
    // Read by maybeOfferFeedbackInvitation, which must not raise its AlertWindow
    // during a run. The invitation is triggered by closing a tool, which is the
    // first thing establishing a state does, and a modal window over a surface
    // is photographed as a black rectangle -- an obscured window has no pixels
    // to capture. Cancelling the modal instead does not work: the alert is
    // raised asynchronously, so it appears after the state is established.
    bool testControlDriving = false;
#endif
    bool automaticSettingsSaveEnabled = true;
    bool feedbackInvitationsDisabledState = false;
    bool sessionMicrophoneMuteEnabled = false;
    bool persistedMicrophoneMuted = false;
    std::unique_ptr<juce::XmlElement> pendingAudioDeviceState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
