#include "ApprovedWindowStates.h"

#include <algorithm>

namespace testcontrol
{
namespace
{
using Tools = std::vector<std::string>;

// Every approved state, in the order `list-states` reports them.
//
// Adding one here is what makes a surface verifiable; there is deliberately no
// way for the harness to compose a state of its own. Removing one removes it
// from manual verification, so this list is the reviewable record of what is
// checked before a release.
const std::vector<ApprovedWindowState>& states()
{
    static const std::vector<ApprovedWindowState> approved{
        // --- The shell on its own ---
        {"empty",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         false,
         "The shell with no tool open"},

        // --- One tool at a time ---
        {"tuner-docked", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner, docked in the workspace", 0.0, "graph"},
        {"tuner-in-tune", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner reading a note dead in tune", 440.0, "graph"},
        {"tuner-sharp", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner reading a note sharp", 452.0, "graph"},
        {"tuner-flat", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner reading a note flat", 428.0, "graph"},
        {"tuner-bar", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner in its bar view, reading a note", 440.0, "bar"},
        {"tuner-meter", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner in its meter view, reading a note", 440.0, "meter"},
        {"tuner-advanced", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner with its advanced settings expanded", 440.0, "advanced"},
        {"spectrogram-tone", Tools{"spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The spectrogram with a steady tone in it", 440.0, ""},
        {"harmonics-tone", Tools{"harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The harmonic analyser reading a sustained pitch", 220.0, ""},
        {"all-tools-tone", Tools{"tuner", "spectrogram", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "Every tool at once, all analysing the same tone", 440.0, ""},
        {"tuner-floating", Tools{"tuner"}, ToolPresentation::floating, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::available, false, "", false,
         "The tuner, in its own floating window"},
        {"spectrogram-docked", Tools{"spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The spectrogram, docked in the workspace"},
        {"spectrogram-floating", Tools{"spectrogram"}, ToolPresentation::floating,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The spectrogram, in its own floating window"},
        {"harmonics-docked", Tools{"harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The harmonic analyser, docked in the workspace"},
        {"harmonics-floating", Tools{"harmonics"}, ToolPresentation::floating,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The harmonic analyser, in its own floating window"},

        // --- Several tools at once. Nothing else reaches the split tree with
        // more than one leaf, or the tab strip at all.
        {"two-tools-split", Tools{"tuner", "spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "The tuner and spectrogram tiled side by side"},
        {"two-tools-tabbed", Tools{"tuner", "spectrogram"}, ToolPresentation::docked,
         WorkspaceArrangement::tabbed, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "The tuner and spectrogram sharing one tab strip"},
        {"tuner-harmonics-split", Tools{"tuner", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "The tuner and the harmonic analyser tiled side by side"},
        {"three-tools-tabbed", Tools{"tuner", "spectrogram", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::tabbed, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", false, "All three tools sharing one tab strip"},
        {"all-tools-docked", Tools{"tuner", "spectrogram", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::available, false,
         "", false, "All three tools tiled at once, each consuming live audio"},

        // --- Window geometry ---
        {"narrow-window", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::narrow, MicrophoneCondition::available, false, "", false,
         "A window narrow enough that the title bar collapses to the hamburger menu"},
        {"fullscreen", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::fullscreen, MicrophoneCondition::available, false, "", false,
         "The application in fullscreen"},
        {"narrow-empty", Tools{}, ToolPresentation::none, WorkspaceArrangement::single,
         WindowGeometry::narrow, MicrophoneCondition::available, false, "", false,
         "The empty shell in a narrow window, with the collapsed menu and no tool"},

        // --- Audio conditions ---
        {"microphone-muted", Tools{"tuner"}, ToolPresentation::docked, WorkspaceArrangement::single,
         WindowGeometry::normal, MicrophoneCondition::muted, false, "", false,
         "The tuner with the global microphone mute engaged"},
        {"muted-all-tools", Tools{"tuner", "spectrogram", "harmonics"}, ToolPresentation::docked,
         WorkspaceArrangement::split, WindowGeometry::normal, MicrophoneCondition::muted, false, "",
         false, "Every tool at once with the microphone muted"},
        {"microphone-warning",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::unavailable,
         false,
         "",
         false,
         "The no-usable-input warning, which a working microphone otherwise hides"},

        // --- Settings and feedback ---
        {"settings-open",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "",
         false,
         "The settings window on its default panel"},
        {"settings-audio-device",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "audio-device",
         false,
         "The settings window showing audio device selection"},
        {"settings-appearance",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "appearance",
         false,
         "The settings window showing appearance and theme"},
        {"settings-practice",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "practice",
         false,
         "The settings window showing practice presets"},
        {"settings-reset",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         true,
         "reset",
         false,
         "The settings window showing reset and support"},
        {"feedback-over-tool", Tools{"tuner"}, ToolPresentation::docked,
         WorkspaceArrangement::single, WindowGeometry::normal, MicrophoneCondition::available,
         false, "", true, "The feedback form opened over a tool that is already in use"},
        {"feedback-open",
         {},
         ToolPresentation::none,
         WorkspaceArrangement::single,
         WindowGeometry::normal,
         MicrophoneCondition::available,
         false,
         "",
         true,
         "The feedback form"},
    };

    return approved;
}

// The approved click targets. Each id matches the component id set on the
// corresponding object in the shell, so a rename that misses one is caught by
// the click failing rather than by silently clicking nothing.
const std::vector<ApprovedClickTarget>& targets()
{
    static const std::vector<ApprovedClickTarget> approved{
        {"tools-button", "Opens the tools menu"},
        {"settings-button", "Opens the settings menu"},
        {"help-button", "Opens the help menu"},
        {"microphone-button", "Toggles the global microphone mute"},
        {"hamburger-button", "Opens the collapsed menu in a narrow window"},
        {"fullscreen-button", "Toggles fullscreen"},
    };

    return approved;
}

template <typename Item>
[[nodiscard]] const Item* findById(const std::vector<Item>& items, const std::string& id)
{
    const auto match =
        std::find_if(items.begin(), items.end(), [&id](const Item& item) { return item.id == id; });

    return match == items.end() ? nullptr : &*match;
}
} // namespace

const std::vector<ApprovedWindowState>& approvedWindowStates()
{
    return states();
}

const std::vector<ApprovedClickTarget>& approvedClickTargets()
{
    return targets();
}

const ApprovedWindowState* findApprovedWindowState(const std::string& id)
{
    return findById(states(), id);
}

const ApprovedClickTarget* findApprovedClickTarget(const std::string& id)
{
    return findById(targets(), id);
}

const std::vector<std::string>& approvedThemeNames()
{
    static const std::vector<std::string> names{"dark", "light"};

    return names;
}

const std::vector<std::string>& approvedGeometryNames()
{
    static const std::vector<std::string> names{
        "normal", "narrow", "tiny", "maximised", "fullscreen", "slim", "squat"};

    return names;
}
} // namespace testcontrol
