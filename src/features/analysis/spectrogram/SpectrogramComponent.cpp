#include "SpectrogramComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr std::array<double, 5> frequencyGridLines{100.0, 500.0, 1000.0, 5000.0, 10000.0};

} // namespace

//==============================================================================
SpectrogramComponent::SpectrogramComponent(AudioInputService& sharedAudioInputService)
    : audioInputService(sharedAudioInputService)
{
    setOpaque(true);
    spectrogramImage.clear(spectrogramImage.getBounds(), backgroundColour());

    audioInputService.addListener(this);
    startTimerHz(refreshRateHz);
}

SpectrogramComponent::~SpectrogramComponent()
{
    stopTimer();
    audioInputService.removeListener(this);
}

void SpectrogramComponent::resetToDefaults()
{
    audioInputService.discardPendingSamples(this);
    fftData.fill(0.0f);
    spectrogramImage.clear(spectrogramImage.getBounds(), backgroundColour());
    repaint();
}

//==============================================================================
// Appearance

void SpectrogramComponent::setTheme(Theme theme)
{
    if (currentTheme == theme)
    {
        return;
    }

    currentTheme = theme;

    // Old columns use colours from the previous theme, so clear the image when
    // the appearance changes instead of mixing light and dark palettes.
    spectrogramImage.clear(spectrogramImage.getBounds(), backgroundColour());
    repaint();
}

juce::Colour SpectrogramComponent::backgroundColour() const
{
    return spectrogramPaletteFor(currentTheme).background;
}

juce::Colour SpectrogramComponent::panelColour() const
{
    return spectrogramPaletteFor(currentTheme).panel;
}

juce::Colour SpectrogramComponent::mutedColour() const
{
    return spectrogramPaletteFor(currentTheme).muted;
}

juce::Colour SpectrogramComponent::outlineColour() const
{
    return spectrogramPaletteFor(currentTheme).outline;
}

//==============================================================================
// Audio capture

void SpectrogramComponent::audioInputAboutToStart(double sampleRate, int inputChannels)
{
    juce::ignoreUnused(inputChannels);
    currentSampleRate.store(sampleRate);
    audioInputService.discardPendingSamples(this);
}

void SpectrogramComponent::audioInputStopped()
{
    audioInputService.discardPendingSamples(this);
}

void SpectrogramComponent::audioInputStateChanged(AudioInputService::InputState state)
{
    audioInputService.discardPendingSamples(this);

    clipping = state == AudioInputService::InputState::clipping;

    switch (state)
    {
    case AudioInputService::InputState::disconnected:
        audioErrorMessage = "Microphone disconnected.";
        break;
    case AudioInputService::InputState::opening:
        audioErrorMessage = "Waiting for the microphone.";
        break;
    case AudioInputService::InputState::muted:
        audioErrorMessage = "Microphone muted.";
        break;
    case AudioInputService::InputState::clipping:
    case AudioInputService::InputState::active:
        // Clipping still has a signal behind it -- the loudest, most relevant
        // one there is -- so it is drawn like `active`, not like the three
        // above. `clipping` (set above) is what tells `paint()` to mark it.
        audioErrorMessage.clear();
        break;
    }

    repaint();
}

//==============================================================================
// FFT and image generation

void SpectrogramComponent::timerCallback()
{
    // Process every complete FFT frame that has accumulated since the previous
    // timer tick. Partial frames remain in the FIFO for the next tick.
    while (audioInputService.availableSamples(this) >= fftSize)
    {
        calculateNextColumn();
    }

    repaint(spectrogramBounds);
}

void SpectrogramComponent::calculateNextColumn()
{
    fftData.fill(0.0f);
    const auto samplesRead = audioInputService.readSamples(this, fftData.data(), fftSize);
    if (samplesRead != fftSize)
    {
        return;
    }

    // The Hann window reduces spectral leakage before the FFT.
    hannWindow.multiplyWithWindowingTable(fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());
    updateSpectrogramColumn();
}

void SpectrogramComponent::updateSpectrogramColumn()
{
    // Shift older data left by one pixel, leaving the rightmost column for the
    // newest FFT frame.
    spectrogramImage.moveImageSection(0, 0, 1, 0, imageWidth - 1, imageHeight);

    for (int imageRow = 0; imageRow < imageHeight; ++imageRow)
    {
        const auto frequency = frequencyForImageRow(imageRow);
        const auto fftBin = fftBinForFrequency(frequency);
        const auto magnitude =
            fftData[static_cast<std::size_t>(fftBin)] / static_cast<float>(fftSize);
        const auto decibels = juce::Decibels::gainToDecibels(magnitude, minimumAnalysisDecibels);
        const auto visibleLevel = juce::jmap(decibels, visibleDecibelFloor, 0.0f, 0.0f, 1.0f);

        spectrogramImage.setPixelAt(imageWidth - 1, imageRow, colourForLevel(visibleLevel));
    }
}

double SpectrogramComponent::maximumVisibleFrequency() const
{
    const auto nyquistFrequency = currentSampleRate.load() * 0.5;
    return std::min(maximumDisplayedFrequencyHz, nyquistFrequency);
}

double SpectrogramComponent::frequencyForImageRow(int imageRow) const
{
    // A logarithmic scale gives musical low and mid frequencies enough space
    // while still showing the upper spectrum.
    const auto verticalPosition =
        1.0 - static_cast<double>(imageRow) / static_cast<double>(imageHeight - 1);
    const auto maximumFrequency = maximumVisibleFrequency();

    return minimumDisplayedFrequencyHz *
           std::pow(maximumFrequency / minimumDisplayedFrequencyHz, verticalPosition);
}

int SpectrogramComponent::fftBinForFrequency(double frequency) const
{
    const auto sampleRate = currentSampleRate.load();
    if (sampleRate <= 0.0)
    {
        return 0;
    }

    const auto bin = static_cast<int>(frequency * fftSize / sampleRate);
    return juce::jlimit(0, fftSize / 2, bin);
}

juce::Colour SpectrogramComponent::colourForLevel(float level) const
{
    return spectrogramColourForLevel(currentTheme, level);
}

//==============================================================================
// Drawing

float SpectrogramComponent::yForFrequency(double frequency) const
{
    const auto maximumFrequency = maximumVisibleFrequency();
    const auto logarithmicPosition = std::log(frequency / minimumDisplayedFrequencyHz) /
                                     std::log(maximumFrequency / minimumDisplayedFrequencyHz);

    return juce::jmap(
        static_cast<float>(logarithmicPosition), 0.0f, 1.0f,
        static_cast<float>(spectrogramBounds.getBottom()),
        static_cast<float>(spectrogramBounds.getY()));
}

float SpectrogramComponent::xForRotatedFrequency(double frequency) const
{
    // The horizontal twin of yForFrequency, for the vertical (rotated) shape,
    // where drawRotatedSpectrogram puts frequency on the pane's X axis instead
    // of its Y: low frequencies to the left, matching that transform's own
    // corners rather than a second, independently-derived mapping that could
    // drift from it.
    const auto maximumFrequency = maximumVisibleFrequency();
    const auto logarithmicPosition = std::log(frequency / minimumDisplayedFrequencyHz) /
                                     std::log(maximumFrequency / minimumDisplayedFrequencyHz);

    return juce::jmap(
        static_cast<float>(logarithmicPosition), 0.0f, 1.0f,
        static_cast<float>(spectrogramBounds.getX()),
        static_cast<float>(spectrogramBounds.getRight()));
}

juce::String SpectrogramComponent::frequencyLabel(double frequency) const
{
    if (frequency < 1000.0)
    {
        return juce::String(static_cast<int>(frequency)) + " Hz";
    }

    // A decimal point earns its place only when it says something -- 1500 Hz
    // is legitimately "1.5 kHz", but 5000 and 10000 are not "5.0" and "10.0"
    // of anything, they are 5 and 10 with a zero bolted on. The grid's own
    // frequencies (1, 5, 10 kHz) all land on whole numbers, so this was
    // spending a character to report nothing -- one a narrow, one-glyph-per-
    // line label can least afford, and one that reads as a stray "-" at that
    // size in the compact grid rather than as the decimal point it is.
    const auto inKiloHertz = frequency / 1000.0;
    const auto isWholeNumber = std::abs(inKiloHertz - std::round(inKiloHertz)) < 0.001;
    const auto decimalPlaces = isWholeNumber ? 0 : 1;
    return juce::String(inKiloHertz, decimalPlaces) + " kHz";
}

void SpectrogramComponent::drawFrequencyGrid(
    juce::Graphics& graphics,
    compact::Shape shape,
    juce::Colour accentColour) const
{
    // Labelled at every shape, not only the full one: the label is what makes a
    // gridline a frequency rather than a decoration, and dropping it below the
    // threshold was trading the one thing the grid is for to save a few pixels.
    // Smaller everywhere but full size, since a compact pane has fewer of them
    // to spare.
    //
    // `accentColour` rather than always `mutedColour()`: while clipping, the
    // caller passes red, so the grid reads as part of the same warning as the
    // border rather than as ordinary, unconcerned chrome sitting next to it.
    graphics.setColour(accentColour);
    graphics.setFont(juce::FontOptions(shape == compact::Shape::full ? 11.0f : 9.0f));

    for (const auto frequency : frequencyGridLines)
    {
        if (frequency >= maximumVisibleFrequency())
        {
            continue;
        }

        if (shape == compact::Shape::vertical)
        {
            // Frequency runs left to right here (drawRotatedSpectrogram), so the
            // grid turns with it: a vertical line at the frequency's X.
            const auto x = static_cast<int>(xForRotatedFrequency(frequency));
            graphics.drawVerticalLine(
                x, static_cast<float>(spectrogramBounds.getY()),
                static_cast<float>(spectrogramBounds.getBottom()));

            // The label reads one character per line -- upright, not rotated,
            // stacked downward -- rather than as one line read left to right.
            // Adjacent lines on a log scale can sit closer together than even
            // "5.0 kHz" alone needs -- 5.0 kHz and 10.0 kHz are ~33px apart in
            // a 320px-wide pane -- so a horizontal label, and a two-line
            // value-over-unit one, both ran into their neighbour however they
            // were justified: nothing wider than a single glyph was ever going
            // to fit in that gap. One character wide, no line comes close to
            // needing the full gap regardless of how tightly two lines sit,
            // and the pane's height -- which a narrow pane still has plenty
            // of -- is what carries the rest of the label instead of its
            // scarce width.
            const auto label = frequencyLabel(frequency).removeCharacters(" ");

            constexpr int glyphWidth = 10;
            constexpr int lineHeight = 11;

            // Same edge case a one-line label would have, narrower: the
            // highest frequency's line sits close enough to the right edge
            // that starting the column there runs it past the panel's
            // rounded corner, so where there is not room it reads from the
            // line leftward instead.
            const auto roomToTheRight = spectrogramBounds.getRight() - (x + 2);
            const auto left = roomToTheRight >= glyphWidth ? x + 2 : x - 2 - glyphWidth;

            for (int index = 0; index < label.length(); ++index)
            {
                graphics.drawText(
                    label.substring(index, index + 1), left,
                    spectrogramBounds.getY() + 1 + index * lineHeight, glyphWidth, lineHeight,
                    juce::Justification::centred);
            }
        }
        else
        {
            const auto y = yForFrequency(frequency);
            graphics.drawHorizontalLine(
                static_cast<int>(y), static_cast<float>(spectrogramBounds.getX()),
                static_cast<float>(spectrogramBounds.getRight()));
            graphics.drawText(
                frequencyLabel(frequency), spectrogramBounds.getX() + 4, static_cast<int>(y) - 13,
                50, 12, juce::Justification::centredLeft);
        }
    }
}

void SpectrogramComponent::drawRotatedSpectrogram(
    juce::Graphics& graphics,
    juce::Rectangle<int> bounds) const
{
    // Time down the pane instead of across it, for a pane that is tall and thin.
    // A trace scrolling left to right through 180px is a second of history and a
    // smear; the same trace scrolling downward has the long axis to spend.
    //
    // Only the mapping onto the screen turns. The image is still generated with
    // time along its width and frequency up its height -- rotating the source
    // would mean rewriting the scroll in `updateSpectrogramColumn` and
    // re-deriving every column already in the history, to arrive at a picture
    // this transform gives exactly.
    //
    // Stated as corners rather than as an angle, because the thing being decided
    // is where the oldest column, the newest column, and the lowest frequency
    // each land. A rotation plus a translation encodes the same choice in a form
    // no reader can check. Worked through, it is a 90-degree clockwise turn of
    // the full view: time ran left to right and now runs top to bottom;
    // frequency ran low at the bottom and now runs low at the left.
    const auto area = bounds.toFloat();
    const auto imageRight = static_cast<float>(spectrogramImage.getWidth());
    const auto imageBottom = static_cast<float>(spectrogramImage.getHeight());

    const auto transform = juce::AffineTransform::fromTargetPoints(
        // Oldest column, highest frequency.
        0.0f, 0.0f, area.getRight(), area.getY(),
        // Newest column, highest frequency: time runs down, so the newest is at
        // the bottom, which is where the eye already looks for "now".
        imageRight, 0.0f, area.getRight(), area.getBottom(),
        // Oldest column, lowest frequency: low frequencies to the left.
        0.0f, imageBottom, area.getX(), area.getY());

    juce::Graphics::ScopedSaveState saved(graphics);
    graphics.reduceClipRegion(bounds);
    graphics.drawImageTransformed(spectrogramImage, transform);
}

void SpectrogramComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(backgroundColour());

    const auto shape = compact::shapeFor(getWidth(), getHeight());
    const auto compactPane = shape != compact::Shape::full;

    graphics.setColour(panelColour());
    graphics.fillRoundedRectangle(spectrogramBounds.toFloat(), 8.0f);

    if (audioErrorMessage.isEmpty())
    {
        if (shape == compact::Shape::vertical)
        {
            drawRotatedSpectrogram(graphics, spectrogramBounds);
        }
        else
        {
            // Short and wide keeps the orientation it already had. The pane is
            // short of height, which is the axis the plot spends on frequency,
            // and turning it would put time on the short axis instead -- the
            // trade the vertical case exists to avoid, made backwards.
            graphics.drawImage(
                spectrogramImage, spectrogramBounds.toFloat(),
                juce::RectanglePlacement::stretchToFit);
        }
    }
    else
    {
        graphics.setColour(mutedColour());
        graphics.setFont(juce::FontOptions(compactPane ? 12.0f : 17.0f));
        graphics.drawFittedText(
            audioErrorMessage, spectrogramBounds.reduced(compactPane ? 4 : 20),
            juce::Justification::centred, compactPane ? 3 : 2);
    }

    // Clipping is shown by tinting the accents red -- the border and the
    // frequency grid, both already drawn regardless of state -- rather than by
    // covering any part of the plot. The plot is the one thing worth seeing
    // most while it is happening, so nothing here may obscure it: a distorted,
    // too-loud signal is still a real one, and the top bar's own "Mic clipping"
    // indicator already says so in words for anyone who wants the sentence.
    const auto accentColour = clipping ? juce::Colours::red : outlineColour();
    graphics.setColour(accentColour);
    graphics.drawRoundedRectangle(spectrogramBounds.toFloat(), 8.0f, clipping ? 3.0f : 1.0f);

    // Labelled at every shape now (see drawFrequencyGrid) -- the grid used to
    // be dropped below the compact threshold, which threw away the one thing
    // that makes it a frequency axis rather than a decoration next to the plot.
    drawFrequencyGrid(graphics, shape, clipping ? juce::Colours::red : mutedColour());
}

void SpectrogramComponent::resized()
{
    // The 18px frame is comfortable on a full pane and is most of a small one:
    // at the 180px floor it would spend a fifth of the width on margin. The plot
    // is the tool, so below the threshold the frame gives way to it.
    const auto inset = compact::isCompact(getWidth(), getHeight()) ? 4 : 18;
    spectrogramBounds = getLocalBounds().reduced(inset);
}
