#include "../../MainComponent.h"

#include "../../ui/feedback/FeedbackWindow.h"
#include "../../ui/main_window/MicrophoneWarning.h"
#include "../../ui/score/ScoreImportWindow.h"
#include "../../ui/settings/SettingsWindow.h"

void MainComponent::setTheme(Theme theme)
{
    if (currentTheme == theme)
    {
        return;
    }
    currentTheme = theme;
    applyAppearance();
}

void MainComponent::applyAppearance()
{
    configureLookAndFeelColours();
    applyAppearanceToTopButtons();
    applyAppearanceToOpenWindows();
    sendLookAndFeelChange();
    repaint();
}

void MainComponent::configureLookAndFeelColours()
{
    const auto palette = appPaletteFor(currentTheme);

    appLookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, palette.background);
    appLookAndFeel.setColour(juce::DocumentWindow::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::Label::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    appLookAndFeel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    appLookAndFeel.setColour(juce::TextButton::buttonColourId, palette.button);
    appLookAndFeel.setColour(juce::TextButton::buttonOnColourId, palette.buttonHover);
    appLookAndFeel.setColour(juce::TextButton::textColourOffId, palette.foreground);
    appLookAndFeel.setColour(juce::TextButton::textColourOnId, palette.foreground);
    appLookAndFeel.setColour(juce::ComboBox::backgroundColourId, palette.button);
    appLookAndFeel.setColour(juce::ComboBox::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::ComboBox::outlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::ComboBox::arrowColourId, palette.foreground);
    appLookAndFeel.setColour(juce::PopupMenu::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::PopupMenu::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::PopupMenu::headerTextColourId, palette.muted);
    appLookAndFeel.setColour(
        juce::PopupMenu::highlightedBackgroundColourId, palette.accent.withAlpha(0.7f));
    appLookAndFeel.setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    appLookAndFeel.setColour(juce::TabbedButtonBar::tabTextColourId, palette.foreground);
    appLookAndFeel.setColour(juce::TabbedButtonBar::frontTextColourId, palette.foreground);
    appLookAndFeel.setColour(juce::TextEditor::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::TextEditor::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::TextEditor::highlightColourId, palette.accent);
    appLookAndFeel.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    appLookAndFeel.setColour(juce::TextEditor::outlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::TextEditor::focusedOutlineColourId, palette.accent);
    appLookAndFeel.setColour(juce::AlertWindow::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::AlertWindow::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::AlertWindow::outlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::TooltipWindow::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::TooltipWindow::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::TooltipWindow::outlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::GroupComponent::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::GroupComponent::outlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::ListBox::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::ListBox::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::ListBox::outlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::Slider::backgroundColourId, palette.panel);
    appLookAndFeel.setColour(juce::Slider::trackColourId, palette.accent.withAlpha(0.75f));
    appLookAndFeel.setColour(juce::Slider::thumbColourId, palette.accent);
    appLookAndFeel.setColour(juce::Slider::textBoxTextColourId, palette.foreground);
    appLookAndFeel.setColour(juce::Slider::textBoxBackgroundColourId, palette.button);
    appLookAndFeel.setColour(juce::Slider::textBoxOutlineColourId, palette.outline);
    appLookAndFeel.setColour(juce::ToggleButton::textColourId, palette.foreground);
    appLookAndFeel.setColour(juce::ToggleButton::tickColourId, palette.accent);
    appLookAndFeel.setColour(juce::ToggleButton::tickDisabledColourId, palette.muted);
    setLookAndFeel(&appLookAndFeel);
}

void MainComponent::applyAppearanceToTopButtons()
{
    const auto palette = appPaletteFor(currentTheme);
    for (auto* button : {&fileButton, &settingsButton, &toolsButton, &helpButton})
    {
        button->setColour(juce::TextButton::buttonColourId, palette.button);
        button->setColour(juce::TextButton::buttonOnColourId, palette.buttonHover);
        button->setColour(juce::TextButton::textColourOffId, palette.foreground);
        button->setColour(juce::TextButton::textColourOnId, palette.foreground);
    }
    updateMicrophoneStateControl();
}

void MainComponent::applyAppearanceToOpenWindows()
{
    const auto palette = appPaletteFor(currentTheme);
    if (microphoneWarning != nullptr)
    {
        microphoneWarning->setTheme(currentTheme);
    }
    for (const auto& entry : liveTools)
    {
        if (entry.state.isOpen())
        {
            applyAppearanceToTool(entry.id);
        }
    }
    if (settingsWindow != nullptr)
    {
        settingsWindow->applyAppearance(&appLookAndFeel, palette.background, currentTheme);
    }
    if (feedbackWindow != nullptr)
    {
        feedbackWindow->applyAppearance(&appLookAndFeel, palette.background);
    }
    if (scoreImportWindow != nullptr)
    {
        // Missed when the window landed, and found by its own capture: the
        // report is a TextEditor, which takes its colours from the look and
        // feel when it is told the look and feel changed and not otherwise.
        // Without this the text keeps the palette it was created under, which
        // in the dark theme is near-invisible dark-on-dark -- and only when the
        // window outlived a theme change, so two runs of the same surface
        // disagreed.
        scoreImportWindow->applyAppearance(&appLookAndFeel, palette.background);
    }
}
