#pragma once

#include "../../localisation/LocalisationRules.h"

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

// What each is called on screen, by catalogue identifier. The English entries
// are the constants above -- a test holds them together -- so the manifest
// names the window English shows; in the pseudolocale it names
// pseudolocale::transform of the same text.
inline constexpr std::string_view settingsKey = l10n::key("window.settings");
inline constexpr std::string_view feedbackKey = l10n::key("window.feedback");
inline constexpr std::string_view scoreImportKey = l10n::key("window.score-import");
} // namespace shellwindows
