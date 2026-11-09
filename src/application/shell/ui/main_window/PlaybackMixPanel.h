#pragma once

#include "../../../theme/AppLookAndFeel.h"

#include <JuceHeader.h>

#include "../../../../platform/transport/PracticeClock.h"

#include <functional>
#include <memory>
#include <vector>

// What the Mix button opens: whether the score plays, how loud, and Mute and
// Solo for each part, and each hand of a piano
// (openspec/changes/archive/2026-11-08-score-playback/design.md, decision 6).
//
// It holds no state: every control reads the practice clock when the panel
// opens and changes it at once. Play the score and the volume are saved, so
// they also tell the shell.
class PlaybackMixPanel final : public juce::Component
{
  public:
    static constexpr int panelWidth = 320;
    static constexpr int rowHeight = 28;
    static constexpr int maximumRowsShown = 10;

    // `panelLookAndFeel` is the transport's, which knows the text size: a call-out
    // opens on the desktop, outside the window whose look-and-feel it would
    // otherwise inherit (global-text-size).
    PlaybackMixPanel(
        transport::PracticeClock& practiceClock,
        std::function<void()> settingsHandler,
        juce::LookAndFeel* panelLookAndFeel = nullptr)
        : clock(practiceClock), onSettingsChanged(std::move(settingsHandler))
    {
        setLookAndFeel(panelLookAndFeel);
        setComponentID("playback-mix");
        const std::optional<std::string> unavailable = clock.whyPlaybackUnavailable();

        playButton.setComponentID("mix-play");
        playButton.setToggleState(clock.playbackMix().enabled, juce::dontSendNotification);
        playButton.setEnabled(!unavailable.has_value());
        playButton.onClick = [this]
        {
            clock.setPlaybackEnabled(playButton.getToggleState());
            saved();
        };
        addAndMakeVisible(playButton);

        volumeLabel.setText("Volume", juce::dontSendNotification);
        addAndMakeVisible(volumeLabel);
        volumeSlider.setComponentID("mix-volume");
        volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        volumeSlider.setRange(0.0, 1.0, 0.01);
        volumeSlider.setValue(clock.playbackMix().volume, juce::dontSendNotification);
        volumeSlider.setEnabled(!unavailable.has_value());
        volumeSlider.onDragEnd = [this] { saved(); };
        volumeSlider.onValueChange = [this]
        { clock.setPlaybackVolume(static_cast<float>(volumeSlider.getValue())); };
        addAndMakeVisible(volumeSlider);

        if (unavailable.has_value())
        {
            note.setText(juce::String(*unavailable) + ".", juce::dontSendNotification);
        }
        else if (clock.playbackTracks().empty())
        {
            note.setText("Open a score to hear it.", juce::dontSendNotification);
        }
        note.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(note);

        const auto& names = clock.playbackTrackNames();
        for (std::size_t track = 0; track < clock.playbackTracks().size(); ++track)
        {
            rows.push_back(
                std::make_unique<Row>(*this, track, track < names.size() ? names[track] : ""));
            list.addAndMakeVisible(*rows.back());
        }
        viewport.setViewedComponent(&list, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);

        const int shown = std::min(static_cast<int>(rows.size()), maximumRowsShown);
        const int noteHeight = note.getText().isEmpty() ? 0 : px(40);
        setSize(px(panelWidth), 12 + px(28) + px(32) + noteHeight + (shown * px(rowHeight)) + 8);
    }

    ~PlaybackMixPanel() override
    {
        setLookAndFeel(nullptr);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10, 6);
        playButton.setBounds(bounds.removeFromTop(px(28)));
        auto volume = bounds.removeFromTop(px(32));
        volumeLabel.setBounds(volume.removeFromLeft(px(64)));
        volumeSlider.setBounds(volume);
        if (note.getText().isNotEmpty())
        {
            note.setBounds(bounds.removeFromTop(px(40)));
        }
        viewport.setBounds(bounds);
        const int row = px(rowHeight);
        list.setSize(
            bounds.getWidth() -
                (rows.size() > maximumRowsShown ? viewport.getScrollBarThickness() : 0),
            static_cast<int>(rows.size()) * row);
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            rows[index]->setBounds(0, static_cast<int>(index) * row, list.getWidth(), row);
        }
    }

  private:
    // A size laid out for 100 percent, grown with the text size.
    [[nodiscard]] int px(int base) const
    {
        return appearance::scaledFor(*this, base);
    }

    // A track: its name, and Mute and Solo.
    class Row final : public juce::Component
    {
      public:
        Row(PlaybackMixPanel& owner, std::size_t trackIndex, const std::string& name)
            : panel(owner), track(trackIndex)
        {
            label.setText(juce::String::fromUTF8(name.c_str()), juce::dontSendNotification);
            addAndMakeVisible(label);
            for (auto* button : {&mute, &solo})
            {
                button->setClickingTogglesState(true);
                addAndMakeVisible(*button);
            }
            // On is unmistakable: amber for a muted track, green for a
            // soloed one.
            mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd28a1e));
            solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2e9e4f));
            for (auto* button : {&mute, &solo})
            {
                button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            }
            mute.setComponentID("mix-mute-" + juce::String(static_cast<int>(track)));
            solo.setComponentID("mix-solo-" + juce::String(static_cast<int>(track)));
            mute.setToggleState(
                panel.clock.playbackMix().isMuted(track), juce::dontSendNotification);
            solo.setToggleState(
                panel.clock.playbackMix().isSoloed(track), juce::dontSendNotification);
            mute.setTooltip("Silence this track");
            solo.setTooltip("Hear only the soloed tracks");
            mute.onClick = [this] { panel.clock.setTrackMuted(track, mute.getToggleState()); };
            solo.onClick = [this] { panel.clock.setTrackSoloed(track, solo.getToggleState()); };
            const bool usable = !panel.clock.whyPlaybackUnavailable().has_value();
            mute.setEnabled(usable);
            solo.setEnabled(usable);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(0, 2);
            solo.setBounds(bounds.removeFromRight(appearance::scaledFor(*this, 48)));
            bounds.removeFromRight(4);
            mute.setBounds(bounds.removeFromRight(appearance::scaledFor(*this, 52)));
            label.setBounds(bounds);
        }

      private:
        PlaybackMixPanel& panel;
        std::size_t track;
        juce::Label label;
        juce::TextButton mute{"Mute"};
        juce::TextButton solo{"Solo"};
    };

    void saved()
    {
        if (onSettingsChanged)
        {
            onSettingsChanged();
        }
    }

    transport::PracticeClock& clock;
    std::function<void()> onSettingsChanged;
    juce::ToggleButton playButton{"Play the score"};
    juce::Label volumeLabel;
    juce::Slider volumeSlider;
    juce::Label note;
    juce::Component list;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<Row>> rows;
};
