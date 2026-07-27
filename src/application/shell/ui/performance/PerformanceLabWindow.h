#pragma once

#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB

#include "../../../../features/performance/BenchmarkRecordStore.h"
#include "../../../../features/performance/PerformanceLabController.h"
#include "../../MainComponent.h"

#include <atomic>
#include <utility>

class MainComponent::PerformanceLabWindow final : public juce::DocumentWindow
{
  public:
    class Content final : public juce::Component
    {
      public:
        explicit Content(performance::PerformanceLabController& controllerIn)
            : controller(controllerIn), progressValue(0.0), progressBar(progressValue)
        {
            configureNavigation();
            configureConfiguration();
            configureActions();
            addAndMakeVisible(contentViewport);
            contentViewport.setViewedComponent(&content, false);
            applyConfiguration();
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(14);
            navigation.setBounds(bounds.removeFromTop(38));
            bounds.removeFromTop(10);
            contentViewport.setBounds(bounds);
            layoutCurrentView();
        }

        void refresh()
        {
            const auto& state = controller.state();
            configurationButton.setToggleState(
                state.view == performance::PerformanceLabView::configuration,
                juce::dontSendNotification);
            liveButton.setToggleState(
                state.view == performance::PerformanceLabView::liveRun, juce::dontSendNotification);
            historyButton.setToggleState(
                state.view == performance::PerformanceLabView::history, juce::dontSendNotification);
            resultsButton.setToggleState(
                state.view == performance::PerformanceLabView::results, juce::dontSendNotification);
            comparisonButton.setToggleState(
                state.view == performance::PerformanceLabView::comparison,
                juce::dontSendNotification);

            for (auto* component : viewComponents)
                component->setVisible(false);

            updateConfiguration(state);
            updateLive(state);
            updateHistory(state);
            updateResults(state);
            updateComparison(state);
            layoutCurrentView();
            content.repaint();
        }

      private:
        static juce::String phaseName(performance::RunnerPhase phase)
        {
            using enum performance::RunnerPhase;
            switch (phase)
            {
            case validating:
                return "Validating";
            case stabilizing:
                return "Stabilizing";
            case warmingUp:
                return "Warming up";
            case measuring:
                return "Measuring";
            case aggregating:
                return "Aggregating";
            case cleaningUp:
                return "Cleaning up";
            case completed:
                return "Completed";
            case idle:
                return "Idle";
            }
            return "Unknown";
        }

        static juce::String statusName(performance::RunStatus status)
        {
            using enum performance::RunStatus;
            switch (status)
            {
            case pending:
                return "Pending";
            case running:
                return "Running";
            case completed:
                return "Completed";
            case cancelled:
                return "Incomplete - cancelled";
            case failed:
                return "Incomplete - failed";
            }
            return "Unknown";
        }

        void addViewComponent(juce::Component& component)
        {
            content.addAndMakeVisible(component);
            viewComponents.push_back(&component);
        }

        void configureNavigation()
        {
            for (auto* button :
                 {&configurationButton, &liveButton, &historyButton, &resultsButton,
                  &comparisonButton})
            {
                button->setClickingTogglesState(false);
                navigation.addAndMakeVisible(button);
            }
            configurationButton.onClick = [this]
            {
                controller.showConfiguration();
                refresh();
            };
            historyButton.onClick = [this]
            {
                controller.showHistory();
                refresh();
            };
            resultsButton.onClick = [this]
            {
                const auto& state = controller.state();
                if (state.selectedResult)
                    (void)controller.selectResult(*state.selectedResult);
                refresh();
            };
            comparisonButton.onClick = [this]
            {
                const auto size = controller.state().history.size();
                if (size >= 2)
                    (void)controller.selectComparison(size - 2, size - 1);
                refresh();
            };
            liveButton.setEnabled(false);
        }

        void configureConfiguration()
        {
            heading.setText("Run configuration", juce::dontSendNotification);
            heading.setFont(juce::FontOptions(22.0f, juce::Font::bold));
            addViewComponent(heading);
            scenarioLabel.setText("Scenario", juce::dontSendNotification);
            strategyLabel.setText("Strategy", juce::dontSendNotification);
            warmUpLabel.setText("Warm-up runs", juce::dontSendNotification);
            trialsLabel.setText("Measured trials", juce::dontSendNotification);
            deviceLabel.setText("Audio device", juce::dontSendNotification);
            backendLabel.setText("Backend", juce::dontSendNotification);
            sampleRateLabel.setText("Sample rate", juce::dontSendNotification);
            bufferSizeLabel.setText("Buffer size", juce::dontSendNotification);
            for (auto* label :
                 {&scenarioLabel, &strategyLabel, &warmUpLabel, &trialsLabel, &deviceLabel,
                  &backendLabel, &sampleRateLabel, &bufferSizeLabel})
                addViewComponent(*label);

            scenarioBox.addItem("Sustained audio analysis", 1);
            scenarioBox.setSelectedId(1, juce::dontSendNotification);
            strategyBox.addItem("Production baseline", 1);
            strategyBox.addItem("Parameterized analysis fixture", 2);
            strategyBox.setSelectedId(1, juce::dontSendNotification);
            sampleRateBox.addItem("44100 Hz", 1);
            sampleRateBox.addItem("48000 Hz", 2);
            sampleRateBox.setSelectedId(2, juce::dontSendNotification);
            bufferSizeBox.addItem("128 frames", 1);
            bufferSizeBox.addItem("256 frames", 2);
            bufferSizeBox.addItem("512 frames", 3);
            bufferSizeBox.setSelectedId(2, juce::dontSendNotification);
            for (auto* box : {&scenarioBox, &strategyBox, &sampleRateBox, &bufferSizeBox})
                addViewComponent(*box);

            warmUpEditor.setInputRestrictions(3, "0123456789");
            warmUpEditor.setText("1", false);
            trialsEditor.setInputRestrictions(3, "0123456789");
            trialsEditor.setText("5", false);
            deviceEditor.setText("Current audio device", false);
            backendEditor.setText("Current backend", false);
            for (auto* editor : {&warmUpEditor, &trialsEditor, &deviceEditor, &backendEditor})
                addViewComponent(*editor);

            validationLabel.setJustificationType(juce::Justification::topLeft);
            validationLabel.setColour(juce::Label::textColourId, juce::Colours::orangered);
            addViewComponent(validationLabel);
            runButton.onClick = [this]
            {
                applyConfiguration();
                if (controller.startRun())
                    refresh();
            };
            addViewComponent(runButton);
        }

        void configureActions()
        {
            liveHeading.setFont(juce::FontOptions(22.0f, juce::Font::bold));
            liveHeading.setText("Live run", juce::dontSendNotification);
            liveStatus.setJustificationType(juce::Justification::topLeft);
            warningLabel.setJustificationType(juce::Justification::topLeft);
            cancelButton.onClick = [this]
            {
                controller.requestCancellation();
                refresh();
            };
            for (auto* component : std::initializer_list<juce::Component*>{
                     &liveHeading, &liveStatus, &progressBar, &warningLabel, &cancelButton})
                addViewComponent(*component);

            historyHeading.setText("Run history", juce::dontSendNotification);
            historyHeading.setFont(juce::FontOptions(22.0f, juce::Font::bold));
            historyTable.setMultiLine(true);
            historyTable.setReadOnly(true);
            historyTable.setScrollbarsShown(true);
            reopenButton.onClick = [this] { chooseReopenFile(); };
            for (auto* component : std::initializer_list<juce::Component*>{
                     &historyHeading, &historyTable, &reopenButton})
                addViewComponent(*component);

            resultsHeading.setText("Result details", juce::dontSendNotification);
            resultsHeading.setFont(juce::FontOptions(22.0f, juce::Font::bold));
            resultSummary.setMultiLine(true);
            resultSummary.setReadOnly(true);
            resultSummary.setScrollbarsShown(true);
            exportButton.onClick = [this] { chooseExportFile(); };
            for (auto* component : std::initializer_list<juce::Component*>{
                     &resultsHeading, &resultSummary, &exportButton})
                addViewComponent(*component);

            comparisonHeading.setText("Strategy comparison", juce::dontSendNotification);
            comparisonHeading.setFont(juce::FontOptions(22.0f, juce::Font::bold));
            comparisonTable.setMultiLine(true);
            comparisonTable.setReadOnly(true);
            comparisonTable.setScrollbarsShown(true);
            for (auto* component :
                 std::initializer_list<juce::Component*>{&comparisonHeading, &comparisonTable})
                addViewComponent(*component);
        }

        void applyConfiguration()
        {
            performance::RunConfiguration configuration;
            configuration.scenarioId = "sustained-audio-analysis";
            configuration.workloadId = "deterministic-harmonic-fixture";
            configuration.strategyId =
                strategyBox.getSelectedId() == 1 ? "baseline" : "parameterized-analysis";
            if (configuration.strategyId != "baseline")
                configuration.strategyParameters["window-size"] = "2048";
            configuration.warmUpCount =
                static_cast<std::uint32_t>(warmUpEditor.getText().getIntValue());
            configuration.measuredTrialCount =
                static_cast<std::uint32_t>(trialsEditor.getText().getIntValue());
            configuration.audio.deviceName = deviceEditor.getText().toStdString();
            configuration.audio.backend = backendEditor.getText().toStdString();
            configuration.audio.sampleRateHz =
                sampleRateBox.getSelectedId() == 1 ? 44100.0 : 48000.0;
            const std::uint32_t sizes[] = {128, 256, 512};
            configuration.audio.bufferSizeFrames = sizes[bufferSizeBox.getSelectedId() - 1];
            controller.setConfiguration(std::move(configuration));
            refresh();
        }

        void updateConfiguration(const performance::PerformanceLabState& state)
        {
            const auto visible = state.view == performance::PerformanceLabView::configuration;
            for (auto* component : std::initializer_list<juce::Component*>{
                     &heading, &scenarioLabel, &strategyLabel, &warmUpLabel, &trialsLabel,
                     &deviceLabel, &backendLabel, &sampleRateLabel, &bufferSizeLabel, &scenarioBox,
                     &strategyBox, &sampleRateBox, &bufferSizeBox, &warmUpEditor, &trialsEditor,
                     &deviceEditor, &backendEditor, &validationLabel, &runButton})
                component->setVisible(visible);

            juce::String errors;
            for (const auto& error : state.validation.errors)
                errors << juce::String(error) << "\n";
            validationLabel.setText(errors, juce::dontSendNotification);
            runButton.setEnabled(state.validation.valid && !state.running);
        }

        void updateLive(const performance::PerformanceLabState& state)
        {
            const auto visible = state.view == performance::PerformanceLabView::liveRun;
            for (auto* component : std::initializer_list<juce::Component*>{
                     &liveHeading, &liveStatus, &progressBar, &warningLabel, &cancelButton})
                component->setVisible(visible);
            progressValue = state.progress.totalTrials == 0
                                ? 0.0
                                : static_cast<double>(state.progress.completedTrials) /
                                      static_cast<double>(state.progress.totalTrials);
            liveStatus.setText(
                phaseName(state.progress.phase) + "\nTrial " +
                    juce::String(state.progress.completedTrials) + " of " +
                    juce::String(state.progress.totalTrials),
                juce::dontSendNotification);
            juce::String warnings;
            for (const auto& warning : state.safetyWarnings)
                warnings << juce::String(warning.message) << "\n";
            warningLabel.setText(warnings, juce::dontSendNotification);
            cancelButton.setEnabled(state.running && !state.cancellationRequested);
        }

        void updateHistory(const performance::PerformanceLabState& state)
        {
            const auto visible = state.view == performance::PerformanceLabView::history;
            historyHeading.setVisible(visible);
            historyTable.setVisible(visible);
            reopenButton.setVisible(visible);
            juce::String text;
            for (std::size_t index = 0; index < state.history.size(); ++index)
            {
                const auto& record = state.history[index];
                text << juce::String(static_cast<int>(index + 1)) << ". " << record.runId << " | "
                     << record.configuration.strategyId << " | " << statusName(record.status)
                     << "\n";
            }
            historyTable.setText(text, false);
        }

        void updateResults(const performance::PerformanceLabState& state)
        {
            const auto visible = state.view == performance::PerformanceLabView::results;
            resultsHeading.setVisible(visible);
            resultSummary.setVisible(visible);
            exportButton.setVisible(visible);
            juce::String text;
            if (state.selectedResult && *state.selectedResult < state.history.size())
            {
                const auto& record = state.history[*state.selectedResult];
                text << "Status: " << statusName(record.status)
                     << "\nStrategy: " << record.configuration.strategyId
                     << "\nScenario: " << record.configuration.scenarioId << "\nWorkload: "
                     << record.configuration.workloadId << "\nCommit: " << record.provenance.commit
                     << "\nBuild: " << record.provenance.buildType << "\nOS: "
                     << record.provenance.operatingSystem << "\nCPU: " << record.provenance.cpu
                     << "\nAudio: " << record.provenance.audio.deviceName << " / "
                     << record.provenance.audio.backend << " / "
                     << record.provenance.audio.sampleRateHz << " Hz / "
                     << static_cast<int>(record.provenance.audio.bufferSizeFrames)
                     << " frames\n\nMetrics\n";
                for (const auto& summary : record.summaries)
                    text << summary.metricId << ": median " << summary.median << " " << summary.unit
                         << ", p-tail " << summary.tailPercentile << ", min " << summary.minimum
                         << ", max " << summary.maximum << ", variability " << summary.variability
                         << " (n=" << static_cast<int>(summary.sampleCount) << ")\n";
                text << "\nRaw trials\n";
                for (const auto& trial : record.trials)
                    text << "Trial " << static_cast<int>(trial.trialIndex + 1) << ": "
                         << static_cast<double>(trial.duration.count()) / 1000000.0 << " ms, "
                         << static_cast<int>(trial.deadlineMisses) << " deadline misses, "
                         << static_cast<int>(trial.dropouts) << " dropouts, "
                         << static_cast<int>(trial.underruns) << " underruns\n";
                for (const auto& warning : record.warnings)
                    text << "Warning: " << warning.message << "\n";
                if (record.statusDetail)
                    text << "Detail: " << *record.statusDetail << "\n";
            }
            if (state.error)
                text << "\nError: " << *state.error;
            resultSummary.setText(text, false);
        }

        void updateComparison(const performance::PerformanceLabState& state)
        {
            const auto visible = state.view == performance::PerformanceLabView::comparison;
            comparisonHeading.setVisible(visible);
            comparisonTable.setVisible(visible);
            juce::String text;
            if (state.comparison)
            {
                if (!state.comparison->comparable)
                {
                    text << "Not comparable. Improvement and regression labels are "
                            "blocked.\n\nMismatched fields\n";
                    for (const auto& mismatch : state.comparison->mismatches)
                        text << mismatch.field << ": " << mismatch.baselineValue << " vs "
                             << mismatch.candidateValue << "\n";
                }
                else
                {
                    text << "Baseline | Candidate | Absolute change | Relative change\n";
                    for (const auto& metric : state.comparison->metrics)
                    {
                        text << metric.metricId << ": " << metric.baselineValue.value_or(0.0)
                             << " | " << metric.candidateValue.value_or(0.0) << " | "
                             << metric.absoluteChange.value_or(0.0) << " | ";
                        if (metric.relativePercentChange)
                            text << *metric.relativePercentChange << "%";
                        else
                            text << "n/a";
                        text << "\n";
                    }
                }
            }
            comparisonTable.setText(text, false);
        }

        void layoutCurrentView()
        {
            auto bounds = contentViewport.getLocalBounds().withWidth(juce::jmax(
                720, contentViewport.getWidth() - contentViewport.getScrollBarThickness()));
            content.setSize(bounds.getWidth(), juce::jmax(620, contentViewport.getHeight()));
            bounds = content.getLocalBounds().reduced(20);
            auto row = [&bounds](juce::Component& label, juce::Component& control)
            {
                auto area = bounds.removeFromTop(38);
                label.setBounds(area.removeFromLeft(180));
                control.setBounds(area.removeFromLeft(360));
                bounds.removeFromTop(7);
            };

            const auto view = controller.state().view;
            if (view == performance::PerformanceLabView::configuration)
            {
                heading.setBounds(bounds.removeFromTop(36));
                bounds.removeFromTop(14);
                row(scenarioLabel, scenarioBox);
                row(strategyLabel, strategyBox);
                row(warmUpLabel, warmUpEditor);
                row(trialsLabel, trialsEditor);
                row(deviceLabel, deviceEditor);
                row(backendLabel, backendEditor);
                row(sampleRateLabel, sampleRateBox);
                row(bufferSizeLabel, bufferSizeBox);
                validationLabel.setBounds(bounds.removeFromTop(58));
                runButton.setBounds(bounds.removeFromTop(38).removeFromLeft(180));
            }
            else if (view == performance::PerformanceLabView::liveRun)
            {
                liveHeading.setBounds(bounds.removeFromTop(36));
                liveStatus.setBounds(bounds.removeFromTop(70));
                progressBar.setBounds(bounds.removeFromTop(28));
                bounds.removeFromTop(14);
                warningLabel.setBounds(bounds.removeFromTop(100));
                cancelButton.setBounds(bounds.removeFromTop(38).removeFromLeft(180));
            }
            else
            {
                auto* viewHeading =
                    view == performance::PerformanceLabView::history ? &historyHeading
                    : view == performance::PerformanceLabView::results
                        ? &resultsHeading
                        : &comparisonHeading;
                auto* editor = view == performance::PerformanceLabView::history ? &historyTable
                               : view == performance::PerformanceLabView::results
                                   ? &resultSummary
                                   : &comparisonTable;
                viewHeading->setBounds(bounds.removeFromTop(36));
                bounds.removeFromTop(10);
                if (view != performance::PerformanceLabView::comparison)
                {
                    auto* action = view == performance::PerformanceLabView::history
                                       ? &reopenButton
                                       : &exportButton;
                    action->setBounds(bounds.removeFromBottom(38).removeFromLeft(180));
                    bounds.removeFromBottom(10);
                }
                editor->setBounds(bounds);
            }
        }

        void chooseReopenFile()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Open benchmark result", juce::File{}, "*.json");
            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& chooser)
                {
                    const auto file = chooser.getResult();
                    if (file.existsAsFile())
                    {
                        const auto decoded =
                            performance::BenchmarkRecordCodec::decode(juce::JSON::parse(file));
                        if (decoded.record)
                        {
                            reopenedRecord = *decoded.record;
                            (void)controller.reopen(reopenedRecord->runId);
                        }
                    }
                    refresh();
                });
        }

        void chooseExportFile()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Export benchmark report",
                juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                    .getChildFile("benchmark-report.json"),
                "*.json");
            fileChooser->launchAsync(
                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                    juce::FileBrowserComponent::warnAboutOverwriting,
                [this](const juce::FileChooser& chooser)
                {
                    const auto file = chooser.getResult();
                    if (file != juce::File{})
                        (void)controller.exportSelected(file.getFullPathName().toStdString());
                    refresh();
                });
        }

        performance::PerformanceLabController& controller;
        juce::Component navigation;
        juce::TextButton configurationButton{"Configuration"}, liveButton{"Live"},
            historyButton{"History"}, resultsButton{"Results"}, comparisonButton{"Compare"};
        juce::Viewport contentViewport;
        juce::Component content;
        std::vector<juce::Component*> viewComponents;

        juce::Label heading, scenarioLabel, strategyLabel, warmUpLabel, trialsLabel, deviceLabel,
            backendLabel, sampleRateLabel, bufferSizeLabel, validationLabel;
        juce::ComboBox scenarioBox, strategyBox, sampleRateBox, bufferSizeBox;
        juce::TextEditor warmUpEditor, trialsEditor, deviceEditor, backendEditor;
        juce::TextButton runButton{"Run benchmark"};

        juce::Label liveHeading, liveStatus, warningLabel;
        double progressValue;
        juce::ProgressBar progressBar;
        juce::TextButton cancelButton{"Cancel safely"};

        juce::Label historyHeading, resultsHeading, comparisonHeading;
        juce::TextEditor historyTable, resultSummary, comparisonTable;
        juce::TextButton reopenButton{"Open saved result"}, exportButton{"Export JSON"};
        std::unique_ptr<juce::FileChooser> fileChooser;
        std::optional<performance::BenchmarkRunRecord> reopenedRecord;
    };

    explicit PerformanceLabWindow(std::function<void()> closeHandler)
        : DocumentWindow(
              "Performance Lab",
              juce::Colours::darkgrey,
              juce::DocumentWindow::allButtons),
          recordStore(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("PracticeTakes")
                          .getChildFile("PerformanceLab")),
          controller(
              validateConfiguration,
              [this](
                  const auto& configuration,
                  const auto& reportProgress,
                  const auto& cancellationRequested)
              { return executeFixtureRun(configuration, reportProgress, cancellationRequested); },
              [this](std::string_view runId) -> std::optional<performance::BenchmarkRunRecord>
              {
                  if (pendingReopen && pendingReopen->runId == runId)
                      return pendingReopen;
                  const auto result = recordStore.load(runId);
                  return result.record;
              },
              [](const auto& record, std::string_view destination)
              {
                  const auto destinationPath = juce::String::fromUTF8(
                      destination.data(), static_cast<int>(destination.size()));
                  return performance::exportBenchmarkRecord(record, juce::File(destinationPath)) ==
                         performance::RecordStoreStatus::succeeded;
              }),
          onClose(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Content(controller), true);
        setResizable(true, true);
        setResizeLimits(780, 620, 1600, 1200);
        centreWithSize(980, 760);
        setVisible(true);
    }

    ~PerformanceLabWindow() override
    {
        setLookAndFeel(nullptr);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        juce::MessageManager::callAsync(onClose);
    }

    void applyAppearance(juce::LookAndFeel* appearance, juce::Colour background)
    {
        setLookAndFeel(appearance);
        setBackgroundColour(background);
        sendLookAndFeelChange();
        repaint();
    }

  private:
    static performance::ValidationResult
    validateConfiguration(const performance::RunConfiguration& configuration)
    {
        performance::ValidationResult result;
        const auto require = [&result](bool condition, std::string message)
        {
            if (!condition)
            {
                result.valid = false;
                result.errors.push_back(std::move(message));
            }
        };
        require(!configuration.scenarioId.empty(), "Select a scenario");
        require(!configuration.strategyId.empty(), "Select a strategy");
        require(configuration.measuredTrialCount > 0, "Measured trials must be at least one");
        require(!configuration.audio.deviceName.empty(), "Select an audio device");
        require(!configuration.audio.backend.empty(), "Select an audio backend");
        require(configuration.audio.sampleRateHz > 0.0, "Select a sample rate");
        require(configuration.audio.bufferSizeFrames > 0, "Select a buffer size");
        return result;
    }

    performance::BenchmarkRunRecord executeFixtureRun(
        const performance::RunConfiguration& configuration,
        const std::function<void(const performance::RunnerProgress&)>& reportProgress,
        const std::function<bool()>& cancellationRequested)
    {
        performance::BenchmarkRunRecord record;
        record.runId = juce::Uuid().toString().toStdString();
        record.status = performance::RunStatus::running;
        record.configuration = configuration;
        record.provenance.applicationVersion = PRACTICE_TAKES_VERSION;
        record.provenance.commit = PRACTICE_TAKES_COMMIT;
        record.provenance.buildType = PRACTICE_TAKES_BUILD_TYPE;
        record.provenance.operatingSystem =
            juce::SystemStats::getOperatingSystemName().toStdString();
        record.provenance.cpu = juce::SystemStats::getCpuModel().toStdString();
        record.provenance.memoryBytes =
            static_cast<std::uint64_t>(juce::SystemStats::getMemorySizeInMegabytes()) * 1024 * 1024;
        record.provenance.audio = configuration.audio;
        record.provenance.instrumentationVersion = PRACTICE_TAKES_INSTRUMENTATION_VERSION;
        record.provenance.instrumentationOverheadStatus =
            performance::InstrumentationOverheadStatus::unknown;
        record.warnings.push_back(
            {performance::WarningCode::instrumentationOverheadUnknown,
             "Instrumentation overhead has not been calibrated on this hardware"});

        reportProgress(
            {performance::RunnerPhase::stabilizing, 0, configuration.measuredTrialCount});
        reportProgress({performance::RunnerPhase::warmingUp, 0, configuration.measuredTrialCount});
        for (std::uint32_t trial = 0; trial < configuration.measuredTrialCount; ++trial)
        {
            if (cancellationRequested())
            {
                record.status = performance::RunStatus::cancelled;
                record.statusDetail = "Cancelled after a safe trial boundary";
                record.warnings.push_back(
                    {performance::WarningCode::incompleteRun, "Run is incomplete"});
                break;
            }
            reportProgress(
                {performance::RunnerPhase::measuring, trial, configuration.measuredTrialCount});
            performance::TrialMeasurement measurement;
            measurement.trialIndex = trial;
            const auto base = configuration.strategyId == "baseline" ? 2.0 : 1.5;
            const auto value = base + static_cast<double>(trial) * 0.02;
            measurement.duration =
                std::chrono::nanoseconds(static_cast<std::int64_t>(value * 1000000.0));
            measurement.samples.push_back({"analysis-latency", value, "ms"});
            record.trials.push_back(std::move(measurement));
        }
        if (record.status == performance::RunStatus::running)
            record.status = performance::RunStatus::completed;
        if (!record.trials.empty())
        {
            const auto first = record.trials.front().samples.front().value;
            const auto last = record.trials.back().samples.front().value;
            record.summaries.push_back(
                {"analysis-latency", "ms", record.trials.size(), (first + last) / 2.0, last, first,
                 last, last - first});
        }
        reportProgress(
            {performance::RunnerPhase::completed, static_cast<std::uint32_t>(record.trials.size()),
             configuration.measuredTrialCount});
        (void)recordStore.save(record);
        return record;
    }

    performance::BenchmarkRecordStore recordStore;
    std::optional<performance::BenchmarkRunRecord> pendingReopen;
    performance::PerformanceLabController controller;
    std::function<void()> onClose;
};

#endif
