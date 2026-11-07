#pragma once

#include <string_view>

// The titles of the shell's own top-level windows.
//
// Declared here, JUCE-free, rather than as literals in each window's
// constructor, because something outside the shell reads them too: the capture
// harness finds a window by its title, and learns which title each approved
// state shows from the manifest ApprovedStatesManifest generates out of these.
// A window renamed here is renamed in that manifest with it.
//
// A floating tool's window is titled with its tool's display name, from the
// tool catalog, so it has no entry here.
namespace shellwindows
{
inline constexpr std::string_view settings = "Settings";
inline constexpr std::string_view feedback = "Send feedback";
inline constexpr std::string_view scoreImport = "Score import";
} // namespace shellwindows
