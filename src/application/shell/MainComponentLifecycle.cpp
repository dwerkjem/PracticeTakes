#include "MainComponent.h"

#include <algorithm>

#include "ui/feedback/FeedbackWindow.h"
#include "ui/main_window/MainTitleBar.h"
#include "ui/main_window/MicrophoneWarning.h"
#include "ui/score/BundledSampleScore.h"
#include "ui/score/ScoreImportWindow.h"
#include "ui/settings/SettingsWindow.h"
#include "ui/workspace/components/DockedToolPanel.h"
#include "ui/workspace/components/ToolWindow.h"

#include "../../platform/transport/SoundFontLocation.h"

namespace
{
constexpr int microphoneWarningWidth = 470;
constexpr int microphoneWarningHeight = 118;

// Space between the microphone warning and the workspace laid out below it.
constexpr int microphoneWarningGap = 10;

// The margin every edge of the workspace keeps from the window.
constexpr int workspaceMargin = 18;

// The empty-workspace guidance: a title, two lines of text, and the sample
// button under them, centred as one block.
constexpr int guidanceTitleHeight = 32;
constexpr int guidanceTextHeight = 48;
constexpr int sampleButtonWidth = 220;
constexpr int sampleButtonHeight = 32;
constexpr int guidanceBlockHeight = guidanceTitleHeight + guidanceTextHeight + sampleButtonHeight;

juce::Rectangle<int> emptyGuidanceBlock(juce::Rectangle<int> area)
{
    return area.withSizeKeepingCentre(juce::jmin(area.getWidth(), 560), guidanceBlockHeight);
}

juce::String settingsFolderName()
{
#if JUCE_LINUX || JUCE_BSD
    // PropertiesFile puts Linux settings straight under $HOME, which leaves a
    // visible folder among the user's own directories. An absolute folderName
    // overrides that, so resolve the XDG config location instead.
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("PracticeTakes")
        .getFullPathName();
#else
    return "PracticeTakes";
#endif
}

void migrateLegacySettingsFile(const juce::File& destination)
{
#if JUCE_LINUX || JUCE_BSD
    if (destination.existsAsFile())
    {
        return;
    }

    const auto legacyFolder =
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("PracticeTakes");
    const auto legacyFile = legacyFolder.getChildFile("PracticeTakes.settings");

    if (!legacyFile.existsAsFile() || !destination.getParentDirectory().createDirectory().wasOk())
    {
        return;
    }

    if (legacyFile.moveFileTo(destination))
    {
        // Removes the directory only when it is empty, so anything else the
        // user kept there survives.
        legacyFolder.deleteFile();
    }
#else
    juce::ignoreUnused(destination);
#endif
}
} // namespace

std::string MainComponent::findSoundFont()
{
    const juce::File executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    for (const std::string& candidate : transport::soundFontCandidates(
             executable.getParentDirectory().getFullPathName().toStdString()))
    {
        const juce::File file{juce::String(candidate)};
        if (file.existsAsFile())
        {
            return file.getFullPathName().toStdString();
        }
    }
    return {};
}

MainComponent::MainComponent(bool muteMicrophoneForSession, bool declineAudioInput)
    // Declaration order, not argument order: audioInputService is declared well
    // before sessionMicrophoneMuteEnabled, so the parameter is used directly
    // rather than reading the member, which is not initialised yet.
    : audioInputService(declineAudioInput), sessionMicrophoneMuteEnabled(muteMicrophoneForSession)
{
    setOpaque(true);
    audioInputService.addChangeListener(this);

    juce::PropertiesFile::Options storageOptions;
    storageOptions.applicationName = "PracticeTakes";
    storageOptions.filenameSuffix = ".settings";
    storageOptions.folderName = settingsFolderName();
    storageOptions.osxLibrarySubFolder = "Application Support";
    storageOptions.commonToAllUsers = false;
    storageOptions.millisecondsBeforeSaving = -1;
    storageOptions.storageFormat = juce::PropertiesFile::storeAsXML;
    migrateLegacySettingsFile(storageOptions.getDefaultFile());
    applicationProperties.setStorageParameters(storageOptions);
    loadSettings();
    persistedMicrophoneMuted = audioInputService.isMuted();
    if (sessionMicrophoneMuteEnabled)
    {
        audioInputService.setMuted(true);
    }

    // The score's sounds load on a worker thread; the clock picks them up when
    // they are ready (openspec/changes/archive/2026-11-08-score-playback).
    practiceClock.loadSoundFont(findSoundFont());

    configureTopButtons();
    createMicrophoneWarning();
    applyAppearance();
    updateMicrophoneStateControl();
    updateMicrophoneWarning();
    setSize(1200, 760);
}

MainComponent::~MainComponent()
{
    saveSettings();
    audioInputService.removeChangeListener(this);
    settingsWindow.reset();
    feedbackWindow.reset();
    workspaceContainers.clear();
    // Explicit, and before microphoneWarning, so every tool is gone while the
    // audio service and look-and-feel it borrowed are still alive. Member
    // destruction order would do this anyway -- see the liveTools declaration
    // -- but a tool's destructor deregisters from audio, so making it visible
    // here keeps that dependency from looking accidental.
    liveTools.clear();
    microphoneWarning.reset();
    applicationProperties.closeFiles();
    setLookAndFeel(nullptr);
}

void MainComponent::configureTopButtons()
{
    fileButton.setTooltip("Open a score");
    fileButton.onClick = [this] { showFileMenu(); };
    settingsButton.onClick = [this] { showSettingsMenu(); };
    toolsButton.onClick = [this] { showToolsMenu(); };
    helpButton.onClick = [this] { showHelpMenu(); };
    microphoneButton.setTitle("Global microphone mute control");
    microphoneButton.onClick = [this] { audioInputService.toggleMuted(); };
    openSampleButton.setTooltip(bundled_sample::menuLabel());
    openSampleButton.onClick = [this] { openSampleScore(); };
    addChildComponent(openSampleButton);

    // Component ids let the development-only test control channel address these
    // by name rather than by screen position, so the manual GUI harness never
    // synthesises a pointer or a key. The names must match the approved click
    // targets in src/application/testcontrol/ApprovedWindowStates.cpp -- a
    // rename that misses one shows up as a click failing, not as a click
    // silently landing on nothing.
    fileButton.setComponentID("file-button");
    settingsButton.setComponentID("settings-button");
    toolsButton.setComponentID("tools-button");
    helpButton.setComponentID("help-button");
    microphoneButton.setComponentID("microphone-button");
    openSampleButton.setComponentID("open-sample-button");
}

take::TakeSources MainComponent::takeSources()
{
    // Called while the recorder is being constructed, before the members
    // after it exist: every function only captures `this` and reads members
    // declared before the recorder when it is called.
    take::TakeSources sources;
    sources.microphoneOpen = [this] { return audioInputService.hasUsableInput(); };
    sources.microphoneMuted = [this] { return audioInputService.isMuted(); };
    sources.keyboardOpen = [this]
    { return midiInputService.connectionState() == MidiInputService::ConnectionState::open; };
    sources.beginKeyboard = [this]
    {
        midiInputService.addConsumer(&takeKeyboard);
        midiInputService.discardPendingEvents(&takeKeyboard);
    };
    sources.endKeyboard = [this] { midiInputService.removeConsumer(&takeKeyboard); };
    sources.readKeyboard = [this](midi::MidiEvent* destination, std::size_t maximum)
    { return midiInputService.readEvents(&takeKeyboard, destination, maximum); };
    sources.latencies = [this]
    {
        auto* device = audioInputService.deviceManager().getCurrentAudioDevice();
        return device != nullptr
                   ? std::pair<
                         int,
                         int>{device->getInputLatencyInSamples(), device->getOutputLatencyInSamples()}
                   : std::pair<int, int>{0, 0};
    };
    sources.referencePitch = [this] { return referencePitch.hertz(); };
    return sources;
}

std::unique_ptr<MainTitleBar> MainComponent::createTitleBar(std::function<void()> fullscreenHandler)
{
    return std::make_unique<MainTitleBar>(
        fileButton, settingsButton, toolsButton, helpButton, microphoneButton, transportStrip,
        std::move(fullscreenHandler));
}

AppSettings::FullscreenMode MainComponent::fullscreenMode() const noexcept
{
    return selectedFullscreenMode;
}

void MainComponent::initialiseAudioAfterLaunch()
{
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync(
        [safeThis]
        {
            if (safeThis == nullptr)
            {
                return;
            }

            if (safeThis->pendingAudioDeviceState != nullptr)
            {
                safeThis->audioInputService.applySavedDeviceState(
                    *safeThis->pendingAudioDeviceState);
            }
            else
            {
                safeThis->audioInputService.initialiseDefaultInput();
            }

            safeThis->pendingAudioDeviceState.reset();
            safeThis->updateMicrophoneStateControl();
            safeThis->updateMicrophoneWarning();
        });
}

void MainComponent::createMicrophoneWarning()
{
    microphoneWarning = std::make_unique<MicrophoneWarning>(
        [this] { showSettings(); }, [this] { dismissMicrophoneWarning(); });
    addChildComponent(*microphoneWarning);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(appPaletteFor(currentTheme).background);

    if (!anyToolOpen())
    {
        paintEmptyWorkspaceGuidance(graphics);
    }
}

bool MainComponent::anyToolOpen() const
{
    return std::any_of(
        liveTools.begin(), liveTools.end(),
        [](const LiveTool& tool) { return tool.state.isOpen(); });
}

juce::Rectangle<int> MainComponent::workspaceArea() const
{
    auto area = getLocalBounds().reduced(workspaceMargin);

    // Laid out below the warning rather than under it. It used to float over
    // the workspace's top-right corner, so on a machine with no microphone it
    // covered whatever tool was docked there -- the Score View's first bars
    // included, for someone who only came to read music and needs no
    // microphone for it. Reserving its height keeps every tool clear of it,
    // whichever tool happens to be in that corner.
    if (microphoneWarning != nullptr && microphoneWarning->isVisible())
    {
        area.removeFromTop(microphoneWarningHeight + microphoneWarningGap);
    }

    return area;
}

void MainComponent::paintEmptyWorkspaceGuidance(juce::Graphics& graphics) const
{
    // What a new user sees on first launch, and what anyone sees after closing
    // the last tool: before this, a blank window with nothing to say that a
    // score can be opened or that the Tools menu holds anything. Two lines, not
    // a tour -- the menus are right above it.
    const AppPalette colours = appPaletteFor(currentTheme);
    auto area = workspaceArea().reduced(24);
    auto block = emptyGuidanceBlock(area);

    graphics.setColour(colours.foreground);
    graphics.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    graphics.drawFittedText(
        "Open a score to get started", block.removeFromTop(guidanceTitleHeight),
        juce::Justification::centred, 1);

    graphics.setColour(colours.muted);
    graphics.setFont(juce::FontOptions(14.5f));
    graphics.drawFittedText(
        "File > Open score reads a MusicXML file and shows it here, or try the sample "
        "below. The Tools menu adds a tuner, spectrogram or harmonic analyser.",
        block.removeFromTop(guidanceTextHeight), juce::Justification::centredTop, 3);
}

void MainComponent::resized()
{
    if (microphoneWarning != nullptr)
    {
        auto warningArea = getLocalBounds().reduced(workspaceMargin);
        const auto availableWidth = juce::jmin(microphoneWarningWidth, warningArea.getWidth());
        microphoneWarning->setBounds(
            warningArea.removeFromTop(microphoneWarningHeight).removeFromRight(availableWidth));
        if (microphoneWarning->isVisible())
        {
            microphoneWarning->toFront(false);
        }
    }

    layoutWorkspace(workspaceArea());

    // Part of the guidance painted behind it, so it follows the same rule:
    // visible exactly when no tool is open, laid out under the text.
    const bool showGuidance = !anyToolOpen();
    openSampleButton.setVisible(showGuidance);

    if (showGuidance)
    {
        auto block = emptyGuidanceBlock(workspaceArea().reduced(24));
        block.removeFromTop(guidanceTitleHeight + guidanceTextHeight);
        openSampleButton.setBounds(block.withSizeKeepingCentre(
            juce::jmin(block.getWidth(), sampleButtonWidth), sampleButtonHeight));
    }

    // The background carries the empty-workspace guidance, which appears and
    // disappears as tools open and close. Every open and close, docked or
    // floating, rebuilds the workspace and lands here, so this is the one place
    // that keeps it current.
    repaint();
}
