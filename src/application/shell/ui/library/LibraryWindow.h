#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

#include "platform/library/LibraryListing.h"

// The media library's window, from File -> Library... (openspec/changes/
// media-library, decision 7): the songs and the takes in the library folder,
// and the commands on them.
//
// It shows what it is given and asks the shell to do everything else, so it
// knows nothing of the recorder, the importer or the disk: the shell lists the
// folder on a worker thread and hands the result here.
class LibraryWindow final : public juce::DocumentWindow
{
  public:
    struct Commands
    {
        std::function<void(const juce::File&)> openSong;
        std::function<void()> addSongs;
        std::function<void(const library::SongEntry&)> removeSong;
        std::function<void(const juce::File&)> reviewTake;
        std::function<void(const library::TakeEntry&)> deleteTake;
        std::function<void(const juce::File&)> showInFolder;
        std::function<void()> changeFolder;
        std::function<void()> refresh;
        std::function<void()> close;
    };

    enum class Tab
    {
        songs,
        takes
    };

    explicit LibraryWindow(Commands commands);
    ~LibraryWindow() override;

    void showSongs(std::vector<library::SongEntry> songs);
    void showTakes(std::vector<library::TakeEntry> takes);

    // The folder, and whether it can be reached; a folder that cannot is said
    // so in the status line, and nothing is listed from it.
    void showFolder(const juce::File& folder, bool reachable);

    // A line about what just happened: a take that could not be saved, a
    // song that could not be copied in. Empty clears it.
    void showStatus(const juce::String& status);

    void showTab(Tab tab);
    [[nodiscard]] Tab tab() const;

    void applyAppearance(juce::LookAndFeel* appearance, juce::Colour background);

    void closeButtonPressed() override;

  private:
    class Content;
    Content* content = nullptr;
    std::function<void()> onClose;
};
