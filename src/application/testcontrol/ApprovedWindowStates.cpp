#include "ApprovedWindowStates.h"

#include <algorithm>

namespace testcontrol
{
namespace
{
// The approved states. Adding one here is what makes it verifiable; there is
// deliberately no way for the harness to compose a state of its own.
const std::vector<ApprovedWindowState>& states()
{
    static const std::vector<ApprovedWindowState> approved{
        {"empty", "", ToolPresentation::none, false, false, "The shell with no tool open"},
        {"tuner-docked", "tuner", ToolPresentation::docked, false, false,
         "The tuner, docked in the workspace"},
        {"tuner-floating", "tuner", ToolPresentation::floating, false, false,
         "The tuner, in its own floating window"},
        {"spectrogram-docked", "spectrogram", ToolPresentation::docked, false, false,
         "The spectrogram, docked in the workspace"},
        {"spectrogram-floating", "spectrogram", ToolPresentation::floating, false, false,
         "The spectrogram, in its own floating window"},
        {"harmonics-docked", "harmonics", ToolPresentation::docked, false, false,
         "The harmonic analyser, docked in the workspace"},
        {"harmonics-floating", "harmonics", ToolPresentation::floating, false, false,
         "The harmonic analyser, in its own floating window"},
        {"settings-open", "", ToolPresentation::none, true, false, "The settings window"},
        {"feedback-open", "", ToolPresentation::none, false, true, "The feedback form"},
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
} // namespace testcontrol
