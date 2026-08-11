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
    for (const auto& definition : builtInToolRegistry().tools())
    {
        const auto instance = instanceIdToOpen(definition.id);
        if (toolIsOpen(instance))
        {
            closeTool(instance);
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

    // Set before the tools are built, so the first analysis frame they draw
    // already has something in it rather than a second of "waiting for input".
    audioInputService.setSyntheticTone(static_cast<float>(state.toneHz));
    isMicrophoneWarningDismissed = false;
    updateMicrophoneStateControl();
    updateMicrophoneWarning();

    const auto presentation =
        state.presentation == ToolPresentation::floating
            ? WorkspaceToolState::Presentation::floating
            : WorkspaceToolState::Presentation::docked;

    std::vector<ToolInstanceId> opened;

    for (const std::string& name : state.tools)
    {
        // Resolved through the catalog, including its aliases, so the approved
        // list can keep using a tool's historical name.
        const auto toolId = builtInToolRegistry().catalog().resolve(name);
        if (!toolId.has_value())
        {
            // The approved list named a tool this build does not have, which
            // means the two have drifted. Failing is the point.
            return false;
        }

        const auto instance = instanceIdToOpen(*toolId);
        openTool(instance, presentation);
        opened.push_back(instance);
    }

    if (state.arrangement == WorkspaceArrangement::tabbed)
    {
        setWorkspaceLayout(WorkspaceLayoutState::Layout::tabbed);
    }
    else if (state.arrangement == WorkspaceArrangement::split)
    {
        // openTool always inserts with DropZone::centre, which auto-tabs
        // alongside whatever is already docked -- so two tools opened normally
        // share a tab strip, and applyLayoutCommand(horizontal) only
        // re-orients an *existing* split rather than creating one. Tiling has
        // to be asked for explicitly, by placing each tool beside the previous.
        for (std::size_t index = 1; index < opened.size(); ++index)
        {
            const auto* current = findLiveTool(opened[index]);
            const auto* previous = findLiveTool(opened[index - 1]);
            if (current == nullptr || previous == nullptr)
            {
                return false;
            }
            workspaceLayoutState.insert(
                current->handle, previous->handle, WorkspaceLayoutState::DropZone::right);
        }

        rebuildWorkspaceContainer();
    }

    // A state that names no view must still be in a known one. Left alone, a
    // tool keeps whatever the previous state selected, so the same state
    // captured twice in a run could show two different displays depending on
    // what preceded it -- which is exactly what "tuner-in-tune" did, showing
    // the graph in the first palette pass and the meter in the second.
    //
    // resetToDefaults is the tool's own answer to "what do you look like with
    // nothing asked of you", and the shell still does not know what that means.
    if (state.toolView.empty())
    {
        for (auto& live : liveTools)
        {
            if (live.component != nullptr)
            {
                live.component->resetToDefaults();
            }
        }
    }

    if (!state.toolView.empty())
    {
        // Asked of the tool by name. The shell does not know which tool it is
        // holding, and a view the tool does not have is a failure rather than a
        // quiet capture of the default.
        auto shown = false;

        for (auto& live : liveTools)
        {
            if (live.component != nullptr && live.component->showView(state.toolView))
            {
                shown = true;
            }
        }

        if (!shown)
        {
            return false;
        }
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
            else if (state.settingsPanel == "practice")
            {
                panel = "Practice";
            }
            else if (state.settingsPanel == "reset")
            {
                panel = "Reset & support";
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

bool MainComponent::applyTestControlTheme(const std::string& theme)
{
    // Goes through setTheme rather than repainting anything here, so a
    // captured surface is in exactly the palette a user would get from the
    // appearance settings -- including the settings window and any floating
    // tool, which setTheme already reaches.
    if (theme == "dark")
    {
        setTheme(Theme::dark);

        return true;
    }

    if (theme == "light")
    {
        setTheme(Theme::light);

        return true;
    }

    return false;
}

bool MainComponent::applyTestControlGeometry(const std::string& geometry)
{
    auto* window = dynamic_cast<juce::ResizableWindow*>(getTopLevelComponent());

    if (window == nullptr)
    {
        return false;
    }

    // setSize rather than an external resize. The window advertises a 980px
    // minimum width via setResizeLimits and a window manager honours it, but
    // that is above MainTitleBar's 900px collapse threshold -- so only the
    // application resizing itself can reach the collapsed menu.
    if (geometry == "normal")
    {
        window->setFullScreen(false);
        window->setSize(1280, 800);

        return true;
    }

    if (geometry == "narrow")
    {
        window->setFullScreen(false);
        window->setSize(800, 600);

        return true;
    }

    // Smaller than any window manager would let a user make it, deliberately.
    // Most of what manual verification finds is a layout running out of room,
    // and 640x480 is where a docked tool has to choose what to drop -- the
    // question #113 asks, asked of every tool at once.
    if (geometry == "tiny")
    {
        window->setFullScreen(false);
        window->setSize(640, 480);

        return true;
    }

    // Tall and thin, and short and wide. A window this shape is what puts a
    // *pane* below the size its tool can draw at -- the sweep's other
    // geometries shrink the window but leave a single docked tool a pane with
    // plenty of room, so the compact displays never appear in a capture.
    //
    // Both are outside what a window manager would allow, like "tiny", because
    // reaching a small pane through the window is the only way when one tool
    // fills the workspace.
    if (geometry == "slim")
    {
        window->setFullScreen(false);
        window->setSize(340, 780);

        return true;
    }

    if (geometry == "squat")
    {
        window->setFullScreen(false);
        window->setSize(1100, 300);

        return true;
    }

    if (geometry == "maximised")
    {
        window->setFullScreen(false);

        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            window->setBounds(display->userArea);

            return true;
        }

        return false;
    }

    if (geometry == "fullscreen")
    {
        window->setFullScreen(true);

        return true;
    }

    return false;
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
