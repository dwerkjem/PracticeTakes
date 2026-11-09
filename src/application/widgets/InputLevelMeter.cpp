#include "InputLevelMeter.h"

#include "../../platform/evaluation/OnsetDetector.h"
#include "../localisation/Localisation.h"
#include "../theme/AppLookAndFeel.h"
#include "../theme/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
// The scale's figures, every 20 dB.
constexpr double scaleStepDb = 20.0;

// Room the figures take, beside or under the bar.
constexpr float baseScaleDepth = 16.0f;
constexpr float baseScaleWidth = 30.0f;

// How far the threshold's line stands out past the bar's edges.
constexpr float lineOverhang = 4.0f;

// Drawn as a minus sign rather than a hyphen, as a figure below zero is.
[[nodiscard]] juce::String decibels(double db)
{
    const auto rounded = static_cast<int>(std::lround(db));
    return rounded < 0
               ? juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) + juce::String(-rounded)
               : juce::String(rounded);
}
} // namespace

InputLevelMeter::InputLevelMeter(
    AudioInputService& audioInputService,
    std::function<double()> thresholdDb,
    Listening howLong,
    Theme initialTheme)
    : audio(audioInputService), threshold(std::move(thresholdDb)), listening(howLong),
      theme(initialTheme), meter(evaluation::OnsetSettings{}.windowSize), scratch(4096, 0.0f)
{
    setTitle(l10n::text("settings.audio.input-level"));
    setDescription(l10n::text("level.meter.description"));
    if (listening == Listening::always)
    {
        startListening();
    }
    startTimerHz(refreshRateHz);
}

InputLevelMeter::~InputLevelMeter()
{
    stopTimer();
    stopListening();
}

void InputLevelMeter::setTheme(Theme newTheme)
{
    theme = newTheme;
    repaint();
}

void InputLevelMeter::setVertical(bool shouldBeVertical)
{
    vertical = shouldBeVertical;
    repaint();
}

void InputLevelMeter::setShowsScale(bool shouldShowScale)
{
    showsScale = shouldShowScale;
    repaint();
}

double InputLevelMeter::thresholdDb() const
{
    return threshold ? threshold() : evaluation::OnsetSettings::defaultMinimumLevelDb;
}

void InputLevelMeter::reset()
{
    if (listeningNow)
    {
        audio.discardPendingSamples(this);
    }
    meter.reset();
    repaint();
}

void InputLevelMeter::startListening()
{
    if (listeningNow)
    {
        return;
    }

    // The shell checked for a tool's stream before it opened the tool; Settings
    // asks for one on its own, and says so when there is none rather than
    // taking a slot that is not there.
    if (listening == Listening::whileShowing && audio.freeConsumerSlots() == 0)
    {
        unavailable = l10n::text("level.unavailable.no-stream");
        return;
    }

    listeningNow = true;
    meter.reset();
    // Tells this meter the input's state and rate at once.
    audio.addListener(this);
}

void InputLevelMeter::stopListening()
{
    if (!listeningNow)
    {
        return;
    }
    audio.removeListener(this);
    listeningNow = false;
    meter.reset();
}

void InputLevelMeter::audioInputAboutToStart(double sampleRate, int inputChannels)
{
    juce::ignoreUnused(inputChannels);
    meter.setSampleRate(sampleRate);
    audio.discardPendingSamples(this);
}

void InputLevelMeter::audioInputStopped()
{
    audio.discardPendingSamples(this);
    meter.reset();
}

void InputLevelMeter::audioInputStateChanged(AudioInputService::InputState state)
{
    switch (state)
    {
    case AudioInputService::InputState::disconnected:
        stateReason = l10n::text("level.unavailable.no-microphone");
        break;
    case AudioInputService::InputState::opening:
        stateReason = l10n::text("audio.input.opening");
        break;
    case AudioInputService::InputState::muted:
        stateReason = l10n::text("audio.input.muted");
        break;
    case AudioInputService::InputState::active:
    case AudioInputService::InputState::clipping:
        // Clipping has a level to show, the loudest there is.
        stateReason.clear();
        break;
    }
    if (!stateReason.isEmpty())
    {
        meter.reset();
    }
}

void InputLevelMeter::timerCallback()
{
    if (listening == Listening::whileShowing)
    {
        if (isShowing())
        {
            startListening();
        }
        else
        {
            stopListening();
            return;
        }
    }

    if (listeningNow)
    {
        unavailable = stateReason;
        while (const auto read = audio.readSamples(this, scratch.data(), scratch.size()))
        {
            meter.push(scratch.data(), read);
            if (read < scratch.size())
            {
                break;
            }
        }
    }

    if (onUpdate)
    {
        onUpdate();
    }
    repaint();
}

float InputLevelMeter::proportionFor(double db) noexcept
{
    return static_cast<float>(std::clamp((db - lowestDb) / (highestDb - lowestDb), 0.0, 1.0));
}

void InputLevelMeter::paint(juce::Graphics& graphics)
{
    // The figures' room grows with their text (global-text-size).
    const float textFactor = static_cast<float>(
        appearance::TextScale::factor(std::max(100, appearance::textScaleOf(*this))));
    const float scaleDepth = baseScaleDepth * textFactor;
    const float scaleWidth = baseScaleWidth * textFactor;

    const auto palette = tunerPaletteFor(theme);
    auto area = getLocalBounds().toFloat();

    // The figures take a strip under a horizontal bar, or beside a vertical
    // one, when there is room for both.
    juce::Rectangle<float> scaleArea;
    if (showsScale)
    {
        if (vertical && area.getWidth() > scaleWidth + 16.0f)
        {
            scaleArea = area.removeFromLeft(scaleWidth);
        }
        else if (!vertical && area.getHeight() > scaleDepth + 10.0f)
        {
            scaleArea = area.removeFromBottom(scaleDepth);
        }
    }

    const auto bar = vertical ? area.reduced(lineOverhang + 2.0f, 2.0f)
                              : area.reduced(2.0f, lineOverhang + 2.0f);
    if (bar.isEmpty())
    {
        return;
    }

    // A point `db` along the bar: rightwards, or upwards.
    const auto along = [&bar, this](double db)
    {
        const float proportion = proportionFor(db);
        return vertical ? bar.getBottom() - (proportion * bar.getHeight())
                        : bar.getX() + (proportion * bar.getWidth());
    };
    // The part of the bar from `fromDb` to `toDb`.
    const auto span = [&bar, &along, this](double fromDb, double toDb)
    {
        const float from = along(fromDb);
        const float to = along(toDb);
        return vertical ? juce::Rectangle<float>(
                              bar.getX(), to, bar.getWidth(), std::max(0.0f, from - to))
                        : juce::Rectangle<float>(
                              from, bar.getY(), std::max(0.0f, to - from), bar.getHeight());
    };

    const float corner = 3.0f;
    graphics.setColour(palette.control);
    graphics.fillRoundedRectangle(bar, corner);

    const double limit = thresholdDb();
    const auto level = meter.reading();

    if (unavailable.isEmpty())
    {
        // Under the line, the bar is the room: drawn quietly. Over it, it is
        // what a take hears: drawn in the colour of a note in tune. Near full
        // scale, a warning.
        juce::Graphics::ScopedSaveState clipped(graphics);
        juce::Path rounded;
        rounded.addRoundedRectangle(bar, corner);
        graphics.reduceClipRegion(rounded);

        graphics.setColour(palette.muted.withAlpha(0.55f));
        graphics.fillRect(span(lowestDb, std::min(level.levelDb, limit)));
        if (level.levelDb > limit)
        {
            graphics.setColour(palette.inTune);
            graphics.fillRect(span(limit, std::min(level.levelDb, nearClippingDb)));
        }
        if (level.levelDb > nearClippingDb)
        {
            graphics.setColour(intonationZoneColour(pitch::IntonationZone::outside));
            graphics.fillRect(span(nearClippingDb, level.levelDb));
        }

        // The peak: a thin mark where the level was loudest lately.
        if (level.peakDb > lowestDb)
        {
            const float peak = along(level.peakDb);
            graphics.setColour(palette.foreground.withAlpha(0.8f));
            if (vertical)
            {
                graphics.fillRect(bar.getX(), peak - 1.0f, bar.getWidth(), 2.0f);
            }
            else
            {
                graphics.fillRect(peak - 1.0f, bar.getY(), 2.0f, bar.getHeight());
            }
        }
    }

    graphics.setColour(palette.outline);
    graphics.drawRoundedRectangle(bar, corner, 1.0f);

    // The threshold: a line across the bar, standing out past both edges.
    {
        const float line = along(limit);
        graphics.setColour(palette.foreground);
        if (vertical)
        {
            graphics.fillRect(
                bar.getX() - lineOverhang, line - 1.0f, bar.getWidth() + (2.0f * lineOverhang),
                2.0f);
        }
        else
        {
            graphics.fillRect(
                line - 1.0f, bar.getY() - lineOverhang, 2.0f,
                bar.getHeight() + (2.0f * lineOverhang));
        }
    }

    if (!unavailable.isEmpty())
    {
        graphics.setColour(palette.muted);
        graphics.setFont(appearance::fontFor(*this, appearance::TextRole::body));
        graphics.drawFittedText(
            unavailable, bar.toNearestInt().reduced(6, 0), juce::Justification::centred, 2);
    }

    if (!scaleArea.isEmpty())
    {
        graphics.setColour(palette.muted);
        graphics.setFont(appearance::fontFor(*this, appearance::TextRole::axis));
        // Every 20 dB where there is room for the figures, every 40 or only
        // the ends where there is not, so they never run into each other.
        const float length = vertical ? bar.getHeight() : bar.getWidth();
        const float roomPerFigure = (vertical ? 16.0f : 34.0f) * textFactor;
        double stepDb = scaleStepDb;
        while (stepDb < (highestDb - lowestDb) &&
               length * static_cast<float>(stepDb / (highestDb - lowestDb)) < roomPerFigure)
        {
            stepDb *= 2.0;
        }
        const auto steps = static_cast<int>(std::lround((highestDb - lowestDb) / stepDb));
        for (int step = 0; step <= steps; ++step)
        {
            const double db = lowestDb + (stepDb * step);
            const float at = along(db);
            const auto text = decibels(db);
            if (vertical)
            {
                // Kept inside the strip, so the ends' figures are not cut off.
                const float top = std::clamp(
                    at - (7.0f * textFactor), scaleArea.getY(),
                    scaleArea.getBottom() - (14.0f * textFactor));
                graphics.drawText(
                    text,
                    juce::Rectangle<float>(
                        scaleArea.getX(), top, scaleWidth - 4.0f, 14.0f * textFactor),
                    juce::Justification::centredRight, false);
            }
            else
            {
                // The ends' figures sit inside the bar's ends, the rest
                // centred on their mark.
                const float width = 34.0f * textFactor;
                auto box = juce::Rectangle<float>(
                    at - (width / 2.0f), scaleArea.getY(), width, scaleArea.getHeight());
                auto justification = juce::Justification::centred;
                if (step == 0)
                {
                    box.setX(at);
                    justification = juce::Justification::centredLeft;
                }
                else if (step == steps)
                {
                    box.setX(at - width);
                    justification = juce::Justification::centredRight;
                }
                graphics.drawText(text, box, justification, false);
            }
        }
    }
}
