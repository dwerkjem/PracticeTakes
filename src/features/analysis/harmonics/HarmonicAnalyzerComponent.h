#pragma once

#include "../../../application/theme/Theme.h"
#include "../../../application/tools/ToolComponent.h"
#include "../../../platform/audio/AudioInputService.h"
#include "HarmonicAnalyzer.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>

class HarmonicAnalyzerComponent final
    : public ToolComponent,
      private AudioInputService::Listener,
      private juce::Timer
{
  public:
    explicit HarmonicAnalyzerComponent(AudioInputService& sharedAudioInputService);
    ~HarmonicAnalyzerComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void setTheme(Theme theme) override;
    void resetToDefaults() override;

  private:
    static constexpr int historyCapacity = 180;
    static constexpr int refreshRateHz = 20;

    void audioInputAboutToStart(double sampleRate, int inputChannels) override;
    void audioInputStopped() override;
    void audioInputStateChanged(AudioInputService::InputState state) override;
    void timerCallback() override;
    void appendHistory(const HarmonicAnalyzer::Result& result);
    [[nodiscard]] juce::Colour harmonicColour(int harmonic) const;

    AudioInputService& audioInputService;
    HarmonicAnalyzer analyzer;
    std::array<float, HarmonicAnalyzer::windowSize> samples{};
    std::array<std::array<float, HarmonicAnalyzer::harmonicCount>, historyCapacity>
        harmonicHistory{};
    std::array<float, historyCapacity> centroidHistory{};
    HarmonicAnalyzer::Result currentResult;
    std::atomic<double> currentSampleRate{44100.0};
    int historyWriteIndex = 0;
    int historyCount = 0;
    Theme currentTheme = Theme::light;
    juce::String audioErrorMessage;
    juce::Rectangle<int> currentBarsBounds;
    juce::Rectangle<int> historyBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicAnalyzerComponent)
};
