#include "HarmonicAnalyzerComponent.h"

#include <algorithm>

HarmonicAnalyzerComponent::HarmonicAnalyzerComponent(AudioInputService& sharedAudioInputService)
    : audioInputService(sharedAudioInputService)
{
    setOpaque(true);
    audioInputService.addListener(this);
    startTimerHz(refreshRateHz);
}

HarmonicAnalyzerComponent::~HarmonicAnalyzerComponent()
{
    stopTimer();
    audioInputService.removeListener(this);
}

void HarmonicAnalyzerComponent::setTheme(Theme theme)
{
    currentTheme = theme;
    repaint();
}

void HarmonicAnalyzerComponent::resetToDefaults()
{
    audioInputService.discardPendingSamples(this);
    currentResult = {};
    harmonicHistory = {};
    centroidHistory = {};
    historyWriteIndex = 0;
    historyCount = 0;
    repaint();
}

void HarmonicAnalyzerComponent::audioInputAboutToStart(double sampleRate, int inputChannels)
{
    juce::ignoreUnused(inputChannels);
    currentSampleRate.store(sampleRate);
    audioInputService.discardPendingSamples(this);
}

void HarmonicAnalyzerComponent::audioInputStopped()
{
    audioInputService.discardPendingSamples(this);
}

void HarmonicAnalyzerComponent::audioInputStateChanged(AudioInputService::InputState state)
{
    audioInputService.discardPendingSamples(this);
    switch (state)
    {
    case AudioInputService::InputState::disconnected:
        audioErrorMessage = "Microphone disconnected";
        break;
    case AudioInputService::InputState::opening:
        audioErrorMessage = "Waiting for the microphone";
        break;
    case AudioInputService::InputState::muted:
        audioErrorMessage = "Microphone muted";
        break;
    case AudioInputService::InputState::clipping:
        audioErrorMessage = "Microphone input is clipping";
        break;
    case AudioInputService::InputState::active:
        audioErrorMessage.clear();
        break;
    }
    repaint();
}

void HarmonicAnalyzerComponent::timerCallback()
{
    bool analyzedFrame = false;
    while (audioInputService.availableSamples(this) >= samples.size())
    {
        if (audioInputService.readSamples(this, samples.data(), samples.size()) != samples.size())
        {
            break;
        }
        currentResult = analyzer.analyze(samples, currentSampleRate.load());
        appendHistory(currentResult);
        analyzedFrame = true;
    }
    if (analyzedFrame)
    {
        repaint();
    }
}

void HarmonicAnalyzerComponent::appendHistory(const HarmonicAnalyzer::Result& result)
{
    harmonicHistory[static_cast<std::size_t>(historyWriteIndex)] = result.relativeAmplitudes;
    centroidHistory[static_cast<std::size_t>(historyWriteIndex)] =
        static_cast<float>(result.spectralCentroidHz);
    historyWriteIndex = (historyWriteIndex + 1) % historyCapacity;
    historyCount = std::min(historyCount + 1, historyCapacity);
}

juce::Colour HarmonicAnalyzerComponent::harmonicColour(int harmonic) const
{
    const auto saturation = isDarkTheme(currentTheme) ? 0.72f : 0.82f;
    const auto brightness = isDarkTheme(currentTheme) ? 0.92f : 0.70f;
    return juce::Colour::fromHSV(
        static_cast<float>(harmonic) / static_cast<float>(HarmonicAnalyzer::harmonicCount),
        saturation, brightness, 1.0f);
}

void HarmonicAnalyzerComponent::paint(juce::Graphics& graphics)
{
    const auto palette = spectrogramPaletteFor(currentTheme);
    const auto appPalette = appPaletteFor(currentTheme);
    graphics.fillAll(palette.background);

    auto bounds = getLocalBounds().reduced(18);
    auto status = bounds.removeFromTop(52);
    graphics.setColour(appPalette.foreground);
    graphics.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    const auto fundamentalText =
        currentResult.fundamentalHz > 0.0
            ? "Fundamental " + juce::String(currentResult.fundamentalHz, 1) + " Hz"
            : "Listening for a sustained pitch";
    graphics.drawText(fundamentalText, status.removeFromTop(28), juce::Justification::centredLeft);

    graphics.setColour(currentResult.isPitched ? palette.muted : juce::Colours::orange);
    graphics.setFont(juce::FontOptions(14.0f));
    juce::String detail;
    if (audioErrorMessage.isNotEmpty())
    {
        detail = audioErrorMessage;
    }
    else if (!currentResult.isPitched)
    {
        detail = "Uncertain or inharmonic input";
    }
    else if (currentResult.relativeAmplitudes[0] < 0.2f)
    {
        detail = "Weak fundamental - profile anchored to the strongest overtone";
    }
    else
    {
        detail = "Brightness " + juce::String(currentResult.spectralCentroidHz, 0) +
                 " Hz  |  confidence " + juce::String(currentResult.confidence * 100.0f, 0) + "%";
    }
    graphics.drawText(detail, status, juce::Justification::centredLeft);

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(currentBarsBounds.toFloat(), 8.0f);
    const auto barWidth =
        static_cast<float>(currentBarsBounds.getWidth()) / HarmonicAnalyzer::harmonicCount;
    for (int harmonic = 0; harmonic < HarmonicAnalyzer::harmonicCount; ++harmonic)
    {
        const auto amplitude = currentResult.relativeAmplitudes[static_cast<std::size_t>(harmonic)];
        const auto height = amplitude * static_cast<float>(currentBarsBounds.getHeight() - 30);
        const auto x = static_cast<float>(currentBarsBounds.getX()) +
                       static_cast<float>(harmonic) * barWidth + 5.0f;
        graphics.setColour(harmonicColour(harmonic));
        graphics.fillRoundedRectangle(
            x, static_cast<float>(currentBarsBounds.getBottom()) - 22.0f - height, barWidth - 10.0f,
            height, 3.0f);
        graphics.drawText(
            "H" + juce::String(harmonic + 1),
            juce::Rectangle<int>{
                static_cast<int>(x), currentBarsBounds.getBottom() - 21,
                static_cast<int>(barWidth - 10.0f), 18},
            juce::Justification::centred);
    }

    graphics.setColour(palette.panel);
    graphics.fillRoundedRectangle(historyBounds.toFloat(), 8.0f);
    graphics.setColour(palette.muted);
    graphics.drawText(
        "Relative harmonic amplitudes over time", historyBounds.reduced(8).removeFromTop(20),
        juce::Justification::centredLeft);
    if (historyCount < 2)
    {
        return;
    }

    const auto graph = historyBounds.reduced(10).withTrimmedTop(22);
    for (int harmonic = 0; harmonic < HarmonicAnalyzer::harmonicCount; ++harmonic)
    {
        juce::Path path;
        for (int offset = 0; offset < historyCount; ++offset)
        {
            const auto index =
                (historyWriteIndex - historyCount + offset + historyCapacity) % historyCapacity;
            const auto value = harmonicHistory[static_cast<std::size_t>(index)]
                                              [static_cast<std::size_t>(harmonic)];
            const auto x = juce::jmap(
                static_cast<float>(offset), 0.0f, static_cast<float>(historyCount - 1),
                static_cast<float>(graph.getX()), static_cast<float>(graph.getRight()));
            const auto y = juce::jmap(
                value, 0.0f, 1.0f, static_cast<float>(graph.getBottom()),
                static_cast<float>(graph.getY()));
            if (offset == 0)
            {
                path.startNewSubPath(x, y);
            }
            else
            {
                path.lineTo(x, y);
            }
        }
        graphics.setColour(harmonicColour(harmonic).withAlpha(0.85f));
        graphics.strokePath(path, juce::PathStrokeType(1.5f));
    }
}

void HarmonicAnalyzerComponent::resized()
{
    auto bounds = getLocalBounds().reduced(18);
    bounds.removeFromTop(60);
    currentBarsBounds = bounds.removeFromTop(std::min(230, bounds.getHeight() / 2));
    bounds.removeFromTop(10);
    historyBounds = bounds;
}
