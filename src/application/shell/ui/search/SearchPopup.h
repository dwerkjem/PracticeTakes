#pragma once

#include <JuceHeader.h>

#include <functional>
#include <vector>

#include "platform/search/SearchIndex.h"

// The search bar and what it finds, opened from the magnifying glass in the
// title strip or with Ctrl+K (openspec/changes/archive/2026-11-10-everything-search, decision 6).
//
// It searches as the reader types, lists the best matches with what kind each
// is, and runs the one chosen: by click, or by the arrow keys and Return.
// Escape, or a click anywhere else, closes it -- not the focus moving, which a
// window's own activation does too. It knows nothing of what a result
// does: it asks `search` for hits and hands the chosen one to `choose`.
class SearchPopup final
    : public juce::Component,
      private juce::ListBoxModel,
      private juce::KeyListener
{
  public:
    using Search = std::function<std::vector<search::SearchHit>(const juce::String&)>;
    using Choose = std::function<void(const search::SearchHit&)>;

    SearchPopup(Search searchFor, Choose chooseHit, std::function<void()> closePopup);
    ~SearchPopup() override;

    SearchPopup(const SearchPopup&) = delete;
    SearchPopup& operator=(const SearchPopup&) = delete;

    // Put the caret in the box. Called once it is on screen.
    void focus();

    // Search again for what is typed: the index changed under it.
    void refresh();

    // What is typed, as if the reader typed it. For the capture harness.
    void setQuery(const juce::String& query);

    [[nodiscard]] int preferredHeight() const;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void lookAndFeelChanged() override;

  private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected)
        override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;
    void returnKeyPressed(int row) override;

    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;
    void pressedSomewhere(const juce::MouseEvent& event);
    void closeLater();

    // Every press in the application, for closing on one outside it.
    struct OutsidePresses final : juce::MouseListener
    {
        explicit OutsidePresses(SearchPopup& popup) : owner(popup) {}
        void mouseDown(const juce::MouseEvent& event) override
        {
            owner.pressedSomewhere(event);
        }
        SearchPopup& owner;
    };
    OutsidePresses outsidePresses{*this};

    void chooseRow(int row);

    Search search;
    Choose choose;
    std::function<void()> close;

    juce::TextEditor box;
    juce::ListBox list;
    juce::Label hint;
    std::vector<search::SearchHit> hits;
};
