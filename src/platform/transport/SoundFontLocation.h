#pragma once

#include <string>
#include <vector>

// Where the SoundFont is installed, relative to the executable (no JUCE
// dependency). openspec/changes/score-playback/design.md, decision 2; the
// layouts are listed in tools/packaging/sounds/README.md.
namespace transport
{
inline constexpr const char* soundFontFileName = "GeneralUser-GS.sf2";

// The places to look, in order, given the directory holding the executable:
// beside it (a build tree, Windows), Linux's share directory, and the macOS
// bundle's Resources (the executable is in Contents/MacOS).
[[nodiscard]] inline std::vector<std::string>
soundFontCandidates(const std::string& executableDirectory)
{
    const std::string sounds = std::string("sounds/") + soundFontFileName;
    return {
        executableDirectory + "/" + sounds,
        executableDirectory + "/../share/practice-takes/" + sounds,
        executableDirectory + "/../Resources/" + sounds,
    };
}
} // namespace transport
