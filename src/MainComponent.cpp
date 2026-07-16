#include "MainComponent.h"

namespace
{
const auto backgroundColour = juce::Colour::fromRGB(18, 20, 27);
const auto panelColour = juce::Colour::fromRGB(31, 35, 46);
const auto panelOutlineColour = juce::Colour::fromRGB(58, 65, 82);
const auto foregroundColour = juce::Colour::fromRGB(238, 241, 247);
const auto mutedColour = juce::Colour::fromRGB(142, 150, 166);
}

MainComponent::MainComponent()
{
    setOpaque(true);

    const auto configureLabel = [this](juce::Label& label,
                                       const juce::String& text,
                                       float fontSize,
                                       bool bold)
    {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, foregroundColour);
        label.setFont(juce::FontOptions(
            fontSize,
            bold ? juce::Font::bold : juce::Font::plain));
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    };

    configureLabel(appTitle, "PRACTICE TAKES", 28.0f, true);
    configureLabel(appSubtitle, "Live practice analysis workspace", 15.0f, false);
    appSubtitle.setColour(juce::Label::textColourId, mutedColour);
    configureLabel(microphoneLabel, "Microphone: none selected", 14.0f, false);
    microphoneLabel.setColour(juce::Label::textColourId, mutedColour);
    configureLabel(workspaceLabel, "OPEN TOOLS", 13.0f, true);
    workspaceLabel.setColour(juce::Label::textColourId, mutedColour);

    const auto configureButton = [](juce::Button& button)
    {
        button.setColour(juce::TextButton::buttonColourId,
                         juce::Colour::fromRGB(54, 59, 72));
        button.setColour(juce::TextButton::buttonOnColourId,
                         juce::Colour::fromRGB(70, 92, 130));
        button.setColour(juce::TextButton::textColourOffId, foregroundColour);
        button.setColour(juce::TextButton::textColourOnId, foregroundColour);
    };

    configureButton(settingsButton);
    configureButton(settingsDoneButton);
    configureButton(tunerToggle);
    configureButton(spectrogramToggle);

    settingsButton.onClick = [this] { showAudioSettings(); };
    settingsDoneButton.onClick = [this] { hideAudioSettings(); };
    tunerToggle.onClick = [this] { toggleTuner(); };
    spectrogramToggle.onClick = [this] { toggleSpectrogram(); };

    addAndMakeVisible(settingsButton);
    addChildComponent(settingsDoneButton);
    addAndMakeVisible(tunerToggle);
    addAndMakeVisible(spectrogramToggle);

    juce::XmlElement noDeviceState { "DEVICESETUP" };
    const auto error = audioDeviceManager.initialise(2, 0, &noDeviceState, false);
    audioErrorMessage = error.isNotEmpty() ? error : "Select a microphone in Audio settings.";
    audioDeviceManager.addChangeListener(this);

    tunerToggle.setToggleState(true, juce::dontSendNotification);
    tunerComponent = std::make_unique<TunerComponent>(audioDeviceManager);
    addAndMakeVisible(*tunerComponent);

    updateMicrophoneLabel();
    setSize(1180, 760);
}

MainComponent::~MainComponent()
{
    spectrogramComponent.reset();
    tunerComponent.reset();
    audioDeviceSelector.reset();
    audioDeviceManager.removeChangeListener(this);
    audioDeviceManager.closeAudioDevice();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(backgroundColour);

    graphics.setColour(panelColour);
    graphics.fillRect(headerBounds);
    graphics.fillRect(toolbarBounds);

    graphics.setColour(panelOutlineColour);
    graphics.drawHorizontalLine(headerBounds.getBottom() - 1,
                                0.0f,
                                static_cast<float>(getWidth()));
    graphics.drawHorizontalLine(toolbarBounds.getBottom() - 1,
                                0.0f,
                                static_cast<float>(getWidth()));

    if (showingAudioSettings)
        return;

    if (tunerComponent == nullptr && spectrogramComponent == nullptr)
    {
        graphics.setColour(mutedColour);
        graphics.setFont(juce::FontOptions(20.0f));
        graphics.drawText("Choose one or more tools above.",
                          workspaceBounds,
                          juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    headerBounds = bounds.removeFromTop(76);
    toolbarBounds = bounds.removeFromTop(58);
    workspaceBounds = bounds.reduced(12);

    auto header = headerBounds.reduced(20, 10);
    auto settingsArea = header.removeFromRight(190);
    settingsButton.setBounds(settingsArea.reduced(10, 6));
    microphoneLabel.setBounds(header.removeFromRight(340));
    appTitle.setBounds(header.removeFromTop(34));
    appSubtitle.setBounds(header);

    auto toolbar = toolbarBounds.reduced(20, 8);
    workspaceLabel.setBounds(toolbar.removeFromLeft(110));
    tunerToggle.setBounds(toolbar.removeFromLeft(120).reduced(5, 2));
    spectrogramToggle.setBounds(toolbar.removeFromLeft(160).reduced(5, 2));

    if (showingAudioSettings)
    {
        auto settings = workspaceBounds.reduced(20);
        auto footer = settings.removeFromBottom(48);
        if (audioDeviceSelector != nullptr)
            audioDeviceSelector->setBounds(settings);
        settingsDoneButton.setBounds(footer.removeFromRight(130).reduced(4));
        return;
    }

    const auto hasTuner = tunerComponent != nullptr;
    const auto hasSpectrogram = spectrogramComponent != nullptr;

    if (hasTuner && hasSpectrogram)
    {
        auto left = workspaceBounds.removeFromLeft(workspaceBounds.getWidth() / 2);
        left.removeFromRight(6);
        workspaceBounds.removeFromLeft(6);
        tunerComponent->setBounds(left);
        spectrogramComponent->setBounds(workspaceBounds);
    }
    else if (hasTuner)
    {
        tunerComponent->setBounds(workspaceBounds);
    }
    else if (hasSpectrogram)
    {
        spectrogramComponent->setBounds(workspaceBounds);
    }
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &audioDeviceManager)
        updateMicrophoneLabel();
}

void MainComponent::showAudioSettings()
{
    if (showingAudioSettings)
        return;

    if (audioDeviceSelector == nullptr)
    {
        audioDeviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            audioDeviceManager, 1, 2, 0, 0, false, false, false, true);
        addChildComponent(*audioDeviceSelector);
    }

    showingAudioSettings = true;
    tunerToggle.setVisible(false);
    spectrogramToggle.setVisible(false);
    workspaceLabel.setVisible(false);
    settingsButton.setVisible(false);
    audioDeviceSelector->setVisible(true);
    settingsDoneButton.setVisible(true);
    resized();
    repaint();
}

void MainComponent::hideAudioSettings()
{
    showingAudioSettings = false;
    if (audioDeviceSelector != nullptr)
        audioDeviceSelector->setVisible(false);
    settingsDoneButton.setVisible(false);
    tunerToggle.setVisible(true);
    spectrogramToggle.setVisible(true);
    workspaceLabel.setVisible(true);
    settingsButton.setVisible(true);
    updateMicrophoneLabel();
    resized();
    repaint();
}

void MainComponent::updateMicrophoneLabel()
{
    auto* device = audioDeviceManager.getCurrentAudioDevice();
    if (device != nullptr && device->isOpen()
        && device->getActiveInputChannels().countNumberOfSetBits() > 0)
    {
        const auto setup = audioDeviceManager.getAudioDeviceSetup();
        const auto name = setup.inputDeviceName.isNotEmpty()
            ? setup.inputDeviceName
            : device->getName();
        microphoneLabel.setText("Microphone: " + name,
                                juce::dontSendNotification);
        audioErrorMessage.clear();
    }
    else
    {
        microphoneLabel.setText("Microphone: none selected",
                                juce::dontSendNotification);
    }
}

void MainComponent::toggleTuner()
{
    if (tunerToggle.getToggleState())
    {
        if (tunerComponent == nullptr)
        {
            tunerComponent = std::make_unique<TunerComponent>(audioDeviceManager);
            addAndMakeVisible(*tunerComponent);
        }
    }
    else
    {
        tunerComponent.reset();
    }

    updateToolVisibility();
}

void MainComponent::toggleSpectrogram()
{
    if (spectrogramToggle.getToggleState())
    {
        if (spectrogramComponent == nullptr)
        {
            spectrogramComponent =
                std::make_unique<SpectrogramComponent>(audioDeviceManager);
            addAndMakeVisible(*spectrogramComponent);
        }
    }
    else
    {
        spectrogramComponent.reset();
    }

    updateToolVisibility();
}

void MainComponent::updateToolVisibility()
{
    resized();
    repaint();
}
