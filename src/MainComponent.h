#pragma once

#include <JuceHeader.h>

#include "TunerComponent.h"

#include <memory>

class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void showHomePage();
    void showTuner();
    void setHomeControlsVisible(bool shouldBeVisible);

    juce::Label appTitle;
    juce::Label appSubtitle;
    juce::Label toolsHeading;
    juce::Label tunerTitle;
    juce::Label tunerDescription;
    juce::Label pageTitle;
    juce::TextButton tunerButton { "Open tuner" };
    juce::TextButton backButton { "Back to tools" };

    juce::Rectangle<int> tunerCardBounds;
    std::unique_ptr<TunerComponent> tunerComponent;
    bool showingTuner = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
