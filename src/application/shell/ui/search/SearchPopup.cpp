#include "SearchPopup.h"

#include "../../../localisation/Localisation.h"
#include "../../../search/SearchCatalogue.h"
#include "../../../theme/AppLookAndFeel.h"
#include "../../../theme/Icons.h"

#include <utility>

namespace
{
// Enough to scan without scrolling, few enough to stay a short list.
constexpr std::size_t shownResults = 12;
constexpr int rowHeight = 40;
constexpr int boxHeight = 34;
constexpr int margin = 10;
} // namespace

SearchPopup::SearchPopup(Search searchFor, Choose chooseHit, std::function<void()> closePopup)
    : search(std::move(searchFor)), choose(std::move(chooseHit)), close(std::move(closePopup))
{
    box.setTextToShowWhenEmpty(l10n::text("search.placeholder"), juce::Colours::grey);
    box.setTitle(l10n::text("search.title"));
    box.setComponentID("search-box");
    box.setSelectAllWhenFocused(true);
    box.onTextChange = [this] { refresh(); };
    box.onReturnKey = [this] { chooseRow(std::max(0, list.getSelectedRow())); };
    box.onEscapeKey = [this]
    {
        if (close)
        {
            close();
        }
    };
    box.addKeyListener(this);
    addAndMakeVisible(box);

    list.setModel(this);
    list.setRowHeight(rowHeight);
    list.setComponentID("search-results");
    addAndMakeVisible(list);

    hint.setJustificationType(juce::Justification::centred);
    hint.setText(l10n::text("search.hint"), juce::dontSendNotification);
    addAndMakeVisible(hint);

    juce::Desktop::getInstance().addGlobalMouseListener(&outsidePresses);
    refresh();
}

SearchPopup::~SearchPopup()
{
    juce::Desktop::getInstance().removeGlobalMouseListener(&outsidePresses);
    box.removeKeyListener(this);
}

void SearchPopup::focus()
{
    box.grabKeyboardFocus();
}

void SearchPopup::setQuery(const juce::String& query)
{
    box.setText(query, false);
    box.moveCaretToEnd();
    refresh();
}

void SearchPopup::refresh()
{
    const juce::String query = box.getText();
    hits = search ? search(query) : std::vector<search::SearchHit>{};
    if (hits.size() > shownResults)
    {
        hits.resize(shownResults);
    }
    list.updateContent();
    list.repaint();
    if (!hits.empty())
    {
        list.selectRow(0);
    }
    hint.setText(
        query.trim().isEmpty() ? l10n::text("search.hint")
                               : l10n::format("search.nothing", {{"query", query.trim()}}),
        juce::dontSendNotification);
    hint.setVisible(hits.empty());
    // Anchored at its top, under the magnifying glass: it grows downwards.
    if (getWidth() > 0)
    {
        setSize(getWidth(), preferredHeight());
    }
}

int SearchPopup::preferredHeight() const
{
    const int rows = hits.empty() ? 2 : static_cast<int>(hits.size());
    return appearance::scaledFor(*this, boxHeight + margin * 3) +
           rows * appearance::scaledFor(*this, rowHeight);
}

void SearchPopup::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto& look = getLookAndFeel();
    graphics.setColour(look.findColour(juce::ResizableWindow::backgroundColourId));
    graphics.fillRoundedRectangle(bounds.reduced(0.5f), 8.0f);
    graphics.setColour(look.findColour(juce::ListBox::textColourId).withAlpha(0.3f));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
}

void SearchPopup::resized()
{
    auto bounds = getLocalBounds().reduced(appearance::scaledFor(*this, margin));
    box.setBounds(bounds.removeFromTop(appearance::scaledFor(*this, boxHeight)));
    bounds.removeFromTop(appearance::scaledFor(*this, margin));
    list.setRowHeight(appearance::scaledFor(*this, rowHeight));
    list.setBounds(bounds);
    hint.setBounds(bounds);
}

void SearchPopup::lookAndFeelChanged()
{
    const auto& look = getLookAndFeel();
    list.setColour(
        juce::ListBox::backgroundColourId,
        look.findColour(juce::ResizableWindow::backgroundColourId));
    const auto font =
        appearance::textFont(appearance::TextRole::control, appearance::textScaleOf(*this));
    box.setFont(font);
    box.applyFontToAllText(font, true);
    // A TextEditor keeps the colour its text was typed in until told.
    box.applyColourToAllText(look.findColour(juce::TextEditor::textColourId), true);
    resized();
    repaint();
}

int SearchPopup::getNumRows()
{
    return static_cast<int>(hits.size());
}

void SearchPopup::paintListBoxItem(
    int row,
    juce::Graphics& graphics,
    int width,
    int height,
    bool selected)
{
    if (row < 0 || row >= static_cast<int>(hits.size()))
    {
        return;
    }
    const search::SearchHit& hit = hits[static_cast<std::size_t>(row)];
    const auto& look = getLookAndFeel();
    if (selected)
    {
        graphics.setColour(look.findColour(juce::TextEditor::highlightColourId));
        graphics.fillRoundedRectangle(
            juce::Rectangle<float>(
                0.0f, 1.0f, static_cast<float>(width), static_cast<float>(height - 2)),
            5.0f);
    }
    const juce::Colour ink = look.findColour(
        selected ? juce::TextEditor::highlightedTextColourId : juce::ListBox::textColourId);
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(8, 3);

    // What kind of thing it is, on the left, so a setting and a song of the
    // same name read apart.
    const auto small = appearance::fontFor(*this, appearance::TextRole::small);
    graphics.setFont(small);
    graphics.setColour(ink.withAlpha(0.6f));
    graphics.drawText(
        search::kindLabel(hit.kind), area.removeFromLeft(appearance::scaledFor(*this, 92)),
        juce::Justification::centredLeft, true);

    const auto title = appearance::fontFor(*this, appearance::TextRole::control);
    auto top = area.removeFromTop(area.getHeight() / 2 + 2);
    graphics.setFont(title);
    graphics.setColour(ink);
    graphics.drawText(
        juce::String::fromUTF8(hit.title.c_str()), top, juce::Justification::bottomLeft, true);
    graphics.setFont(small);
    graphics.setColour(ink.withAlpha(0.65f));
    graphics.drawText(
        juce::String::fromUTF8(hit.detail.c_str()), area, juce::Justification::topLeft, true);
}

void SearchPopup::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    chooseRow(row);
}

void SearchPopup::returnKeyPressed(int row)
{
    chooseRow(row);
}

bool SearchPopup::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const int count = static_cast<int>(hits.size());
    if (count == 0)
    {
        return false;
    }
    const int selected = list.getSelectedRow();
    if (key == juce::KeyPress::downKey)
    {
        list.selectRow(std::min(count - 1, selected + 1));
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        list.selectRow(std::max(0, selected - 1));
        return true;
    }
    return false;
}

void SearchPopup::pressedSomewhere(const juce::MouseEvent& event)
{
    // A press anywhere but on it -- or on the magnifying glass, which
    // reopens it -- closes it, whether or not what was pressed takes focus.
    auto* pressed = event.eventComponent;
    if (pressed == nullptr || pressed == this || isParentOf(pressed) ||
        pressed->getComponentID() == "search-button")
    {
        return;
    }
    closeLater();
}

void SearchPopup::closeLater()
{
    // After the event that caused it: closing destroys this.
    if (close)
    {
        juce::MessageManager::callAsync(close);
    }
}

void SearchPopup::chooseRow(int row)
{
    if (row < 0 || row >= static_cast<int>(hits.size()))
    {
        return;
    }
    // Copied: closing destroys this, and the hit with it.
    const search::SearchHit chosen = hits[static_cast<std::size_t>(row)];
    const Choose chooseNow = choose;
    if (close)
    {
        close();
    }
    if (chooseNow)
    {
        chooseNow(chosen);
    }
}
