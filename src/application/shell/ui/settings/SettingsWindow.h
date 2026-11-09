#pragma once

#include "../../../localisation/Localisation.h"
#include "../../../theme/PlusOrMinusCents.h"
#include "../../../widgets/InputLevelMeter.h"
#include "../../MainComponent.h"
#include "../ShellWindowTitles.h"

#include "../../../../platform/midi/NoteSoundingTracker.h"
#include "../../../../platform/take/TakeRecorder.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

// Settings remains separate from the main window so it can grow without
// crowding the top-level application shell.
class MainComponent::SettingsWindow final : public juce::DocumentWindow
{
  public:
    class Content final : public juce::Component
    {
      private:
        class CategoryPanel : public juce::Component
        {
          public:
            void paint(juce::Graphics& graphics) override
            {
                graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
            }

            void lookAndFeelChanged() override
            {
                // Above 100 percent a description wraps rather than squeezing
                // its letters onto one line; at 100 it is drawn as it always
                // was.
                const bool larger =
                    appearance::textScaleOf(*this) > appearance::TextScale::defaultPercent;
                for (auto* label : descriptions)
                {
                    label->setMinimumHorizontalScale(larger ? 1.0f : 0.0f);
                }
                resized();
            }

            // How tall the panel's rows are, as its last layout found: the
            // scrolling wrapper makes the panel this tall, so at a large text
            // size it scrolls rather than clipping (global-text-size).
            [[nodiscard]] int contentHeight() const noexcept
            {
                return measuredHeight;
            }

          protected:
            // A size laid out for 100 percent, grown with the text size.
            [[nodiscard]] int px(int base) const
            {
                return appearance::scaledFor(*this, base);
            }

            // The last row ends at `y`; a part below it that takes whatever
            // room is left wants at least `flexibleMinimum`.
            void contentEndsAt(int y, int flexibleMinimum = 0) noexcept
            {
                measuredHeight = y + flexibleMinimum + margin;
            }

            static constexpr int margin = 26;

            void configureHeading(juce::Label& heading, const juce::String& text)
            {
                heading.setText(text, juce::dontSendNotification);
                heading.setFont(
                    appearance::labelFont(appearance::TextRole::panelHeading, juce::Font::bold));
                addAndMakeVisible(heading);
            }

            void configureDescription(juce::Label& description, const juce::String& text)
            {
                description.setText(text, juce::dontSendNotification);
                description.setJustificationType(juce::Justification::topLeft);
                addAndMakeVisible(description);
                descriptions.push_back(&description);
            }

          private:
            int measuredHeight = 0;
            std::vector<juce::Label*> descriptions;
        };

        // A panel in the tab, scrolling when its rows are taller than the
        // window: at 150 percent the Practice panel is.
        class ScrollingPanel final : public juce::Component
        {
          public:
            explicit ScrollingPanel(CategoryPanel& shown) : panel(shown)
            {
                viewport.setViewedComponent(&panel, false);
                viewport.setScrollBarsShown(true, false);
                addAndMakeVisible(viewport);
            }

            void resized() override
            {
                viewport.setBounds(getLocalBounds());
                // Laid out tall first to find where its rows end, then made
                // that tall -- or as tall as the window, whichever is more.
                int width = getWidth();
                panel.setBounds(0, 0, width, measuringHeight);
                if (panel.contentHeight() > getHeight())
                {
                    width -= viewport.getScrollBarThickness();
                    panel.setBounds(0, 0, width, measuringHeight);
                }
                panel.setBounds(0, 0, width, std::max(getHeight(), panel.contentHeight()));
            }

            void lookAndFeelChanged() override
            {
                resized();
            }

          private:
            static constexpr int measuringHeight = 4000;
            CategoryPanel& panel;
            juce::Viewport viewport;
        };

        // Every text role at the chosen size, named, with its base size
        // beside it (global-text-size decision 2): what the text size changes,
        // shown rather than described.
        class TextSizePreview final : public juce::Component
        {
          public:
            TextSizePreview()
            {
                setTitle(l10n::text("settings.appearance.textsize.preview.title"));
            }

            // As tall as its rows at the current text size.
            [[nodiscard]] int preferredHeight() const
            {
                int height = 0;
                for (const auto& info : appearance::textRoles)
                {
                    height += rowHeight(info.role);
                }
                return height;
            }

            void paint(juce::Graphics& graphics) override
            {
                const int scale = appearance::textScaleOf(*this);
                const auto ink = findColour(juce::Label::textColourId);
                auto bounds = getLocalBounds();
                const int detailWidth = appearance::scaledHeight(150, scale);
                for (const auto& info : appearance::textRoles)
                {
                    auto row = bounds.removeFromTop(rowHeight(info.role));
                    auto detail = row.removeFromRight(detailWidth);

                    graphics.setColour(ink);
                    graphics.setFont(appearance::textFont(info.role, scale));
                    graphics.drawFittedText(
                        l10n::text(info.name), row, juce::Justification::centredLeft, 1, 0.8f);

                    const auto points = [](float size) { // Whole or half points only, so a half
                                                         // shows its one decimal.
                        const bool whole = std::lround(size * 2.0f) % 2 == 0;
                        return l10n::format(
                            "settings.appearance.textsize.points",
                            {{"size", l10n::number(size, whole ? 0 : 1)}});
                    };
                    juce::String sizes = points(info.baseSize);
                    if (scale != appearance::TextScale::defaultPercent)
                    {
                        sizes = l10n::format(
                            "settings.appearance.textsize.change",
                            {{"from", sizes},
                             {"to", points(appearance::textSize(info.role, scale))}});
                    }
                    graphics.setColour(ink.withAlpha(0.6f));
                    graphics.setFont(appearance::textFont(appearance::TextRole::small, scale));
                    graphics.drawFittedText(sizes, detail, juce::Justification::centredRight, 1);
                }
            }

          private:
            [[nodiscard]] int rowHeight(appearance::TextRole role) const
            {
                const float size = appearance::textSize(role, appearance::textScaleOf(*this));
                return std::max(20, static_cast<int>(std::ceil(size * 1.4f)) + 4);
            }
        };

        class AppearancePanel final : public CategoryPanel
        {
          public:
            AppearancePanel(
                Theme initialTheme,
                std::function<void(Theme)> appearanceHandler,
                int initialTextScale,
                std::function<void(int)> textScaleHandler)
                : onAppearanceChanged(std::move(appearanceHandler)),
                  onTextScaleChanged(std::move(textScaleHandler))
            {
                configureHeading(heading, l10n::text("settings.appearance.title"));
                configureDescription(description, l10n::text("settings.appearance.description"));

                themeLabel.setText(
                    l10n::text("settings.appearance.theme"), juce::dontSendNotification);
                addAndMakeVisible(themeLabel);

                themeBox.addItem(
                    l10n::text("settings.appearance.theme.light"), static_cast<int>(Theme::light));
                themeBox.addItem(
                    l10n::text("settings.appearance.theme.dark"), static_cast<int>(Theme::dark));
                themeBox.setSelectedId(static_cast<int>(initialTheme), juce::dontSendNotification);
                themeBox.onChange = [this]
                {
                    if (onAppearanceChanged)
                    {
                        onAppearanceChanged(static_cast<Theme>(themeBox.getSelectedId()));
                    }
                };
                addAndMakeVisible(themeBox);

                // One text size for the whole application (global-text-size):
                // menus, buttons, labels, tool text and readouts. The tuner's
                // note and the notation size themselves to their panes.
                textSizeLabel.setText(
                    l10n::text("settings.appearance.textsize"), juce::dontSendNotification);
                addAndMakeVisible(textSizeLabel);
                for (int percent = appearance::TextScale::minimumPercent;
                     percent <= appearance::TextScale::maximumPercent;
                     percent += appearance::TextScale::stepPercent)
                {
                    textSizeBox.addItem(juce::String(percent) + "%", percent);
                }
                textSizeBox.setComponentID("settings-text-size");
                textSizeBox.setTooltip(l10n::text("settings.appearance.textsize.tooltip"));
                textSizeBox.setSelectedId(initialTextScale, juce::dontSendNotification);
                textSizeBox.onChange = [this]
                {
                    if (onTextScaleChanged && textSizeBox.getSelectedId() > 0)
                    {
                        onTextScaleChanged(textSizeBox.getSelectedId());
                    }
                };
                addAndMakeVisible(textSizeBox);

                previewHeading.setText(
                    l10n::text("settings.appearance.textsize.preview"), juce::dontSendNotification);
                previewHeading.setFont(
                    appearance::labelFont(appearance::TextRole::body, juce::Font::bold));
                addAndMakeVisible(previewHeading);
                addAndMakeVisible(preview);
            }

            void lookAndFeelChanged() override
            {
                // Set elsewhere too -- an import, a reset -- so the box shows
                // what is in force.
                textSizeBox.setSelectedId(
                    appearance::textScaleOf(*this), juce::dontSendNotification);
                CategoryPanel::lookAndFeelChanged();
                preview.repaint();
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(px(34)));
                description.setBounds(bounds.removeFromTop(px(42)));
                bounds.removeFromTop(px(18));

                auto themeRow = bounds.removeFromTop(px(36));
                themeLabel.setBounds(themeRow.removeFromLeft(px(130)));
                themeBox.setBounds(themeRow.removeFromLeft(px(240)));
                bounds.removeFromTop(px(12));

                auto sizeRow = bounds.removeFromTop(px(36));
                textSizeLabel.setBounds(sizeRow.removeFromLeft(px(130)));
                textSizeBox.setBounds(sizeRow.removeFromLeft(px(240)));
                bounds.removeFromTop(px(18));

                previewHeading.setBounds(bounds.removeFromTop(px(24)));
                bounds.removeFromTop(px(6));
                preview.setBounds(bounds.removeFromTop(preview.preferredHeight())
                                      .withWidth(std::min(bounds.getWidth(), px(520))));
                contentEndsAt(bounds.getY());
            }

            void setTheme(Theme theme)
            {
                themeBox.setSelectedId(static_cast<int>(theme), juce::dontSendNotification);
            }

          private:
            juce::Label heading;
            juce::Label description;
            juce::Label themeLabel;
            juce::ComboBox themeBox;
            std::function<void(Theme)> onAppearanceChanged;
            std::function<void(int)> onTextScaleChanged;
            juce::Label textSizeLabel;
            juce::ComboBox textSizeBox;
            juce::Label previewHeading;
            TextSizePreview preview;
        };

        class AudioPanel final : public CategoryPanel, private juce::ChangeListener
        {
          public:
            AudioPanel(
                AudioInputService& inputService,
                transport::PracticeClock& practiceClock,
                std::function<void()> metronomeHandler)
                : audioInputService(inputService), clock(practiceClock),
                  onMetronomeChanged(std::move(metronomeHandler)),
                  // Up to two outputs, for the metronome. None is allowed:
                  // the clock and every analysis tool work without one.
                  deviceSelector(
                      inputService.deviceManager(),
                      1,
                      2,
                      0,
                      2,
                      false,
                      false,
                      false,
                      true)
            {
                configureHeading(heading, l10n::text("settings.audio.title"));
                configureDescription(description, l10n::text("settings.audio.description"));

                microphoneButton.onClick = [this] { audioInputService.toggleMuted(); };
                microphoneButton.setTitle(l10n::text("settings.audio.microphone.title"));
                addAndMakeVisible(microphoneButton);

                inputGainLabel.setText(
                    l10n::text("settings.audio.input-volume"), juce::dontSendNotification);
                addAndMakeVisible(inputGainLabel);
                inputGainSlider.setRange(0.0, 200.0, 1.0);
                inputGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
                inputGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
                inputGainSlider.setTextValueSuffix("%");
                inputGainSlider.setValue(
                    inputService.inputGain() * 100.0, juce::dontSendNotification);
                inputGainSlider.setTooltip(l10n::text("settings.audio.input-volume.tooltip"));
                // clang-format off
                inputGainSlider.onValueChange = [this] {
                    audioInputService.setInputGain(
                        static_cast<float>(inputGainSlider.getValue() / 100.0));
                };
                // clang-format on
                addAndMakeVisible(inputGainSlider);

                inputLevelLabel.setText(
                    l10n::text("settings.audio.input-level"), juce::dontSendNotification);
                addAndMakeVisible(inputLevelLabel);
                inputLevelMeter.setPercentageDisplay(false);
                inputLevelMeter.setTitle(l10n::text("settings.audio.input-level.title"));
                addAndMakeVisible(inputLevelMeter);

                bufferingStatusLabel.setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(bufferingStatusLabel);

                metronomeVolumeLabel.setText(
                    l10n::text("settings.audio.metronome-volume"), juce::dontSendNotification);
                addAndMakeVisible(metronomeVolumeLabel);
                metronomeVolumeSlider.setRange(0.0, 100.0, 1.0);
                metronomeVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
                metronomeVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
                metronomeVolumeSlider.setTextValueSuffix("%");
                metronomeVolumeSlider.setValue(
                    clock.metronomeVolume() * 100.0, juce::dontSendNotification);
                metronomeVolumeSlider.setTooltip(
                    l10n::text("settings.audio.metronome-volume.tooltip"));
                // clang-format off
                metronomeVolumeSlider.onValueChange = [this] {
                    clock.setMetronomeVolume(
                        static_cast<float>(metronomeVolumeSlider.getValue() / 100.0));
                };
                metronomeVolumeSlider.onDragEnd = [this] { notifyMetronomeChanged(); };
                // clang-format on
                addAndMakeVisible(metronomeVolumeSlider);

                accentToggle.setButtonText(l10n::text("settings.audio.accent"));
                accentToggle.setToggleState(clock.accentEnabled(), juce::dontSendNotification);
                accentToggle.onClick = [this]
                {
                    clock.setAccentEnabled(accentToggle.getToggleState());
                    notifyMetronomeChanged();
                };
                addAndMakeVisible(accentToggle);

                metronomeStatusLabel.setJustificationType(juce::Justification::centredLeft);
                addAndMakeVisible(metronomeStatusLabel);

                deviceViewport.setViewedComponent(&deviceSelector, false);
                deviceViewport.setScrollBarsShown(true, false);
                addAndMakeVisible(deviceViewport);

                audioInputService.addChangeListener(this);
                audioInputService.deviceManager().addChangeListener(this);
                updateMicrophoneControls();
            }

            ~AudioPanel() override
            {
                audioInputService.deviceManager().removeChangeListener(this);
                audioInputService.removeChangeListener(this);
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(px(34)));
                description.setBounds(bounds.removeFromTop(px(42)));
                bounds.removeFromTop(px(12));

                microphoneButton.setBounds(bounds.removeFromTop(px(36)).removeFromLeft(px(260)));
                bounds.removeFromTop(px(10));

                auto gainRow = bounds.removeFromTop(px(36));
                inputGainLabel.setBounds(gainRow.removeFromLeft(px(130)));
                inputGainSlider.setBounds(gainRow.removeFromLeft(px(360)));
                bounds.removeFromTop(px(8));

                auto levelRow = bounds.removeFromTop(px(24));
                inputLevelLabel.setBounds(levelRow.removeFromLeft(px(130)));
                inputLevelMeter.setBounds(levelRow.removeFromLeft(px(360)));
                bounds.removeFromTop(px(6));
                bufferingStatusLabel.setBounds(bounds.removeFromTop(px(26)));
                bounds.removeFromTop(px(8));

                auto metronomeRow = bounds.removeFromTop(px(36));
                metronomeVolumeLabel.setBounds(metronomeRow.removeFromLeft(px(130)));
                metronomeVolumeSlider.setBounds(metronomeRow.removeFromLeft(px(360)));
                accentToggle.setBounds(bounds.removeFromTop(px(28)).removeFromLeft(px(360)));
                metronomeStatusLabel.setBounds(bounds.removeFromTop(px(26)));
                bounds.removeFromTop(px(10));

                contentEndsAt(bounds.getY(), px(220));
                deviceViewport.setBounds(bounds);
                const auto selectorWidth = juce::jmax(
                    1, deviceViewport.getWidth() - deviceViewport.getScrollBarThickness());
                deviceSelector.setItemHeight(px(24));
                deviceSelector.setBounds(
                    0, 0, selectorWidth,
                    juce::jmax(deviceSelector.getHeight(), deviceViewport.getHeight()));
            }

          private:
            void changeListenerCallback(juce::ChangeBroadcaster* source) override
            {
                if (source == &audioInputService || source == &audioInputService.deviceManager())
                {
                    updateMicrophoneControls();
                }
            }

            void notifyMetronomeChanged()
            {
                if (onMetronomeChanged)
                {
                    onMetronomeChanged();
                }
            }

            // Where the click goes, or why there is nowhere for it to go.
            void updateMetronomeStatus()
            {
                const auto reason = audioInputService.outputUnavailableReason();
                if (reason.isNotEmpty())
                {
                    metronomeStatusLabel.setText(
                        l10n::format("settings.audio.metronome.unavailable", {{"reason", reason}}),
                        juce::dontSendNotification);
                    return;
                }

                juce::AudioDeviceManager::AudioDeviceSetup setup;
                audioInputService.deviceManager().getAudioDeviceSetup(setup);
                metronomeStatusLabel.setText(
                    setup.outputDeviceName.isNotEmpty()
                        ? l10n::format(
                              "settings.audio.metronome.device",
                              {{"device", setup.outputDeviceName}})
                        : l10n::text("settings.audio.metronome.default-device"),
                    juce::dontSendNotification);
            }

            void updateMicrophoneControls()
            {
                const auto isMuted = audioInputService.isMuted();
                microphoneButton.setButtonText(
                    isMuted ? l10n::text("settings.audio.microphone.unmute")
                            : l10n::text("settings.audio.microphone.mute"));
                microphoneButton.setTooltip(
                    isMuted ? l10n::text("settings.audio.microphone.unmute.tooltip")
                            : l10n::text("settings.audio.microphone.mute.tooltip"));
                inputGainSlider.setValue(
                    audioInputService.inputGain() * 100.0, juce::dontSendNotification);
                inputLevelProgress = audioInputService.inputLevel();
                inputLevelMeter.repaint();

                const auto droppedBlocks = audioInputService.droppedAnalysisBlocks();
                bufferingStatusLabel.setText(
                    droppedBlocks == 0 ? l10n::text("settings.audio.buffers.healthy")
                                       : l10n::plural(
                                             "settings.audio.buffers.dropped",
                                             static_cast<std::int64_t>(droppedBlocks)),
                    juce::dontSendNotification);
                updateMetronomeStatus();
            }

            juce::Label heading;
            juce::Label description;
            AudioInputService& audioInputService;
            transport::PracticeClock& clock;
            std::function<void()> onMetronomeChanged;
            juce::Label metronomeVolumeLabel;
            juce::Slider metronomeVolumeSlider;
            juce::ToggleButton accentToggle;
            juce::Label metronomeStatusLabel;
            juce::TextButton microphoneButton;
            juce::Label inputGainLabel;
            juce::Slider inputGainSlider;
            juce::Label inputLevelLabel;
            double inputLevelProgress = 0.0;
            juce::ProgressBar inputLevelMeter{inputLevelProgress};
            juce::Label bufferingStatusLabel;
            juce::AudioDeviceSelectorComponent deviceSelector;
            juce::Viewport deviceViewport;
        };

        // The one consumer of MidiInputService that exists.
        //
        // Deliberately small, and deliberately here rather than as a tool in
        // the catalog: a user whose keyboard is not working looks in settings,
        // and nothing else in the application can yet tell them anything. It is
        // also the only thing that exercises the whole path -- open, driver
        // callback, FIFO, drain -- and shipping the service without one is the
        // mistake #187 documented the cost of.
        class MidiPanel final
            : public CategoryPanel,
              private MidiInputService::Consumer,
              private juce::Timer
        {
          public:
            explicit MidiPanel(
                MidiInputService& inputService,
                int initialPedalThreshold,
                std::function<void()> selectionHandler,
                std::function<void(int)> thresholdHandler)
                : midiInputService(inputService), onSelectionChanged(std::move(selectionHandler)),
                  onThresholdChanged(std::move(thresholdHandler))
            {
                tracker.setPedalThreshold(initialPedalThreshold);

                configureHeading(heading, l10n::text("settings.midi.title"));
                configureDescription(description, l10n::text("settings.midi.description"));

                deviceLabel.setText(l10n::text("settings.midi.device"), juce::dontSendNotification);
                addAndMakeVisible(deviceLabel);

                deviceBox.setTextWhenNothingSelected(l10n::text("settings.midi.device.prompt"));
                deviceBox.setTextWhenNoChoicesAvailable(
                    l10n::text("settings.midi.device.none-detected"));
                deviceBox.onChange = [this] { applySelection(); };
                addAndMakeVisible(deviceBox);

                refreshButton.setButtonText(l10n::text("settings.midi.rescan"));
                refreshButton.setTitle(l10n::text("settings.midi.rescan.title"));
                refreshButton.onClick = [this] { rebuildDeviceList(); };
                addAndMakeVisible(refreshButton);

                statusLabel.setJustificationType(juce::Justification::topLeft);
                addAndMakeVisible(statusLabel);

                activityLabel.setJustificationType(juce::Justification::topLeft);
                addAndMakeVisible(activityLabel);

                thresholdLabel.setText(
                    l10n::text("settings.midi.pedal-threshold"), juce::dontSendNotification);
                addAndMakeVisible(thresholdLabel);

                // A number the user can see, because the point of the setting is
                // that they can find out where the line is. 64 is the midpoint the
                // MIDI specification names; a keyboard whose pedal rests above
                // zero, or never quite reaches 127, is what this is for.
                thresholdSlider.setSliderStyle(juce::Slider::LinearHorizontal);
                thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 24);
                thresholdSlider.setRange(
                    midi::minimumPedalDownThreshold, midi::maximumPedalDownThreshold, 1.0);
                thresholdSlider.setValue(tracker.pedalThreshold(), juce::dontSendNotification);
                thresholdSlider.setTitle(l10n::text("settings.midi.pedal-threshold.tooltip"));
                thresholdSlider.onValueChange = [this]
                {
                    const int value = static_cast<int>(thresholdSlider.getValue());
                    tracker.setPedalThreshold(value);

                    if (onThresholdChanged)
                    {
                        onThresholdChanged(value);
                    }
                };
                addAndMakeVisible(thresholdSlider);

                notesLabel.setJustificationType(juce::Justification::topLeft);
                addAndMakeVisible(notesLabel);

                // Said in words, wherever pedal use is shown, so that nobody reads
                // the absence of a complaint as agreement with their score.
                pedalNoteLabel.setText(
                    l10n::text("settings.midi.pedal-note"), juce::dontSendNotification);
                pedalNoteLabel.setJustificationType(juce::Justification::topLeft);
                addAndMakeVisible(pedalNoteLabel);

                midiInputService.addConsumer(this);
                rebuildDeviceList();
                startTimerHz(refreshRateHz);
            }

            ~MidiPanel() override
            {
                stopTimer();
                midiInputService.removeConsumer(this);
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(px(34)));
                description.setBounds(bounds.removeFromTop(px(42)));
                bounds.removeFromTop(px(12));

                auto deviceRow = bounds.removeFromTop(px(36));
                deviceLabel.setBounds(deviceRow.removeFromLeft(px(130)));
                deviceBox.setBounds(deviceRow.removeFromLeft(px(320)));
                deviceRow.removeFromLeft(px(10));
                refreshButton.setBounds(deviceRow.removeFromLeft(px(110)));

                bounds.removeFromTop(px(14));
                statusLabel.setBounds(bounds.removeFromTop(px(30)));
                bounds.removeFromTop(px(6));
                activityLabel.setBounds(bounds.removeFromTop(px(84)));

                bounds.removeFromTop(px(10));
                auto thresholdRow = bounds.removeFromTop(px(30));
                thresholdLabel.setBounds(thresholdRow.removeFromLeft(px(170)));
                thresholdSlider.setBounds(thresholdRow.removeFromLeft(px(380)));

                bounds.removeFromTop(px(10));
                notesLabel.setBounds(bounds.removeFromTop(px(84)));
                bounds.removeFromTop(px(6));
                pedalNoteLabel.setBounds(bounds.removeFromTop(px(40)));
                contentEndsAt(bounds.getY());
            }

          private:
            static constexpr int refreshRateHz = 10;
            static constexpr int shownEventCount = 4;

            // Far above any device index's id (index + 2), so it can never
            // collide with a real entry.
            static constexpr int absentDeviceItemId = 1'000'000;

            void rebuildDeviceList()
            {
                devices = midiInputService.availableDevices();
                const juce::String selected = midiInputService.selectedDeviceIdentifier();

                deviceBox.clear(juce::dontSendNotification);

                // Id 1 is "none", so that choosing nothing is an option rather
                // than something only reachable by never having chosen.
                deviceBox.addItem(l10n::text("settings.midi.device.none"), 1);

                for (std::size_t index = 0; index < devices.size(); ++index)
                {
                    deviceBox.addItem(devices[index].name, static_cast<int>(index) + 2);
                }

                int selectedId = 1;

                for (std::size_t index = 0; index < devices.size(); ++index)
                {
                    if (devices[index].identifier == selected)
                    {
                        selectedId = static_cast<int>(index) + 2;
                    }
                }

                // The chosen keyboard is unplugged. Showing "No MIDI input" would
                // read as the choice having been cleared, when it is only waiting,
                // so it keeps its place under its name -- shown, not choosable.
                const juce::String selectedName = midiInputService.selectedDeviceName();

                if (selectedId == 1 && selected.isNotEmpty())
                {
                    deviceBox.addItem(
                        l10n::format(
                            "settings.midi.device.absent",
                            {{"device", selectedName.isNotEmpty() ? selectedName : selected}}),
                        absentDeviceItemId);
                    deviceBox.setItemEnabled(absentDeviceItemId, false);
                    selectedId = absentDeviceItemId;
                }

                deviceBox.setSelectedId(selectedId, juce::dontSendNotification);
                shownDeviceListGeneration = midiInputService.deviceListGeneration();
                updateStatus();
            }

            void applySelection()
            {
                const int id = deviceBox.getSelectedId();

                // The placeholder for an unplugged keyboard is not a device;
                // landing on it must not clear the choice it stands for.
                if (id == absentDeviceItemId)
                {
                    return;
                }

                const auto index = static_cast<std::size_t>(id - 2);

                if (id <= 1 || index >= devices.size())
                {
                    midiInputService.selectDevice({}, {});
                }
                else
                {
                    midiInputService.selectDevice(devices[index].identifier, devices[index].name);
                }

                recent.clear();
                recentNotes.clear();

                // Another device is another stream: nothing held on the old one
                // can be ended by the new one. Events it already pushed into this
                // consumer's FIFO are still queued, though, and would reach the
                // freshly invalidated tracker as notes the new keyboard cannot
                // release -- on a different device clock, so with nonsense
                // durations. Drop them first.
                midiInputService.discardPendingEvents(this);
                tracker.invalidate(lastEventSeconds, finishedScratch);
                finishedScratch.clear();
                lastState = midiInputService.connectionState();
                updateStatus();

                if (onSelectionChanged)
                {
                    onSelectionChanged();
                }
            }

            void updateStatus()
            {
                switch (midiInputService.connectionState())
                {
                case MidiInputService::ConnectionState::noDeviceSelected:
                    statusLabel.setText(
                        devices.empty() ? l10n::text("settings.midi.status.none-detected")
                                        : l10n::text("settings.midi.status.none-chosen"),
                        juce::dontSendNotification);
                    break;

                case MidiInputService::ConnectionState::selectedDeviceUnavailable:
                    // Named rather than described: the remembered name is kept
                    // for exactly this sentence, so that a user who has not
                    // switched their piano on yet reads which piano is meant.
                    statusLabel.setText(
                        l10n::format(
                            "settings.midi.status.unavailable",
                            {{"device", midiInputService.selectedDeviceName()}}),
                        juce::dontSendNotification);
                    break;

                case MidiInputService::ConnectionState::open:
                    statusLabel.setText(
                        l10n::format(
                            "settings.midi.status.connected",
                            {{"device", midiInputService.selectedDeviceName()}}),
                        juce::dontSendNotification);
                    break;
                }
            }

            void timerCallback() override
            {
                // A keyboard plugged in or out while this is open appears or
                // goes without pressing Rescan. The service hears the change at
                // once; this only notices that its list moved.
                if (midiInputService.deviceListGeneration() != shownDeviceListGeneration)
                {
                    rebuildDeviceList();
                }

                // The stream can no longer be trusted when the keyboard leaves or
                // when the FIFO dropped events (the oldest go first, and a note
                // off may be among them). Both end every held note as unmeasured
                // rather than leaving it held for ever. This runs before the batch
                // below is read: whatever was lost happened before it.
                const auto state = midiInputService.connectionState();
                const auto lost = midiInputService.lostEvents(this);

                if ((lastState == MidiInputService::ConnectionState::open &&
                     state != MidiInputService::ConnectionState::open) ||
                    lost > lastLostEvents)
                {
                    tracker.invalidate(lastEventSeconds, finishedScratch);
                }

                lastState = state;
                lastLostEvents = lost;

                std::array<midi::MidiEvent, 128> buffer{};
                const std::size_t count =
                    midiInputService.readEvents(this, buffer.data(), buffer.size());

                for (std::size_t index = 0; index < count; ++index)
                {
                    recent.insert(recent.begin(), describe(buffer[index]));
                    tracker.consume(buffer[index], finishedScratch);
                    lastEventSeconds = buffer[index].deviceTimestampSeconds;
                }

                for (const midi::SoundedNote& note : finishedScratch)
                {
                    recentNotes.insert(recentNotes.begin(), describeNote(note));
                }

                finishedScratch.clear();

                if (recent.size() > shownEventCount)
                {
                    recent.resize(shownEventCount);
                }

                if (recentNotes.size() > shownEventCount)
                {
                    recentNotes.resize(shownEventCount);
                }

                updateStatus();

                if (midiInputService.receivedEventCount() == 0)
                {
                    activityLabel.setText(
                        l10n::text("settings.midi.activity.empty"), juce::dontSendNotification);
                    notesLabel.setText(
                        l10n::text("settings.midi.notes.empty"), juce::dontSendNotification);

                    return;
                }

                juce::StringArray lines;

                for (const juce::String& line : recent)
                {
                    lines.add(line);
                }

                activityLabel.setText(lines.joinIntoString("\n"), juce::dontSendNotification);

                juce::StringArray noteLines;

                for (const juce::String& line : recentNotes)
                {
                    noteLines.add(line);
                }

                notesLabel.setText(
                    noteLines.isEmpty()
                        ? juce::String(l10n::text("settings.midi.notes.none-finished"))
                        : noteLines.joinIntoString("\n"),
                    juce::dontSendNotification);
            }

            [[nodiscard]] static juce::String seconds(double value)
            {
                return l10n::format("settings.midi.seconds", {{"seconds", l10n::number(value, 2)}});
            }

            // How long a finished note lasted, by finger and by sound.
            //
            // A note whose length is not known says so rather than showing a
            // number: an abandoned note was cut off by lost events or by the
            // keyboard leaving, and one released before the pedal was ever seen
            // might or might not have been held by it. Showing 0.00 s for either
            // would be a measurement that was never taken.
            [[nodiscard]] static juce::String describeNote(const midi::SoundedNote& note)
            {
                const juce::String number(note.noteNumber);

                if (note.end == midi::SoundingEnd::abandoned)
                {
                    return l10n::format("settings.midi.note.abandoned", {{"note", number}});
                }

                if (note.pedal == midi::PedalCertainty::unknown)
                {
                    return l10n::format(
                        "settings.midi.note.pedal-unknown",
                        {{"note", number}, {"key", seconds(note.fingerSeconds())}});
                }

                const auto held =
                    note.heldByPedal() ? l10n::key("settings.midi.note.held-by-pedal")
                                       : l10n::key("settings.midi.note.held-by-finger");
                return l10n::format(
                    held, {{"note", number},
                           {"key", seconds(note.fingerSeconds())},
                           {"sounding", seconds(note.soundingSeconds())}});
            }

            // Note NUMBERS, not names.
            //
            // Naming a played note properly needs a *spelling*, and a MIDI
            // number has none -- it cannot tell F sharp from G flat.
            // `score::spelledName` exists and is the right function, and it
            // cannot help here: it takes a `score::Pitch`, which is a spelling.
            // The only thing that turns a bare number into one is the tuner's
            // `noteNameForMidi`, which is private to that tool and picks a
            // spelling arbitrarily for exactly this reason.
            //
            // So this shows the number. A played note gets a name once
            // something knows what it was *meant* to be -- which is matching it
            // against a score (#42), not naming it in isolation.
            [[nodiscard]] static juce::String describe(const midi::MidiEvent& event)
            {
                switch (event.kind)
                {
                case midi::MidiEventKind::noteOn:
                    return l10n::format(
                        "settings.midi.event.note-on",
                        {{"note", juce::String(event.noteNumber)},
                         {"velocity", juce::String(event.velocity)},
                         {"channel", juce::String(event.channel)}});

                case midi::MidiEventKind::noteOff:
                    return l10n::format(
                        "settings.midi.event.note-off", {{"note", juce::String(event.noteNumber)},
                                                         {"channel", juce::String(event.channel)}});

                case midi::MidiEventKind::sustainPedal:
                    return l10n::format(
                        "settings.midi.event.pedal",
                        {{"position", juce::String(event.pedalPosition)},
                         {"channel", juce::String(event.channel)}});
                }

                return {};
            }

            MidiInputService& midiInputService;
            std::function<void()> onSelectionChanged;
            std::function<void(int)> onThresholdChanged;
            std::vector<MidiInputService::DeviceDescription> devices;
            std::vector<juce::String> recent;
            std::vector<juce::String> recentNotes;

            // Message-thread only, like the drain that feeds it.
            midi::NoteSoundingTracker tracker;
            std::vector<midi::SoundedNote> finishedScratch;
            std::uint64_t lastLostEvents = 0;
            double lastEventSeconds = 0.0;
            MidiInputService::ConnectionState lastState =
                MidiInputService::ConnectionState::noDeviceSelected;
            std::uint64_t shownDeviceListGeneration = 0;

            juce::Label heading;
            juce::Label description;
            juce::Label deviceLabel;
            juce::ComboBox deviceBox;
            juce::TextButton refreshButton;
            juce::Label statusLabel;
            juce::Label activityLabel;
            juce::Label thresholdLabel;
            juce::Slider thresholdSlider;
            juce::Label notesLabel;
            juce::Label pedalNoteLabel;
        };

        class PracticePanel final : public CategoryPanel
        {
          public:
            PracticePanel(
                std::function<void(AppDefaults::Preset)> presetHandler,
                transport::PracticeClock& practiceClock,
                pitch::ReferencePitch& applicationReferencePitch,
                pitch::CurrentTarget& applicationTarget,
                take::TakeRecorder& takeRecorder,
                AudioInputService& inputService,
                Theme initialTheme,
                std::function<void()> countInHandler)
                : onPreset(std::move(presetHandler)), clock(practiceClock),
                  reference(applicationReferencePitch), target(applicationTarget),
                  recorder(takeRecorder), onCountInChanged(std::move(countInHandler)),
                  minimumLevelMeter(
                      inputService,
                      [this] { return recorder.minimumLevelDb(); },
                      InputLevelMeter::Listening::whileShowing,
                      initialTheme)
            {
                configureHeading(heading, l10n::text("settings.practice.title"));
                configureDescription(description, l10n::text("settings.practice.description"));

                presetLabel.setText(
                    l10n::text("settings.practice.preset"), juce::dontSendNotification);
                addAndMakeVisible(presetLabel);
                for (const AppDefaults::PresetDefinition& preset : AppDefaults::presets)
                {
                    presetBox.addItem(
                        juce::String(preset.label.data(), preset.label.size()),
                        static_cast<int>(preset.id));
                }
                presetBox.setTextWhenNothingSelected(l10n::text("settings.practice.preset.prompt"));
                presetBox.onChange = [this]
                {
                    if (onPreset)
                    {
                        onPreset(static_cast<AppDefaults::Preset>(presetBox.getSelectedId()));
                    }
                };
                addAndMakeVisible(presetBox);

                countInLabel.setText(
                    l10n::text("settings.practice.count-in"), juce::dontSendNotification);
                addAndMakeVisible(countInLabel);
                countInBox.addItem(l10n::text("settings.practice.count-in.none"), 1);
                for (int bars = 1; bars <= transport::maximumCountInBars; ++bars)
                {
                    countInBox.addItem(
                        l10n::plural("settings.practice.count-in.bars", bars), bars + 1);
                }
                countInBox.setSelectedId(clock.countInBars() + 1, juce::dontSendNotification);
                countInBox.setTooltip(l10n::text("settings.practice.count-in.tooltip"));
                countInBox.onChange = [this]
                {
                    clock.setCountInBars(countInBox.getSelectedId() - 1);
                    if (onCountInChanged)
                    {
                        onCountInChanged();
                    }
                };
                addAndMakeVisible(countInBox);

                // The application's A4 (system-wide-target-note decision 1).
                referenceLabel.setText(
                    l10n::text("settings.practice.reference"), juce::dontSendNotification);
                addAndMakeVisible(referenceLabel);
                referenceSlider.setComponentID("settings-reference-pitch");
                referenceSlider.setSliderStyle(juce::Slider::IncDecButtons);
                referenceSlider.setRange(pitch::minimumReferenceHz, pitch::maximumReferenceHz, 1.0);
                referenceSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, 26);
                referenceSlider.setTextValueSuffix(
                    l10n::text("settings.practice.reference.suffix"));
                referenceSlider.setValue(reference.hertz(), juce::dontSendNotification);
                referenceSlider.setTooltip(l10n::text("settings.practice.reference.tooltip"));
                referenceSlider.onValueChange = [this]
                {
                    reference.set(referenceSlider.getValue());
                    if (onCountInChanged)
                    {
                        onCountInChanged();
                    }
                };
                addAndMakeVisible(referenceSlider);

                // The three zone limits, each inside the next (sung-pitch-judging
                // decision 7). Each slider has its own fixed range; after every
                // change pitch::orderedLimits puts the three back in order and
                // the sliders show the result, so the bands cannot cross. In
                // tune within wins: moving it pushes the others aside, and the
                // others stop at it.
                nearPerfectLabel.setText(
                    l10n::text("settings.practice.near-perfect"), juce::dontSendNotification);
                addAndMakeVisible(nearPerfectLabel);
                nearPerfectSlider.setComponentID("settings-near-perfect-cents");
                nearPerfectSlider.setSliderStyle(juce::Slider::IncDecButtons);
                nearPerfectSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, 26);
                showAsPlusOrMinusCents(nearPerfectSlider);
                nearPerfectSlider.setTooltip(l10n::text("settings.practice.near-perfect.tooltip"));
                nearPerfectSlider.onValueChange = [this] { applyZoneLimits(); };
                addAndMakeVisible(nearPerfectSlider);

                toleranceLabel.setText(
                    l10n::text("settings.practice.in-tune"), juce::dontSendNotification);
                addAndMakeVisible(toleranceLabel);
                toleranceSlider.setComponentID("settings-in-tune-cents");
                toleranceSlider.setSliderStyle(juce::Slider::IncDecButtons);
                toleranceSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, 26);
                showAsPlusOrMinusCents(toleranceSlider);
                toleranceSlider.setTooltip(l10n::text("settings.practice.in-tune.tooltip"));
                toleranceSlider.onValueChange = [this] { applyZoneLimits(); };
                addAndMakeVisible(toleranceSlider);

                outOfTuneLabel.setText(
                    l10n::text("settings.practice.out-of-tune"), juce::dontSendNotification);
                addAndMakeVisible(outOfTuneLabel);
                outOfTuneSlider.setComponentID("settings-out-of-tune-cents");
                outOfTuneSlider.setSliderStyle(juce::Slider::IncDecButtons);
                outOfTuneSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, 26);
                showAsPlusOrMinusCents(outOfTuneSlider);
                outOfTuneSlider.setTooltip(l10n::text("settings.practice.out-of-tune.tooltip"));
                outOfTuneSlider.onValueChange = [this] { applyZoneLimits(); };
                addAndMakeVisible(outOfTuneSlider);

                // The quietest sound a microphone take counts (#349): below
                // it the room is never an attack, so never an extra note.
                minimumLevelLabel.setText(
                    l10n::text("settings.practice.minimum-level"), juce::dontSendNotification);
                addAndMakeVisible(minimumLevelLabel);
                minimumLevelSlider.setComponentID("settings-minimum-level");
                minimumLevelSlider.setSliderStyle(juce::Slider::IncDecButtons);
                minimumLevelSlider.setRange(
                    AppDefaults::lowestMinimumLevelDb, AppDefaults::highestMinimumLevelDb, 1.0);
                minimumLevelSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 90, 26);
                minimumLevelSlider.setTextValueSuffix(
                    l10n::text("settings.practice.minimum-level.suffix"));
                minimumLevelSlider.setValue(recorder.minimumLevelDb(), juce::dontSendNotification);
                minimumLevelSlider.setTooltip(
                    l10n::text("settings.practice.minimum-level.tooltip"));
                minimumLevelSlider.onValueChange = [this]
                {
                    recorder.setMinimumLevelDb(minimumLevelSlider.getValue());
                    if (onCountInChanged)
                    {
                        onCountInChanged();
                    }
                };
                addAndMakeVisible(minimumLevelSlider);

                // Beside it, the microphone's level with a line at the
                // threshold, so it can be set against this room (#381).
                minimumLevelMeter.setComponentID("settings-minimum-level-meter");
                minimumLevelMeter.setTooltip(
                    l10n::text("settings.practice.minimum-level.meter.tooltip"));
                addAndMakeVisible(minimumLevelMeter);

                nearPerfectSlider.setRange(
                    pitch::minimumNearPerfectCents, pitch::maximumNearPerfectCents, 1.0);
                toleranceSlider.setRange(
                    pitch::minimumToleranceCents, pitch::maximumToleranceCents, 1.0);
                outOfTuneSlider.setRange(
                    pitch::minimumOutOfTuneCents, pitch::maximumOutOfTuneCents, 1.0);
                showZoneLimits(recorder.intonationSettings().zoneLimits());

                // Whether opening a score replaces a target the player chose
                // (target-note-picker decision 4).
                keepTargetToggle.setComponentID("settings-keep-target");
                keepTargetToggle.setToggleState(
                    target.keepChosenWhenScoreOpens, juce::dontSendNotification);
                keepTargetToggle.setTooltip(l10n::text("settings.practice.keep-target.tooltip"));
                keepTargetToggle.onClick = [this]
                {
                    target.keepChosenWhenScoreOpens = keepTargetToggle.getToggleState();
                    if (onCountInChanged)
                    {
                        onCountInChanged();
                    }
                };
                addAndMakeVisible(keepTargetToggle);

                // Whether a score's target follows the note being sung while
                // the clock plays (now-and-next decision 5).
                followToggle.setComponentID("settings-target-follows-score");
                followToggle.setToggleState(
                    target.followScoreWhilePlaying, juce::dontSendNotification);
                followToggle.setTooltip(l10n::text("settings.practice.target-follows.tooltip"));
                followToggle.onClick = [this]
                {
                    target.followScoreWhilePlaying = followToggle.getToggleState();
                    if (onCountInChanged)
                    {
                        onCountInChanged();
                    }
                };
                addAndMakeVisible(followToggle);

                // Record pressed during playback (#361): on from here, or a
                // count-in first, which steps back a bar.
                countInWhenPlayingToggle.setComponentID("settings-count-in-when-playing");
                countInWhenPlayingToggle.setToggleState(
                    recorder.countInWhenPlaying(), juce::dontSendNotification);
                countInWhenPlayingToggle.setTooltip(
                    l10n::text("settings.practice.count-in-when-playing.tooltip"));
                countInWhenPlayingToggle.onClick = [this]
                {
                    recorder.setCountInWhenPlaying(countInWhenPlayingToggle.getToggleState());
                    if (onCountInChanged)
                    {
                        onCountInChanged();
                    }
                };
                addAndMakeVisible(countInWhenPlayingToggle);
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(px(34)));
                description.setBounds(bounds.removeFromTop(px(48)));
                bounds.removeFromTop(px(18));

                auto presetRow = bounds.removeFromTop(px(36));
                presetLabel.setBounds(presetRow.removeFromLeft(px(150)));
                presetBox.setBounds(presetRow.removeFromLeft(px(280)));
                bounds.removeFromTop(px(12));

                auto countInRow = bounds.removeFromTop(px(36));
                countInLabel.setBounds(countInRow.removeFromLeft(px(150)));
                countInBox.setBounds(countInRow.removeFromLeft(px(280)));
                bounds.removeFromTop(px(12));

                auto referenceRow = bounds.removeFromTop(px(36));
                referenceLabel.setBounds(referenceRow.removeFromLeft(px(150)));
                referenceSlider.setBounds(referenceRow.removeFromLeft(px(180)));
                bounds.removeFromTop(px(12));

                auto nearPerfectRow = bounds.removeFromTop(px(36));
                nearPerfectLabel.setBounds(nearPerfectRow.removeFromLeft(px(150)));
                nearPerfectSlider.setBounds(nearPerfectRow.removeFromLeft(px(180)));
                bounds.removeFromTop(px(12));

                auto toleranceRow = bounds.removeFromTop(px(36));
                toleranceLabel.setBounds(toleranceRow.removeFromLeft(px(150)));
                toleranceSlider.setBounds(toleranceRow.removeFromLeft(px(180)));
                bounds.removeFromTop(px(12));

                auto outOfTuneRow = bounds.removeFromTop(px(36));
                outOfTuneLabel.setBounds(outOfTuneRow.removeFromLeft(px(150)));
                outOfTuneSlider.setBounds(outOfTuneRow.removeFromLeft(px(180)));
                bounds.removeFromTop(px(12));

                // Taller than the rows above: the meter's figures go under its
                // bar.
                auto minimumLevelRow = bounds.removeFromTop(px(44));
                minimumLevelLabel.setBounds(
                    minimumLevelRow.removeFromLeft(px(150)).withSizeKeepingCentre(px(150), px(36)));
                minimumLevelSlider.setBounds(
                    minimumLevelRow.removeFromLeft(px(180)).withSizeKeepingCentre(px(180), px(36)));
                minimumLevelRow.removeFromLeft(px(16));
                minimumLevelMeter.setBounds(minimumLevelRow.removeFromLeft(px(320)));
                bounds.removeFromTop(px(10));
                keepTargetToggle.setBounds(bounds.removeFromTop(px(30)).withWidth(px(430)));
                bounds.removeFromTop(px(6));
                followToggle.setBounds(bounds.removeFromTop(px(30)).withWidth(px(430)));
                bounds.removeFromTop(px(6));
                countInWhenPlayingToggle.setBounds(bounds.removeFromTop(px(30)).withWidth(px(430)));
                contentEndsAt(bounds.getY());
            }

            void setTheme(Theme theme)
            {
                minimumLevelMeter.setTheme(theme);
            }

          private:
            // The three sliders to these limits, which are already in order.
            void showZoneLimits(pitch::ZoneLimits limits)
            {
                nearPerfectSlider.setValue(limits.nearPerfectCents, juce::dontSendNotification);
                toleranceSlider.setValue(limits.toleranceCents, juce::dontSendNotification);
                outOfTuneSlider.setValue(limits.outOfTuneCents, juce::dontSendNotification);
            }

            void applyZoneLimits()
            {
                const auto limits = pitch::orderedLimits(
                    {nearPerfectSlider.getValue(), toleranceSlider.getValue(),
                     outOfTuneSlider.getValue()});
                evaluation::IntonationSettings settings = recorder.intonationSettings();
                settings.nearPerfectCents = limits.nearPerfectCents;
                settings.toleranceCents = limits.toleranceCents;
                settings.outOfTuneCents = limits.outOfTuneCents;
                recorder.setIntonation(settings);
                showZoneLimits(limits);
                if (onCountInChanged)
                {
                    onCountInChanged();
                }
            }

            juce::Label heading;
            juce::Label description;
            juce::Label presetLabel;
            juce::ComboBox presetBox;
            std::function<void(AppDefaults::Preset)> onPreset;
            transport::PracticeClock& clock;
            pitch::ReferencePitch& reference;
            pitch::CurrentTarget& target;
            take::TakeRecorder& recorder;
            std::function<void()> onCountInChanged;
            juce::Label countInLabel;
            juce::ComboBox countInBox;
            juce::Label referenceLabel;
            juce::Slider referenceSlider;
            juce::Label nearPerfectLabel;
            juce::Slider nearPerfectSlider;
            juce::Label toleranceLabel;
            juce::Slider toleranceSlider;
            juce::Label outOfTuneLabel;
            juce::Slider outOfTuneSlider;
            juce::Label minimumLevelLabel;
            juce::Slider minimumLevelSlider;
            InputLevelMeter minimumLevelMeter;
            juce::ToggleButton keepTargetToggle{l10n::text("settings.practice.keep-target")};
            juce::ToggleButton followToggle{l10n::text("settings.practice.target-follows")};
            juce::ToggleButton countInWhenPlayingToggle{
                l10n::text("settings.practice.count-in-when-playing")};
        };

        class ResetPanel final : public CategoryPanel
        {
          public:
            ResetPanel(
                std::function<void()> feedbackHandler,
                std::function<void()> resetToolHandler,
                std::function<void()> resetAudioHandler,
                std::function<void()> resetLayoutHandler,
                std::function<void()> resetAllHandler)
            {
                configureHeading(heading, l10n::text("settings.reset.title"));
                configureDescription(description, l10n::text("settings.reset.description"));

                actionsHeading.setText(
                    l10n::text("settings.reset.support"), juce::dontSendNotification);
                actionsHeading.setFont(
                    appearance::labelFont(appearance::TextRole::menu, juce::Font::bold));
                addAndMakeVisible(actionsHeading);
                feedbackButton.setButtonText(l10n::text("settings.reset.feedback"));
                feedbackButton.setTitle(l10n::text("settings.reset.feedback.title"));
                feedbackButton.onClick = std::move(feedbackHandler);
                addAndMakeVisible(feedbackButton);

                resetHeading.setText(
                    l10n::text("settings.reset.heading"), juce::dontSendNotification);
                resetHeading.setFont(
                    appearance::labelFont(appearance::TextRole::menu, juce::Font::bold));
                addAndMakeVisible(resetHeading);
                configureResetButton(
                    resetToolButton, l10n::text("settings.reset.tool"),
                    l10n::text("settings.reset.tool.question"), std::move(resetToolHandler));
                configureResetButton(
                    resetAudioButton, l10n::text("settings.reset.audio"),
                    l10n::text("settings.reset.audio.question"), std::move(resetAudioHandler));
                configureResetButton(
                    resetLayoutButton, l10n::text("settings.reset.layout"),
                    l10n::text("settings.reset.layout.question"), std::move(resetLayoutHandler));
                configureResetButton(
                    resetAllButton, l10n::text("settings.reset.all"),
                    l10n::text("settings.reset.all.question"), std::move(resetAllHandler));
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(26);
                heading.setBounds(bounds.removeFromTop(px(34)));
                description.setBounds(bounds.removeFromTop(px(48)));
                bounds.removeFromTop(px(18));

                actionsHeading.setBounds(bounds.removeFromTop(px(28)));
                bounds.removeFromTop(px(8));
                feedbackButton.setBounds(bounds.removeFromTop(px(36)).removeFromLeft(px(190)));

                bounds.removeFromTop(px(28));
                resetHeading.setBounds(bounds.removeFromTop(px(28)));
                bounds.removeFromTop(px(8));
                auto firstRow = bounds.removeFromTop(px(36));
                resetToolButton.setBounds(firstRow.removeFromLeft(px(190)));
                firstRow.removeFromLeft(px(10));
                resetAudioButton.setBounds(firstRow.removeFromLeft(px(190)));
                bounds.removeFromTop(px(10));
                auto secondRow = bounds.removeFromTop(px(36));
                resetLayoutButton.setBounds(secondRow.removeFromLeft(px(190)));
                secondRow.removeFromLeft(px(10));
                resetAllButton.setBounds(secondRow.removeFromLeft(px(190)));
                contentEndsAt(bounds.getY());
            }

          private:
            void configureResetButton(
                juce::TextButton& button,
                const juce::String& text,
                const juce::String& question,
                std::function<void()> action)
            {
                button.setButtonText(text);
                button.onClick = [action = std::move(action), question]
                {
                    juce::AlertWindow::showOkCancelBox(
                        juce::MessageBoxIconType::WarningIcon,
                        l10n::text("settings.reset.confirm.title"), question,
                        l10n::text("settings.reset.confirm.reset"),
                        l10n::text("settings.reset.confirm.cancel"), nullptr,
                        juce::ModalCallbackFunction::create(
                            [action](int result)
                            {
                                if (result != 0 && action)
                                {
                                    action();
                                }
                            }));
                };
                addAndMakeVisible(button);
            }

            juce::Label heading;
            juce::Label description;
            juce::Label actionsHeading;
            juce::TextButton feedbackButton;
            juce::Label resetHeading;
            juce::TextButton resetToolButton;
            juce::TextButton resetAudioButton;
            juce::TextButton resetLayoutButton;
            juce::TextButton resetAllButton;
        };

      public:
        Content(
            AudioInputService& inputService,
            MidiInputService& midiService,
            transport::PracticeClock& practiceClock,
            pitch::ReferencePitch& referencePitch,
            pitch::CurrentTarget& currentTarget,
            take::TakeRecorder& takeRecorder,
            std::function<void()> clockSettingsHandler,
            int pedalDownThreshold,
            Theme initialTheme,
            std::function<void(Theme)> appearanceHandler,
            int initialTextScale,
            std::function<void(int)> textScaleHandler,
            std::function<void(AppDefaults::Preset)> presetHandler,
            std::function<void()> midiSelectionHandler,
            std::function<void(int)> pedalThresholdHandler,
            std::function<void()> saveHandler,
            std::function<void()> importHandler,
            std::function<void()> exportHandler,
            std::function<void()> feedbackHandler,
            std::function<void()> resetToolHandler,
            std::function<void()> resetAudioHandler,
            std::function<void()> resetLayoutHandler,
            std::function<void()> resetAllHandler)
            : appearancePanel(
                  initialTheme,
                  std::move(appearanceHandler),
                  initialTextScale,
                  std::move(textScaleHandler)),
              audioPanel(inputService, practiceClock, clockSettingsHandler),
              midiPanel(
                  midiService,
                  pedalDownThreshold,
                  std::move(midiSelectionHandler),
                  std::move(pedalThresholdHandler)),
              practicePanel(
                  std::move(presetHandler),
                  practiceClock,
                  referencePitch,
                  currentTarget,
                  takeRecorder,
                  inputService,
                  initialTheme,
                  std::move(clockSettingsHandler)),
              resetPanel(
                  std::move(feedbackHandler),
                  std::move(resetToolHandler),
                  std::move(resetAudioHandler),
                  std::move(resetLayoutHandler),
                  std::move(resetAllHandler))
        {
            categoryTabs.setTabBarDepth(42);
            // The test-control channel shows a panel by these identifiers, not
            // by its caption, which is translated.
            appearanceScroll.setComponentID("settings-panel-appearance");
            audioScroll.setComponentID("settings-panel-audio-device");
            midiScroll.setComponentID("settings-panel-midi");
            practiceScroll.setComponentID("settings-panel-practice");
            resetScroll.setComponentID("settings-panel-reset");
            categoryTabs.addTab(
                l10n::text("settings.appearance.title"), juce::Colours::transparentBlack,
                &appearanceScroll, false);
            categoryTabs.addTab(
                l10n::text("settings.audio.title"), juce::Colours::transparentBlack, &audioScroll,
                false);
            categoryTabs.addTab(
                l10n::text("settings.midi.title"), juce::Colours::transparentBlack, &midiScroll,
                false);
            categoryTabs.addTab(
                l10n::text("settings.practice.title"), juce::Colours::transparentBlack,
                &practiceScroll, false);
            categoryTabs.addTab(
                l10n::text("settings.reset.title"), juce::Colours::transparentBlack, &resetScroll,
                false);
            addAndMakeVisible(categoryTabs);

            saveButton.setButtonText(l10n::text("settings.save"));
            saveButton.setTitle(l10n::text("settings.save.title"));
            saveButton.onClick = std::move(saveHandler);
            addAndMakeVisible(saveButton);

            exportButton.setButtonText(l10n::text("settings.export"));
            exportButton.setTitle(l10n::text("settings.export.title"));
            exportButton.onClick = std::move(exportHandler);
            addAndMakeVisible(exportButton);

            importButton.setButtonText(l10n::text("settings.import"));
            importButton.setTitle(l10n::text("settings.import.title"));
            importButton.onClick = std::move(importHandler);
            addAndMakeVisible(importButton);
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            // Sizes for text at 100 percent, grown with the text size.
            const auto px = [this](int base) { return appearance::scaledFor(*this, base); };
            auto bounds = getLocalBounds().reduced(12);
            auto footer = bounds.removeFromBottom(px(48));
            categoryTabs.setTabBarDepth(px(42));
            categoryTabs.setBounds(bounds);
            footer.removeFromTop(10);
            saveButton.setBounds(footer.removeFromRight(px(180)));
            footer.removeFromRight(10);
            exportButton.setBounds(footer.removeFromRight(px(180)));
            footer.removeFromRight(10);
            importButton.setBounds(footer.removeFromRight(px(180)));
        }

        void lookAndFeelChanged() override
        {
            resized();
        }

        void setTheme(Theme theme)
        {
            appearancePanel.setTheme(theme);
            practicePanel.setTheme(theme);
            categoryTabs.sendLookAndFeelChange();
            repaint();
        }

        // Select a category tab by its identifier ("practice"), never by its
        // caption, which is translated. False when no tab matches, so a
        // caller naming a panel that has been renamed fails loudly rather
        // than silently leaving whichever tab happened to be showing.
        [[nodiscard]] bool showPanel(const juce::String& identifier)
        {
            for (int index = 0; index < categoryTabs.getNumTabs(); ++index)
            {
                const auto* panel = categoryTabs.getTabContentComponent(index);
                if (panel != nullptr && panel->getComponentID() == "settings-panel-" + identifier)
                {
                    categoryTabs.setCurrentTabIndex(index);

                    return true;
                }
            }

            return false;
        }

      private:
        AppearancePanel appearancePanel;
        AudioPanel audioPanel;
        MidiPanel midiPanel;
        PracticePanel practicePanel;
        ResetPanel resetPanel;
        ScrollingPanel appearanceScroll{appearancePanel};
        ScrollingPanel audioScroll{audioPanel};
        ScrollingPanel midiScroll{midiPanel};
        ScrollingPanel practiceScroll{practicePanel};
        ScrollingPanel resetScroll{resetPanel};
        juce::TextButton saveButton;
        juce::TextButton importButton;
        juce::TextButton exportButton;
        juce::TabbedComponent categoryTabs{juce::TabbedButtonBar::TabsAtTop};
    };

    SettingsWindow(
        AudioInputService& audioInputService,
        MidiInputService& midiInputService,
        transport::PracticeClock& practiceClock,
        pitch::ReferencePitch& referencePitch,
        pitch::CurrentTarget& currentTarget,
        take::TakeRecorder& takeRecorder,
        std::function<void()> clockSettingsHandler,
        int pedalDownThreshold,
        Theme initialTheme,
        std::function<void(Theme)> appearanceHandler,
        int initialTextScale,
        std::function<void(int)> textScaleHandler,
        std::function<void(AppDefaults::Preset)> presetHandler,
        std::function<void()> midiSelectionHandler,
        std::function<void(int)> pedalThresholdHandler,
        std::function<void()> saveHandler,
        std::function<void()> importHandler,
        std::function<void()> exportHandler,
        std::function<void()> feedbackHandler,
        std::function<void()> resetToolHandler,
        std::function<void()> resetAudioHandler,
        std::function<void()> resetLayoutHandler,
        std::function<void()> resetAllHandler,
        std::function<void()> closeHandler)
        : DocumentWindow(
              juce::String(shellwindows::settings.data(), shellwindows::settings.size()),
              juce::Colours::darkgrey,
              juce::DocumentWindow::allButtons),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(
            new Content(
                audioInputService, midiInputService, practiceClock, referencePitch, currentTarget,
                takeRecorder, std::move(clockSettingsHandler), pedalDownThreshold, initialTheme,
                std::move(appearanceHandler), initialTextScale, std::move(textScaleHandler),
                std::move(presetHandler), std::move(midiSelectionHandler),
                std::move(pedalThresholdHandler), std::move(saveHandler), std::move(importHandler),
                std::move(exportHandler), std::move(feedbackHandler), std::move(resetToolHandler),
                std::move(resetAudioHandler), std::move(resetLayoutHandler),
                std::move(resetAllHandler)),
            true);
        setResizable(true, true);
        setResizeLimits(760, 620, 1600, 1200);
        centreWithSize(900, 760);
        setVisible(true);
    }

    ~SettingsWindow() override
    {
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

    // Show a category panel by identifier ("practice"). Used by the
    // development-only test control channel so a manual-verification run can
    // be pointed at one panel rather than at whichever tab the window happens
    // to open on.
    [[nodiscard]] bool showPanel(const juce::String& identifier)
    {
        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        {
            return content->showPanel(identifier);
        }

        return false;
    }

    void applyAppearance(juce::LookAndFeel* appearance, juce::Colour background, Theme theme)
    {
        setLookAndFeel(appearance);
        setBackgroundColour(background);

        if (auto* content = dynamic_cast<Content*>(getContentComponent()))
        {
            content->setTheme(theme);
        }

        sendLookAndFeelChange();
        repaint();
    }

  private:
    std::function<void()> onClose;
};
