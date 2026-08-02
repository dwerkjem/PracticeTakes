#pragma once

#if PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB

#include "../../../../features/performance/ApplicationLaunchTimer.h"
#include "../../../../features/performance/BenchmarkRecordStore.h"
#include "../../../../features/performance/HarmonicAnalysisActions.h"
#include "../../../../features/performance/NonRealTimeTelemetryCollector.h"
#include "../../../../features/performance/PerformanceLabController.h"
#include "../../../../features/performance/StrategyRegistry.h"
#include "../../../../features/performance/SustainedAudioAnalysisScenario.h"
#include "../../../../features/performance/TrialAggregator.h"
#include "../../MainComponent.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <functional>
#include <thread>
#include <utility>

class MainComponent::PerformanceLabWindow final : public juce::DocumentWindow
{
  public:
    class Content final : public juce::Component, private juce::Timer
    {
      public:
        explicit Content(
            performance::PerformanceLabController& controllerIn,
            std::function<bool(const performance::BenchmarkRunRecord&)> reopenRecordIn,
            std::function<void(int)> setCalibrationModeIn,
            std::function<void(bool)> automationCompletedIn = {})
            : controller(controllerIn), reopenRecord(std::move(reopenRecordIn)),
              setCalibrationMode(std::move(setCalibrationModeIn)),
              automationCompleted(std::move(automationCompletedIn)), progressValue(0.0),
              progressBar(progressValue)
        {
            configureNavigation();
            configureConfiguration();
            configureActions();
            addAndMakeVisible(contentViewport);
            contentViewport.setViewedComponent(&content, false);
            applyConfiguration();
            if (automationCompleted)
                juce::MessageManager::callAsync(
                    [safeThis = SafePointer<Content>(this)]
                    {
                        if (safeThis != nullptr)
                            safeThis->startAutomation();
                    });
        }

        ~Content() override
        {
            stopTimer();
            controller.requestCancellation();
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
                startRuns(false);
            };
            runAllButton.onClick = [this]
            {
                applyConfiguration();
                startRuns(true);
            };
            calibrateButton.onClick = [this]
            {
                applyConfiguration();
                startCalibration();
            };
            addViewComponent(runButton);
            addViewComponent(runAllButton);
            addViewComponent(calibrateButton);
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
            backButton.onClick = [this]
            {
                controller.showConfiguration();
                refresh();
            };
            for (auto* component : std::initializer_list<juce::Component*>{
                     &resultsHeading, &resultSummary, &exportButton, &backButton})
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
            configuration.workloadId = "generated-harmonic-fixture-v1";
            configuration.strategyId =
                strategyBox.getSelectedId() == 1 ? "baseline" : "parameterized-fixture";
            if (configuration.strategyId != "baseline")
                configuration.strategyParameters["batchSize"] = "4";
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

        void startRuns(bool runAll)
        {
            setCalibrationMode(0);
            runButton.setEnabled(false);
            runAllButton.setEnabled(false);
            calibrateButton.setEnabled(false);
            workerFinished.store(false, std::memory_order_release);
            benchmarkThread = std::jthread(
                [this, runAll]
                {
                    (void)controller.startRun();
                    if (runAll)
                    {
                        const auto state = controller.state();
                        if (!state.history.empty() &&
                            state.history.back().status == performance::RunStatus::completed)
                        {
                            auto configuration = state.configuration;
                            configuration.strategyId =
                                configuration.strategyId == "baseline"
                                    ? "parameterized-fixture"
                                    : "baseline";
                            configuration.strategyParameters.clear();
                            if (configuration.strategyId == "parameterized-fixture")
                                configuration.strategyParameters["batchSize"] = "4";
                            controller.setConfiguration(std::move(configuration));
                            (void)controller.startRun();
                        }
                    }
                    workerFinished.store(true, std::memory_order_release);
                });
            startTimerHz(20);
        }

        void startCalibration()
        {
            auto configuration = controller.state().configuration;
            configuration.strategyId = "baseline";
            configuration.strategyParameters.clear();
            controller.setConfiguration(std::move(configuration));
            runButton.setEnabled(false);
            runAllButton.setEnabled(false);
            calibrateButton.setEnabled(false);
            workerFinished.store(false, std::memory_order_release);
            benchmarkThread = std::jthread(
                [this]
                {
                    setCalibrationMode(1);
                    (void)controller.startRun();
                    const auto disabledState = controller.state();
                    if (!disabledState.history.empty() &&
                        disabledState.history.back().status == performance::RunStatus::completed)
                    {
                        setCalibrationMode(2);
                        (void)controller.startRun();
                    }
                    setCalibrationMode(0);
                    workerFinished.store(true, std::memory_order_release);
                });
            startTimerHz(20);
        }

        void startAutomation()
        {
            runButton.setEnabled(false);
            runAllButton.setEnabled(false);
            calibrateButton.setEnabled(false);
            workerFinished.store(false, std::memory_order_release);
            benchmarkThread = std::jthread(
                [this]
                {
                    bool succeeded = true;
                    const auto run = [this, &succeeded](int mode)
                    {
                        setCalibrationMode(mode);
                        if (!controller.startRun())
                        {
                            succeeded = false;
                            return;
                        }
                        const auto state = controller.state();
                        succeeded =
                            !state.history.empty() &&
                            state.history.back().status == performance::RunStatus::completed;
                    };

                    auto configuration = controller.state().configuration;
                    configuration.strategyId = "baseline";
                    configuration.strategyParameters.clear();
                    controller.setConfiguration(configuration);
                    run(1);
                    if (succeeded)
                        run(2);
                    if (succeeded)
                        run(0);
                    if (succeeded)
                    {
                        configuration.strategyId = "parameterized-fixture";
                        configuration.strategyParameters["batchSize"] = "4";
                        controller.setConfiguration(std::move(configuration));
                        run(0);
                    }
                    setCalibrationMode(0);
                    workerFinished.store(true, std::memory_order_release);
                    juce::MessageManager::callAsync(
                        [safeThis = SafePointer<Content>(this), succeeded]
                        {
                            if (safeThis != nullptr && safeThis->automationCompleted)
                                safeThis->automationCompleted(succeeded);
                        });
                });
            startTimerHz(20);
        }

        void updateConfiguration(const performance::PerformanceLabState& state)
        {
            const auto visible = state.view == performance::PerformanceLabView::configuration;
            for (auto* component : std::initializer_list<juce::Component*>{
                     &heading,         &scenarioLabel,   &strategyLabel, &warmUpLabel,
                     &trialsLabel,     &deviceLabel,     &backendLabel,  &sampleRateLabel,
                     &bufferSizeLabel, &scenarioBox,     &strategyBox,   &sampleRateBox,
                     &bufferSizeBox,   &warmUpEditor,    &trialsEditor,  &deviceEditor,
                     &backendEditor,   &validationLabel, &runButton,     &runAllButton,
                     &calibrateButton})
                component->setVisible(visible);

            juce::String errors;
            for (const auto& error : state.validation.errors)
                errors << juce::String(error) << "\n";
            validationLabel.setText(errors, juce::dontSendNotification);
            runButton.setEnabled(state.validation.valid && !state.running);
            runAllButton.setEnabled(state.validation.valid && !state.running);
            calibrateButton.setEnabled(state.validation.valid && !state.running);
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
            backButton.setVisible(visible);
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

        void timerCallback() override
        {
            refresh();
            if (workerFinished.load(std::memory_order_acquire))
            {
                stopTimer();
                benchmarkThread = {};
            }
        }

        void layoutCurrentView()
        {
            const auto availableWidth =
                contentViewport.getWidth() - contentViewport.getScrollBarThickness();
            auto bounds =
                contentViewport.getLocalBounds().withWidth(juce::jmax(720, availableWidth));
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
                auto actions = bounds.removeFromTop(38);
                runButton.setBounds(actions.removeFromLeft(180));
                actions.removeFromLeft(10);
                runAllButton.setBounds(actions.removeFromLeft(180));
                actions.removeFromLeft(10);
                calibrateButton.setBounds(actions.removeFromLeft(220));
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
                    auto actions = bounds.removeFromBottom(38);
                    if (view == performance::PerformanceLabView::results)
                    {
                        backButton.setBounds(actions.removeFromLeft(120));
                        actions.removeFromLeft(10);
                    }
                    action->setBounds(actions.removeFromLeft(180));
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
                            (void)reopenRecord(*decoded.record);
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
        std::function<bool(const performance::BenchmarkRunRecord&)> reopenRecord;
        std::function<void(int)> setCalibrationMode;
        std::function<void(bool)> automationCompleted;
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
        juce::TextButton runButton{"Run benchmark"}, runAllButton{"Run all"},
            calibrateButton{"Calibrate instrumentation"};

        juce::Label liveHeading, liveStatus, warningLabel;
        double progressValue;
        juce::ProgressBar progressBar;
        juce::TextButton cancelButton{"Cancel safely"};

        juce::Label historyHeading, resultsHeading, comparisonHeading;
        juce::TextEditor historyTable, resultSummary, comparisonTable;
        juce::TextButton reopenButton{"Open saved result"}, exportButton{"Export JSON"},
            backButton{"Back"};
        std::unique_ptr<juce::FileChooser> fileChooser;
        std::atomic_bool workerFinished{false};
        std::jthread benchmarkThread;
    };

    explicit PerformanceLabWindow(
        std::function<void()> closeHandler,
        std::function<void(bool)> automationCompleted = {})
        : DocumentWindow(
              "Performance Lab",
              juce::Colours::darkgrey,
              juce::DocumentWindow::allButtons),
          recordStore(performanceLabDataDirectory()),
          repositoryRecordStore(
              juce::File(PRACTICE_TAKES_SOURCE_DIR).getChildFile("tools/benchmark-results")),
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
        setContentOwned(
            new Content(
                controller,
                [this](const performance::BenchmarkRunRecord& record)
                {
                    pendingReopen = record;
                    return controller.reopen(record.runId);
                },
                [this](int mode) { calibrationMode.store(mode, std::memory_order_release); },
                std::move(automationCompleted)),
            true);
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
    [[nodiscard]] static juce::File performanceLabDataDirectory()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("PracticeTakes")
            .getChildFile("PerformanceLab");
    }

    static std::string runTag(const performance::RunConfiguration& configuration)
    {
        const auto slug = [](std::string value)
        {
            for (auto& character : value)
            {
                const auto byte = static_cast<unsigned char>(character);
                character = std::isalnum(byte) != 0 ? static_cast<char>(std::tolower(byte)) : '-';
            }
            while (value.find("--") != std::string::npos)
                value.replace(value.find("--"), 2, "-");
            return value;
        };
        const auto timestamp = slug(juce::Time::getCurrentTime().toISO8601(false).toStdString());
        const auto uuid = juce::Uuid().toString().substring(0, 8).toStdString();
        return timestamp + "-" + slug(juce::SystemStats::getOperatingSystemName().toStdString()) +
               "-" + slug(configuration.strategyId) + "-" +
               std::to_string(static_cast<int>(configuration.audio.sampleRateHz)) + "hz-" +
               std::to_string(configuration.audio.bufferSizeFrames) + "f-" +
               slug(PRACTICE_TAKES_COMMIT) + "-" + uuid;
    }

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

    bool hasMatchingCalibration(const performance::RunProvenance& provenance) const
    {
        const auto matches = [&provenance](const performance::BenchmarkRunRecord& record)
        {
            const auto& candidate = record.provenance;
            return record.status == performance::RunStatus::completed &&
                   candidate.instrumentationOverheadStatus ==
                       performance::InstrumentationOverheadStatus::measured &&
                   candidate.operatingSystem == provenance.operatingSystem &&
                   candidate.cpu == provenance.cpu &&
                   candidate.instrumentationVersion == provenance.instrumentationVersion &&
                   candidate.audio.deviceName == provenance.audio.deviceName &&
                   candidate.audio.backend == provenance.audio.backend &&
                   std::abs(candidate.audio.sampleRateHz - provenance.audio.sampleRateHz) < 0.01 &&
                   candidate.audio.bufferSizeFrames == provenance.audio.bufferSizeFrames;
        };
        const auto storeHasMatch = [&matches](const performance::BenchmarkRecordStore& store)
        {
            for (const auto& runId : store.listRunIds())
            {
                const auto loaded = store.load(runId);
                if (loaded.record && matches(*loaded.record))
                    return true;
            }
            return false;
        };
        return storeHasMatch(recordStore) || storeHasMatch(repositoryRecordStore);
    }

    performance::BenchmarkRunRecord executeFixtureRun(
        const performance::RunConfiguration& configuration,
        const std::function<void(const performance::RunnerProgress&)>& reportProgress,
        const std::function<bool()>& cancellationRequested)
    {
        performance::BenchmarkRunRecord record;
        record.runId = runTag(configuration);
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
        const auto currentCalibrationMode = calibrationMode.load(std::memory_order_acquire);
        record.provenance.instrumentationVersion =
            currentCalibrationMode == 1 ? "disabled-v1" : PRACTICE_TAKES_INSTRUMENTATION_VERSION;
        if (currentCalibrationMode == 2 || hasMatchingCalibration(record.provenance))
        {
            record.provenance.instrumentationOverheadStatus =
                performance::InstrumentationOverheadStatus::measured;
        }
        else
        {
            record.provenance.instrumentationOverheadStatus =
                performance::InstrumentationOverheadStatus::unknown;
            record.warnings.push_back(
                {performance::WarningCode::instrumentationOverheadUnknown,
                 "Instrumentation overhead has not been calibrated on this hardware"});
        }

        class SteadyBenchmarkClock final : public performance::BenchmarkClock
        {
          public:
            std::chrono::steady_clock::time_point now() const noexcept override
            {
                return std::chrono::steady_clock::now();
            }
        } clock;
        class UnavailableProcessMetrics final : public performance::ProcessMetricsProvider
        {
          public:
            performance::ProcessMetricsSnapshot sample() override
            {
                return {};
            }
        } processMetrics;
        class DurationOnlyTelemetry final : public performance::TelemetryCollector
        {
          public:
            void beginTrial(std::uint32_t trialIndex) override
            {
                measurement = {};
                measurement.trialIndex = trialIndex;
                startedAt = std::chrono::steady_clock::now();
            }

            performance::TrialMeasurement endTrial() override
            {
                measurement.duration = std::chrono::steady_clock::now() - startedAt;
                return measurement;
            }

            void abortTrial() noexcept override {}

          private:
            std::chrono::steady_clock::time_point startedAt{};
            performance::TrialMeasurement measurement;
        } durationOnlyTelemetry;

        performance::HarmonicAnalysisActions actions(configuration.audio.sampleRateHz);
        performance::SustainedAudioAnalysisScenario scenario(actions);
        performance::StrategyRegistry strategies;
        auto strategy = strategies.create(configuration.strategyId);
        if (!strategy)
        {
            record.status = performance::RunStatus::failed;
            record.statusDetail = "The selected benchmark strategy is unavailable";
            saveRecord(record);
            return record;
        }

        performance::NonRealTimeTelemetryCollector telemetry(processMetrics, clock);
        auto& selectedTelemetry =
            currentCalibrationMode == 1
                ? static_cast<performance::TelemetryCollector&>(durationOnlyTelemetry)
                : static_cast<performance::TelemetryCollector&>(telemetry);
        performance::BenchmarkRunner runner({}, reportProgress, {}, cancellationRequested);
        const auto result = runner.run(configuration, scenario, *strategy, selectedTelemetry);
        record.status = result.status;
        record.statusDetail = result.statusDetail;
        record.trials = result.trials;
        for (auto& trial : record.trials)
            trial.samples.push_back(
                {"analysis-latency",
                 std::chrono::duration<double, std::milli>(trial.duration).count(), "ms"});
        record.summaries = performance::TrialAggregator::aggregate(record.trials).summaries;
        if (record.status != performance::RunStatus::completed)
            record.warnings.push_back(
                {performance::WarningCode::incompleteRun, "Run is incomplete"});
        for (const auto& error : result.validation.errors)
            record.warnings.push_back({performance::WarningCode::incompleteRun, error});
        if (const auto launchDuration = performance::applicationLaunchDuration())
        {
            const auto milliseconds =
                std::chrono::duration<double, std::milli>(*launchDuration).count();
            record.summaries.push_back(
                {"launch-to-main-window", "ms", 1, milliseconds, milliseconds, milliseconds,
                 milliseconds, 0.0});
        }
        reportProgress(
            {performance::RunnerPhase::completed, static_cast<std::uint32_t>(record.trials.size()),
             configuration.measuredTrialCount});
        saveRecord(record);
        return record;
    }

    void saveRecord(performance::BenchmarkRunRecord& record)
    {
        const auto repositoryStatus = repositoryRecordStore.save(record);
        if (repositoryStatus != performance::RecordStoreStatus::succeeded)
            record.warnings.push_back(
                {performance::WarningCode::unknownProvenance,
                 "The benchmark result could not be saved in the repository"});
        (void)recordStore.save(record);
    }

    performance::BenchmarkRecordStore recordStore;
    performance::BenchmarkRecordStore repositoryRecordStore;
    std::atomic_int calibrationMode{0};
    std::optional<performance::BenchmarkRunRecord> pendingReopen;
    performance::PerformanceLabController controller;
    std::function<void()> onClose;
};

#endif
