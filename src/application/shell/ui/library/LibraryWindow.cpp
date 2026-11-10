#include "LibraryWindow.h"

#include "../../../localisation/Localisation.h"
#include "../../../theme/AppLookAndFeel.h"
#include "../../../theme/Icons.h"
#include "../ShellWindowTitles.h"

#include <utility>

namespace
{
constexpr int libraryRowHeight = 28;
constexpr int buttonHeight = 30;
constexpr int gap = 8;

juce::String dateOf(std::int64_t milliseconds)
{
    return milliseconds > 0 ? juce::Time(milliseconds).formatted("%Y-%m-%d %H:%M") : juce::String();
}

juce::String lengthOf(double seconds)
{
    const auto whole = static_cast<int>(std::lround(std::max(0.0, seconds)));
    return juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
}

juce::String sizeOf(std::int64_t bytes)
{
    const double megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return l10n::format(
        "library.size.megabytes", {{"size", juce::String(megabytes, megabytes < 10.0 ? 1 : 0)}});
}

juce::String inputsOf(const library::TakeManifest& manifest)
{
    if (manifest.usedMicrophone && manifest.usedKeyboard)
    {
        return l10n::text("library.inputs.both");
    }
    if (manifest.usedMicrophone)
    {
        return l10n::text("library.inputs.microphone");
    }
    return manifest.usedKeyboard ? l10n::text("library.inputs.keyboard") : juce::String();
}

juce::String endingOf(const library::TakeManifest& manifest)
{
    if (manifest.state == take::TakeState::completed)
    {
        return l10n::text("library.ended.completed");
    }
    if (manifest.state == take::TakeState::failed)
    {
        return l10n::text("library.ended.failed");
    }
    return manifest.stoppedInBar.has_value()
               ? l10n::format(
                     "library.ended.stopped-in-bar",
                     {{"bar", l10n::number(static_cast<double>(*manifest.stoppedInBar + 1), 0)}})
               : l10n::text("library.ended.stopped");
}

juce::String joined(const std::vector<std::string>& parts)
{
    juce::StringArray names;
    for (const auto& part : parts)
    {
        names.add(juce::String::fromUTF8(part.c_str()));
    }
    return names.joinIntoString(", ");
}

// A table of the library's rows: the colours of the look and feel, a row
// highlighted when selected, and double-click as the main command.
class LibraryTable final : public juce::TableListBox
{
  public:
    LibraryTable()
    {
        setRowHeight(libraryRowHeight);
        setMultipleSelectionEnabled(false);
        getHeader().setStretchToFitActive(true);
        getHeader().setPopupMenuActive(false);
    }
};
} // namespace

class LibraryWindow::Content final : public juce::Component
{
  public:
    explicit Content(Commands given) : commands(std::move(given))
    {
        for (auto* button : {&songsTab, &takesTab})
        {
            button->setClickingTogglesState(true);
            button->setRadioGroupId(1);
            addAndMakeVisible(*button);
        }
        songsTab.setToggleState(true, juce::dontSendNotification);
        songsTab.onClick = [this] { selectTab(Tab::songs); };
        takesTab.onClick = [this] { selectTab(Tab::takes); };
        appearance::setIcon(songsTab, appearance::Icon::music);
        appearance::setIcon(takesTab, appearance::Icon::microphone);

        songs.model.owner = this;
        takes.model.owner = this;
        songs.table.setModel(&songs.model);
        takes.table.setModel(&takes.model);
        auto& songHeader = songs.table.getHeader();
        songHeader.addColumn(l10n::text("library.column.title"), 1, 260, 80);
        songHeader.addColumn(l10n::text("library.column.composer"), 2, 160, 60);
        songHeader.addColumn(l10n::text("library.column.parts"), 3, 200, 60);
        songHeader.addColumn(l10n::text("library.column.added"), 4, 130, 80);
        auto& takeHeader = takes.table.getHeader();
        takeHeader.addColumn(l10n::text("library.column.recorded"), 1, 140, 80);
        takeHeader.addColumn(l10n::text("library.column.song"), 2, 230, 80);
        takeHeader.addColumn(l10n::text("library.column.length"), 3, 70, 50);
        takeHeader.addColumn(l10n::text("library.column.inputs"), 4, 150, 60);
        takeHeader.addColumn(l10n::text("library.column.ended"), 5, 140, 60);
        takeHeader.addColumn(l10n::text("library.column.size"), 6, 80, 50);
        addAndMakeVisible(songs.table);
        addChildComponent(takes.table);

        for (auto* button : {&openButton, &addButton, &removeButton, &songFolderButton})
        {
            addAndMakeVisible(*button);
        }
        for (auto* button : {&reviewButton, &deleteButton, &takeFolderButton})
        {
            addChildComponent(*button);
        }
        addChildComponent(thisSongOnly);
        appearance::setIcon(openButton, appearance::Icon::folderOpen);
        appearance::setIcon(reviewButton, appearance::Icon::play);

        openButton.onClick = [this] { openSelectedSong(); };
        addButton.onClick = [this] { run(commands.addSongs); };
        removeButton.onClick = [this]
        {
            if (const auto* song = selectedSong(); song != nullptr && commands.removeSong)
            {
                commands.removeSong(*song);
            }
        };
        songFolderButton.onClick = [this]
        {
            const auto* song = selectedSong();
            if (commands.showInFolder)
            {
                commands.showInFolder(song != nullptr ? song->file : folder);
            }
        };
        reviewButton.onClick = [this] { reviewSelectedTake(); };
        deleteButton.onClick = [this]
        {
            if (const auto* entry = selectedTake(); entry != nullptr && commands.deleteTake)
            {
                commands.deleteTake(*entry);
            }
        };
        takeFolderButton.onClick = [this]
        {
            const auto* entry = selectedTake();
            if (commands.showInFolder)
            {
                commands.showInFolder(entry != nullptr ? entry->folder : folder);
            }
        };
        thisSongOnly.onClick = [this] { filterTakes(); };

        folderLabel.setMinimumHorizontalScale(0.6f);
        addAndMakeVisible(folderLabel);
        addAndMakeVisible(changeFolderButton);
        addAndMakeVisible(refreshButton);
        changeFolderButton.onClick = [this] { run(commands.changeFolder); };
        refreshButton.onClick = [this] { run(commands.refresh); };
        statusLabel.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(statusLabel);
        emptyLabel.setJustificationType(juce::Justification::centred);
        addChildComponent(emptyLabel);

        updateButtons();
    }

    void setSongs(std::vector<library::SongEntry> entries)
    {
        songs.entries = std::move(entries);
        songs.table.updateContent();
        songs.table.repaint();
        filterTakes();
        updateButtons();
    }

    void setTakes(std::vector<library::TakeEntry> entries)
    {
        allTakes = std::move(entries);
        filterTakes();
    }

    void setFolder(const juce::File& libraryFolder, bool reachable)
    {
        folder = libraryFolder;
        folderLabel.setText(
            l10n::format("library.folder", {{"folder", folder.getFullPathName()}}),
            juce::dontSendNotification);
        folderLabel.setTooltip(folder.getFullPathName());
        folderReachable = reachable;
        if (!reachable)
        {
            setStatus(
                l10n::format("library.folder.unreachable", {{"folder", folder.getFullPathName()}}));
        }
        updateButtons();
    }

    void setStatus(const juce::String& status)
    {
        statusLabel.setText(status, juce::dontSendNotification);
    }

    void selectTab(Tab tab)
    {
        shown = tab;
        songsTab.setToggleState(tab == Tab::songs, juce::dontSendNotification);
        takesTab.setToggleState(tab == Tab::takes, juce::dontSendNotification);
        const bool songsShown = tab == Tab::songs;
        songs.table.setVisible(songsShown);
        takes.table.setVisible(!songsShown);
        for (auto* button : {&openButton, &addButton, &removeButton, &songFolderButton})
        {
            button->setVisible(songsShown);
        }
        for (juce::Component* control :
             {static_cast<juce::Component*>(&reviewButton),
              static_cast<juce::Component*>(&deleteButton),
              static_cast<juce::Component*>(&takeFolderButton),
              static_cast<juce::Component*>(&thisSongOnly)})
        {
            control->setVisible(!songsShown);
        }
        updateButtons();
        resized();
    }

    [[nodiscard]] Tab tab() const
    {
        return shown;
    }

    void lookAndFeelChanged() override
    {
        // The header in the window's colours, not JUCE's light grey, which in
        // the dark theme was a pale bar of black text.
        auto& look = getLookAndFeel();
        const juce::Colour ink = look.findColour(juce::ListBox::textColourId);
        const juce::Colour paper = look.findColour(juce::TextEditor::backgroundColourId);
        const juce::Colour window = look.findColour(juce::ResizableWindow::backgroundColourId);
        for (auto* table : {&songs.table, &takes.table})
        {
            table->setColour(juce::ListBox::backgroundColourId, paper);
            auto& header = table->getHeader();
            header.setColour(juce::TableHeaderComponent::backgroundColourId, window);
            header.setColour(
                juce::TableHeaderComponent::highlightColourId, window.contrasting(0.08f));
            header.setColour(juce::TableHeaderComponent::textColourId, ink);
            header.setColour(juce::TableHeaderComponent::outlineColourId, ink.withAlpha(0.25f));
            table->setOutlineThickness(1);
            table->setColour(juce::ListBox::outlineColourId, ink.withAlpha(0.25f));
            table->repaint();
        }
        resized();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(16);
        const int button = appearance::scaledFor(*this, buttonHeight);

        auto tabs = bounds.removeFromTop(button);
        songsTab.setBounds(tabs.removeFromLeft(appearance::scaledFor(*this, 120)));
        tabs.removeFromLeft(gap);
        takesTab.setBounds(tabs.removeFromLeft(appearance::scaledFor(*this, 120)));
        bounds.removeFromTop(gap);

        auto footer = bounds.removeFromBottom(button);
        refreshButton.setBounds(footer.removeFromRight(appearance::scaledFor(*this, 100)));
        footer.removeFromRight(gap);
        changeFolderButton.setBounds(footer.removeFromRight(appearance::scaledFor(*this, 110)));
        footer.removeFromRight(gap);
        folderLabel.setBounds(footer);
        bounds.removeFromBottom(gap);
        statusLabel.setBounds(bounds.removeFromBottom(appearance::scaledFor(*this, 22)));
        bounds.removeFromBottom(gap);

        auto actions = bounds.removeFromBottom(button);
        bounds.removeFromBottom(gap);
        const auto place = [&actions, this](juce::Component& control, int width)
        {
            control.setBounds(actions.removeFromLeft(appearance::scaledFor(*this, width)));
            actions.removeFromLeft(gap);
        };
        if (shown == Tab::songs)
        {
            place(openButton, 110);
            place(addButton, 140);
            place(removeButton, 110);
            place(songFolderButton, 150);
        }
        else
        {
            place(reviewButton, 110);
            place(deleteButton, 110);
            place(takeFolderButton, 150);
            thisSongOnly.setBounds(actions);
        }

        songs.table.setBounds(bounds);
        takes.table.setBounds(bounds);
        emptyLabel.setBounds(bounds.reduced(24));
    }

  private:
    struct SongModel final : juce::TableListBoxModel
    {
        Content* owner = nullptr;

        int getNumRows() override
        {
            return static_cast<int>(owner->songs.entries.size());
        }

        void paintRowBackground(juce::Graphics& graphics, int, int, int, bool selected) override
        {
            owner->paintRow(graphics, selected);
        }

        void paintCell(juce::Graphics& graphics, int row, int column, int width, int height, bool)
            override
        {
            if (row < 0 || row >= getNumRows())
            {
                return;
            }
            const auto& song = owner->songs.entries[static_cast<std::size_t>(row)];
            juce::String text;
            switch (column)
            {
            case 1:
                text = juce::String::fromUTF8(song.title.c_str());
                if (song.builtIn)
                {
                    text = l10n::format("library.song.built-in", {{"title", text}});
                }
                else if (!song.readable)
                {
                    text = l10n::format("library.song.unreadable", {{"title", text}});
                }
                break;
            case 2:
                text = juce::String::fromUTF8(song.composer.c_str());
                break;
            case 3:
                text = joined(song.parts);
                break;
            default:
                text = song.builtIn ? juce::String() : dateOf(song.addedMs);
                break;
            }
            owner->paintText(graphics, text, width, height, !song.readable);
        }

        void selectedRowsChanged(int) override
        {
            owner->filterTakes();
            owner->updateButtons();
        }

        void cellDoubleClicked(int, int, const juce::MouseEvent&) override
        {
            owner->openSelectedSong();
        }

        void returnKeyPressed(int) override
        {
            owner->openSelectedSong();
        }

        void deleteKeyPressed(int) override
        {
            owner->removeButton.triggerClick();
        }
    };

    struct TakeModel final : juce::TableListBoxModel
    {
        Content* owner = nullptr;

        int getNumRows() override
        {
            return static_cast<int>(owner->takes.entries.size());
        }

        void paintRowBackground(juce::Graphics& graphics, int, int, int, bool selected) override
        {
            owner->paintRow(graphics, selected);
        }

        void paintCell(juce::Graphics& graphics, int row, int column, int width, int height, bool)
            override
        {
            if (row < 0 || row >= getNumRows())
            {
                return;
            }
            const auto& entry = owner->takes.entries[static_cast<std::size_t>(row)];
            juce::String text;
            if (!entry.manifest.has_value())
            {
                text = column == 1   ? dateOf(entry.recordedMs())
                       : column == 2 ? l10n::text(entry.error)
                                     : juce::String();
                owner->paintText(graphics, text, width, height, true);
                return;
            }
            const auto& manifest = *entry.manifest;
            switch (column)
            {
            case 1:
                text = dateOf(manifest.recordedMs);
                break;
            case 2:
                text = juce::String::fromUTF8(manifest.scoreTitle.c_str());
                break;
            case 3:
                text = lengthOf(manifest.performedSeconds());
                break;
            case 4:
                text = inputsOf(manifest);
                break;
            case 5:
                text = endingOf(manifest);
                break;
            default:
                text = sizeOf(entry.bytes);
                break;
            }
            owner->paintText(graphics, text, width, height, false);
        }

        void selectedRowsChanged(int) override
        {
            owner->updateButtons();
        }

        void cellDoubleClicked(int, int, const juce::MouseEvent&) override
        {
            owner->reviewSelectedTake();
        }

        void returnKeyPressed(int) override
        {
            owner->reviewSelectedTake();
        }

        void deleteKeyPressed(int) override
        {
            owner->deleteButton.triggerClick();
        }
    };

    template <typename Entry, typename Model> struct Listing
    {
        std::vector<Entry> entries;
        Model model;
        LibraryTable table;
    };

    static void run(const std::function<void()>& command)
    {
        if (command)
        {
            command();
        }
    }

    void paintRow(juce::Graphics& graphics, bool selected)
    {
        if (selected)
        {
            graphics.fillAll(getLookAndFeel().findColour(juce::TextEditor::highlightColourId));
        }
    }

    void
    paintText(juce::Graphics& graphics, const juce::String& text, int width, int height, bool faint)
    {
        auto colour = getLookAndFeel().findColour(juce::ListBox::textColourId);
        graphics.setColour(faint ? colour.withAlpha(0.55f) : colour);
        graphics.setFont(appearance::fontFor(*this, appearance::TextRole::control));
        graphics.drawText(text, 6, 0, width - 10, height, juce::Justification::centredLeft, true);
    }

    [[nodiscard]] const library::SongEntry* selectedSong() const
    {
        const int row = songs.table.getSelectedRow();
        return row >= 0 && row < static_cast<int>(songs.entries.size())
                   ? &songs.entries[static_cast<std::size_t>(row)]
                   : nullptr;
    }

    [[nodiscard]] const library::TakeEntry* selectedTake() const
    {
        const int row = takes.table.getSelectedRow();
        return row >= 0 && row < static_cast<int>(takes.entries.size())
                   ? &takes.entries[static_cast<std::size_t>(row)]
                   : nullptr;
    }

    void openSelectedSong()
    {
        if (const auto* song = selectedSong();
            song != nullptr && song->readable && commands.openSong)
        {
            commands.openSong(song->file);
        }
    }

    void reviewSelectedTake()
    {
        if (const auto* entry = selectedTake();
            entry != nullptr && entry->manifest.has_value() && commands.reviewTake)
        {
            commands.reviewTake(entry->folder);
        }
    }

    // The takes shown: every one, or the selected song's when asked.
    void filterTakes()
    {
        const juce::File selected =
            takes.table.getSelectedRow() >= 0 && selectedTake() != nullptr
                ? selectedTake()->folder
                : juce::File();
        const auto* song = selectedSong();
        thisSongOnly.setEnabled(song != nullptr);
        thisSongOnly.setButtonText(
            song != nullptr ? l10n::format(
                                  "library.take.this-song",
                                  {{"title", juce::String::fromUTF8(song->title.c_str())}})
                            : l10n::text("library.take.this-song.none"));
        takes.entries.clear();
        for (const auto& entry : allTakes)
        {
            if (!thisSongOnly.getToggleState() || song == nullptr ||
                (entry.manifest.has_value() && entry.manifest->scoreTitle == song->title))
            {
                takes.entries.push_back(entry);
            }
        }
        takes.table.updateContent();
        int row = -1;
        for (std::size_t index = 0; index < takes.entries.size(); ++index)
        {
            if (takes.entries[index].folder == selected)
            {
                row = static_cast<int>(index);
            }
        }
        if (row >= 0)
        {
            takes.table.selectRow(row);
        }
        else
        {
            takes.table.deselectAllRows();
        }
        takes.table.repaint();
        updateButtons();
    }

    void updateButtons()
    {
        const auto* song = selectedSong();
        openButton.setEnabled(song != nullptr && song->readable);
        removeButton.setEnabled(song != nullptr && !song->builtIn);
        addButton.setEnabled(folderReachable);
        const auto* entry = selectedTake();
        reviewButton.setEnabled(entry != nullptr && entry->manifest.has_value());
        deleteButton.setEnabled(entry != nullptr);

        const bool empty = shown == Tab::takes && takes.entries.empty();
        emptyLabel.setText(
            allTakes.empty() ? l10n::text("library.take.none")
                             : l10n::text("library.take.none-for-song"),
            juce::dontSendNotification);
        emptyLabel.setVisible(empty);
    }

    Commands commands;
    Tab shown = Tab::songs;
    juce::File folder;
    bool folderReachable = true;

    juce::TextButton songsTab{l10n::text("library.tab.songs")};
    juce::TextButton takesTab{l10n::text("library.tab.takes")};

    Listing<library::SongEntry, SongModel> songs;
    Listing<library::TakeEntry, TakeModel> takes;
    std::vector<library::TakeEntry> allTakes;

    juce::TextButton openButton{l10n::text("library.song.open")};
    juce::TextButton addButton{l10n::text("library.song.add")};
    juce::TextButton removeButton{l10n::text("library.song.remove")};
    juce::TextButton songFolderButton{l10n::text("library.show-in-folder")};
    juce::TextButton reviewButton{l10n::text("library.take.review")};
    juce::TextButton deleteButton{l10n::text("library.take.delete")};
    juce::TextButton takeFolderButton{l10n::text("library.show-in-folder")};
    juce::ToggleButton thisSongOnly;

    juce::Label folderLabel;
    juce::TextButton changeFolderButton{l10n::text("library.folder.change")};
    juce::TextButton refreshButton{l10n::text("library.refresh")};
    juce::Label statusLabel;
    juce::Label emptyLabel;
};

LibraryWindow::LibraryWindow(Commands commands)
    : DocumentWindow(
          l10n::text(shellwindows::libraryKey),
          juce::Colours::darkgrey,
          juce::DocumentWindow::allButtons),
      onClose(commands.close)
{
    setUsingNativeTitleBar(true);
    auto owned = std::make_unique<Content>(std::move(commands));
    content = owned.get();
    setContentOwned(owned.release(), false);
    setResizable(true, true);
    setResizeLimits(620, 380, 2400, 1600);
    centreWithSize(900, 560);
    setVisible(true);
}

LibraryWindow::~LibraryWindow() = default;

void LibraryWindow::showSongs(std::vector<library::SongEntry> songs)
{
    content->setSongs(std::move(songs));
}

void LibraryWindow::showTakes(std::vector<library::TakeEntry> takes)
{
    content->setTakes(std::move(takes));
}

void LibraryWindow::showFolder(const juce::File& folder, bool reachable)
{
    content->setFolder(folder, reachable);
}

void LibraryWindow::showStatus(const juce::String& status)
{
    content->setStatus(status);
}

void LibraryWindow::showTab(Tab tab)
{
    content->selectTab(tab);
}

LibraryWindow::Tab LibraryWindow::tab() const
{
    return content->tab();
}

void LibraryWindow::applyAppearance(juce::LookAndFeel* appearance, juce::Colour background)
{
    setLookAndFeel(appearance);
    setBackgroundColour(background);
    sendLookAndFeelChange();
    repaint();
}

void LibraryWindow::closeButtonPressed()
{
    setVisible(false);
    if (onClose)
    {
        juce::MessageManager::callAsync(onClose);
    }
}
