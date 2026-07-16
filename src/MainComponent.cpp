#include "MainComponent.h"

namespace
{
const auto backgroundColour = juce::Colour::fromRGB(18, 20, 27);
const auto panelColour = juce::Colour::fromRGB(31, 35, 46);
const auto panelOutlineColour = juce::Colour::fromRGB(58, 65, 82);
const auto foregroundColour = juce::Colour::fromRGB(238, 241, 247);
const auto mutedColour = juce::Colour::fromRGB(142, 150, 166);
const auto accentColour = juce::Colour::fromRGB(100, 170, 255);
}

MainComponent::MainComponent()
{
    setOpaque(true);

    const auto configureLabel = [this](
                                    juce::Label& label,
                                    const juce::String& text,
                                    float fontSize,
                                    bool bold)
    {
        label.setText(text, juce::dontSendNotification);
        label.setColour(
            juce::Label::textColourId,
            foregroundColour);
        label.setFont(juce::FontOptions(
            fontSize,
            bold ? juce::Font::bold : juce::Font::plain));
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    };

    configureLabel(appTitle, "PRACTICE TAKES", 38.0f, true);
    configureLabel(
        appSubtitle,
        "Focused tools for better practice sessions",
        18.0f,
        false);
    appSubtitle.setColour(juce::Label::textColourId, mutedColour);

    configureLabel(toolsHeading, "TOOLS", 16.0f, true);
    toolsHeading.setColour(juce::Label::textColourId, mutedColour);

    configureLabel(tunerTitle, "Tuner", 24.0f, true);
    configureLabel(
        tunerDescription,
        "Tune instruments and check pitch from any selected microphone.",
        16.0f,
        false);
    tunerDescription.setColour(juce::Label::textColourId, mutedColour);

    configureLabel(pageTitle, "TUNER", 19.0f, true);
    pageTitle.setJustificationType(juce::Justification::centred);
    pageTitle.setVisible(false);

    const auto configureButton = [](juce::TextButton& button)
    {
        button.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour::fromRGB(54, 59, 72));
        button.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour::fromRGB(70, 92, 130));
        button.setColour(
            juce::TextButton::textColourOffId,
            foregroundColour);
        button.setMouseCursor(
            juce::MouseCursor(juce::MouseCursor::PointingHandCursor));
    };

    configureButton(tunerButton);
    tunerButton.onClick = [this]
    {
        showTuner();
    };
    addAndMakeVisible(tunerButton);

    configureButton(backButton);
    backButton.onClick = [this]
    {
        showHomePage();
    };
    addChildComponent(backButton);

    setSize(820, 620);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(backgroundColour);

    if (showingTuner)
    {
        auto header = getLocalBounds().removeFromTop(70);

        graphics.setColour(panelColour);
        graphics.fillRect(header);

        graphics.setColour(panelOutlineColour);
        graphics.drawHorizontalLine(
            header.getBottom() - 1,
            0.0f,
            static_cast<float>(getWidth()));
        return;
    }

    graphics.setColour(panelColour);
    graphics.fillRoundedRectangle(tunerCardBounds.toFloat(), 14.0f);

    graphics.setColour(panelOutlineColour);
    graphics.drawRoundedRectangle(
        tunerCardBounds.toFloat(),
        14.0f,
        1.0f);

    auto iconBounds = tunerCardBounds
        .withWidth(64)
        .withHeight(64)
        .withCentre({
            tunerCardBounds.getX() + 56,
            tunerCardBounds.getCentreY()
        });

    graphics.setColour(accentColour.withAlpha(0.18f));
    graphics.fillEllipse(iconBounds.toFloat());

    graphics.setColour(accentColour);
    graphics.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    graphics.drawText(
        "A",
        iconBounds,
        juce::Justification::centred);
}

void MainComponent::resized()
{
    if (showingTuner)
    {
        auto content = getLocalBounds();
        auto header = content.removeFromTop(70);

        backButton.setBounds(
            header.removeFromLeft(180).reduced(18, 13));

        pageTitle.setBounds(
            getLocalBounds()
                .removeFromTop(70)
                .reduced(190, 0));

        if (tunerComponent != nullptr)
        {
            tunerComponent->setBounds(content);
        }

        return;
    }

    auto content = getLocalBounds().reduced(52);

    appTitle.setBounds(content.removeFromTop(56));
    appSubtitle.setBounds(content.removeFromTop(34));

    content.removeFromTop(54);
    toolsHeading.setBounds(content.removeFromTop(30));
    content.removeFromTop(12);

    tunerCardBounds = content.removeFromTop(152);

    auto cardContent = tunerCardBounds.reduced(24);
    cardContent.removeFromLeft(80);

    auto actionArea = cardContent.removeFromRight(160);
    tunerButton.setBounds(
        actionArea.withSizeKeepingCentre(144, 44));

    cardContent.removeFromRight(20);
    tunerTitle.setBounds(cardContent.removeFromTop(40));
    tunerDescription.setBounds(cardContent.removeFromTop(52));
}

void MainComponent::showHomePage()
{
    if (! showingTuner)
    {
        return;
    }

    showingTuner = false;
    tunerComponent.reset();

    backButton.setVisible(false);
    pageTitle.setVisible(false);
    setHomeControlsVisible(true);

    resized();
    repaint();
}

void MainComponent::showTuner()
{
    if (showingTuner)
    {
        return;
    }

    showingTuner = true;
    setHomeControlsVisible(false);

    tunerComponent = std::make_unique<TunerComponent>();
    addAndMakeVisible(*tunerComponent);

    backButton.setVisible(true);
    pageTitle.setVisible(true);
    backButton.toFront(false);
    pageTitle.toFront(false);

    resized();
    repaint();
}

void MainComponent::setHomeControlsVisible(bool shouldBeVisible)
{
    appTitle.setVisible(shouldBeVisible);
    appSubtitle.setVisible(shouldBeVisible);
    toolsHeading.setVisible(shouldBeVisible);
    tunerTitle.setVisible(shouldBeVisible);
    tunerDescription.setVisible(shouldBeVisible);
    tunerButton.setVisible(shouldBeVisible);
}
