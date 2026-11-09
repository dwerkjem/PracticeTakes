#pragma once

#include <JuceHeader.h>

// A slider of cents either side of a note shows and reads its value as "±25 ¢":
// a distance each way, not a width centred on the note. Typing a bare number
// still works. One definition, so the Settings window and the tuner write it
// the same way.
inline void showAsPlusOrMinusCents(juce::Slider& slider)
{
    slider.textFromValueFunction = [](double value)
    {
        return juce::String::fromUTF8("\xC2\xB1") + juce::String(juce::roundToInt(value)) +
               juce::String::fromUTF8(" \xC2\xA2");
    };
    slider.valueFromTextFunction = [](const juce::String& text)
    { return text.retainCharacters("0123456789.").getDoubleValue(); };
    slider.updateText();
}
