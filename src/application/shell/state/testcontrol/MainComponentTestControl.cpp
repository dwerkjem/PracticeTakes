#include "../../MainComponent.h"

#if PRACTICE_TAKES_ENABLE_TEST_CONTROL

#include <optional>

#include "../../../testcontrol/TestControlClicker.h"
#include "../../ui/feedback/FeedbackWindow.h"
#include "../../ui/settings/SettingsWindow.h"

// Establishing an approved window state, and running an approved object's
// action. Development-only: the whole file is compiled away without
// PRACTICE_TAKES_ENABLE_TEST_CONTROL.
//
// Everything here runs on the message thread; the channel marshals to it before
// calling, so none of this locks.

bool MainComponent::applyTestControlState(const testcontrol::ApprovedWindowState& state)
{
    using testcontrol::MicrophoneCondition;
    using testcontrol::ToolPresentation;
    using testcontrol::WindowGeometry;
    using testcontrol::WorkspaceArrangement;

    // Not in a known state until this succeeds. If anything below fails, the
    // application is left in some intermediate arrangement, and reporting a
    // state id for it would be a lie the harness then records as verified.
    testControlStateId.clear();

    // Reset first, so states do not accumulate across a run and each one is
    // reached the same way regardless of what preceded it.
    for (const auto tool : {ToolType::tuner, ToolType::spectrogram, ToolType::harmonics})
    {
        if (toolIsOpen(tool))
        {
            closeTool(tool);
        }
    }

    if (settingsWindow != nullptr)
    {
        settingsWindow->setVisible(false);
    }

    if (feedbackWindow != nullptr)
    {
        feedbackWindow->setVisible(false);
    }

    testControlNoMicrophone = state.microphone == MicrophoneCondition::unavailable;
    audioInputService.setMuted(state.microphone == MicrophoneCondition::muted);
    isMicrophoneWarningDismissed = false;
    updateMicrophoneStateControl();
    updateMicrophoneWarning();

    const auto presentation =
        state.presentation == ToolPresentation::floating
            ? WorkspaceToolState::Presentation::floating
            : WorkspaceToolState::Presentation::docked;

    for (const std::string& name : state.tools)
    {
        std::optional<ToolType> tool;

        if (name == "tuner")
        {
            tool = ToolType::tuner;
        }
        else if (name == "spectrogram")
        {
            tool = ToolType::spectrogram;
        }
        else if (name == "harmonics")
        {
            tool = ToolType::harmonics;
        }

        if (!tool.has_value())
        {
            // The approved list named a tool this build does not have, which
            // means the two have drifted. Failing is the point.
            return false;
        }

        openTool(*tool, presentation);
    }

    if (state.arrangement == WorkspaceArrangement::tabbed)
    {
        setWorkspaceLayout(WorkspaceLayoutState::Layout::tabbed);
    }
    else if (state.arrangement == WorkspaceArrangement::split)
    {
        setWorkspaceLayout(WorkspaceLayoutState::Layout::horizontal);
    }

    if (state.settingsOpen)
    {
        showSettings();

        if (!state.settingsPanel.empty())
        {
            // Panel names are the tab captions in SettingsWindow. Mapping them
            // here rather than storing captions in the approved list keeps the
            // list independent of the UI's wording.
            juce::String panel;

            if (state.settingsPanel == "audio-device")
            {
                panel = "Audio";
            }
            else if (state.settingsPanel == "appearance")
            {
                panel = "Appearance";
            }

            if (panel.isEmpty() || settingsWindow == nullptr || !settingsWindow->showPanel(panel))
            {
                return false;
            }
        }
    }

    if (state.feedbackOpen)
    {
        showFeedback();
    }

    if (auto* window = dynamic_cast<juce::ResizableWindow*>(getTopLevelComponent()))
    {
        switch (state.geometry)
        {
        case WindowGeometry::narrow:
            window->setFullScreen(false);
            // Below MainTitleBar's 900px threshold, which is what makes the
            // collapsed hamburger menu appear.
            window->setSize(800, 600);
            break;

        case WindowGeometry::fullscreen:
            window->setFullScreen(true);
            break;

        case WindowGeometry::normal:
            window->setFullScreen(false);
            window->setSize(1280, 800);
            break;
        }
    }

    testControlStateId = state.id;

    return true;
}

bool MainComponent::clickTestControlTarget(const std::string& id)
{
    // Search from the window, not from this component: the title bar objects
    // are siblings of the workspace, not children of it.
    juce::Component* root = getTopLevelComponent();

    if (root == nullptr)
    {
        root = this;
    }

    const bool clicked = testcontrol::clickComponentById(*root, id);

    if (clicked)
    {
        // A click may have opened a menu or changed presentation, so the
        // application is no longer necessarily in the state that was applied.
        testControlStateId.clear();
    }

    return clicked;
}

std::string MainComponent::currentTestControlStateId() const
{
    return testControlStateId;
}

#endif
