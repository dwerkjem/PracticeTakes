cmake_minimum_required(VERSION 3.25)

# The vcpkg manifest lives under tools/ to keep the repository root small. The
# vcpkg toolchain only auto-discovers a manifest in CMAKE_SOURCE_DIR, so it has
# to be pointed at the real directory before project() loads the toolchain.
set(VCPKG_MANIFEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools")

set(PRACTICE_TAKES_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/tools/VERSION")

if(NOT EXISTS "${PRACTICE_TAKES_VERSION_FILE}")
    message(FATAL_ERROR
        "Missing VERSION file at ${PRACTICE_TAKES_VERSION_FILE}")
endif()

file(READ "${PRACTICE_TAKES_VERSION_FILE}" PRACTICE_TAKES_VERSION)
string(STRIP "${PRACTICE_TAKES_VERSION}" PRACTICE_TAKES_VERSION)

if(NOT PRACTICE_TAKES_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "VERSION must use MAJOR.MINOR.PATCH format, for example 1.2.0")
endif()

project(
    PracticeTakes
    VERSION ${PRACTICE_TAKES_VERSION}
    LANGUAGES C CXX
)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE PRACTICE_TAKES_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(PRACTICE_TAKES_COMMIT STREQUAL "")
    set(PRACTICE_TAKES_COMMIT "unknown")
endif()

set(PRACTICE_TAKES_FEEDBACK_ENDPOINT
    "https://practice-takes-feedback-intake.derekrneilson.workers.dev" CACHE STRING
    "HTTPS base URL for the Practice Takes feedback intake service")

option(PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB
    "Enable the development-only Performance Lab window" OFF)

# The manual GUI harness drives the application through this channel. It is a
# control surface over the application's own state, so it must never be present
# in a shipped build -- default off, and the release workflow does not set it.
option(PRACTICE_TAKES_ENABLE_TEST_CONTROL
    "Enable the development-only test control channel used by the manual GUI harness" OFF)

# Coverage instrumentation for PracticeTakesTests. Off by default, and it needs
# its own build tree: instrumented objects are not interchangeable with ordinary
# ones, and ccache does not play well with coverage counters. Use
# tools/scripts/quality/run_coverage.py rather than configuring this by hand.
option(PRACTICE_TAKES_ENABLE_COVERAGE
    "Instrument PracticeTakesTests for gcov coverage measurement" OFF)

if(PRACTICE_TAKES_FEEDBACK_ENDPOINT STREQUAL "")
    set(PRACTICE_TAKES_USE_CURL 0)
else()
    set(PRACTICE_TAKES_USE_CURL 1)
endif()

set(PRACTICE_TAKES_JUCE_APP_OPTIONS)
if(PRACTICE_TAKES_USE_CURL)
    list(APPEND PRACTICE_TAKES_JUCE_APP_OPTIONS NEEDS_CURL TRUE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate the compilation database used by clang-tidy and VS Code IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep JUCE's optional browser/network dependencies out of this tuner.
# These directory-level definitions also apply while JUCE builds juceaide.
add_compile_definitions(
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=${PRACTICE_TAKES_USE_CURL}
    JUCE_USE_XCURSOR=0
)

include(FetchContent)
include(CTest)

# Dependencies are pinned to the exact commit behind each release tag. A shallow
# clone only retrieves ref tips, so a replacement commit must also be tagged
# upstream or the fetch will not contain it.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850 # 8.0.12
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG 2b60af89e23d28eefc081bc930831ee9d45ea58b # v3.8.1
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(Catch2)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include("${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/PatchJuceAlsaChannelMismatch.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/PatchJuceAlsaInputStartup.cmake")
    practice_takes_patch_juce_alsa_channel_mismatch("${juce_SOURCE_DIR}")
    practice_takes_patch_juce_alsa_input_startup("${juce_SOURCE_DIR}")
endif()

# The icon artwork is generated from the design system by
# tools/scripts/design/generate_app_icons.py; see tools/packaging/icons/README.md. JUCE
# turns ICON_BIG/ICON_SMALL into the macOS .icns and the Windows executable
# icon, and ignores them elsewhere.
set(PRACTICE_TAKES_ICON_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tools/packaging/icons")

juce_add_gui_app(PracticeTakes
    ${PRACTICE_TAKES_JUCE_APP_OPTIONS}
    VERSION ${PROJECT_VERSION}
    PRODUCT_NAME "Practice Takes"
    COMPANY_NAME "Derek R. Neilson"
    BUNDLE_ID "org.derekrneilson.practicetakes"
    ICON_BIG "${PRACTICE_TAKES_ICON_DIRECTORY}/practice-takes-1024.png"
    ICON_SMALL "${PRACTICE_TAKES_ICON_DIRECTORY}/practice-takes-128.png"
    # macOS terminates any process that opens an audio input device without a
    # usage description, so the tuner needs this key in the generated
    # Info.plist. JUCE writes it for Apple targets and ignores it elsewhere.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT
        "Practice Takes listens to your instrument to show tuning and spectrum analysis."
)

juce_generate_juce_header(PracticeTakes)

# JUCE only consumes ICON_BIG/ICON_SMALL on macOS and Windows. Linux window
# managers read _NET_WM_ICON off the window instead, so the artwork is also
# embedded for the runtime call in main.cpp that publishes it.
juce_add_binary_data(PracticeTakesIcon
    HEADER_NAME PracticeTakesIcon.h
    NAMESPACE PracticeTakesIcon
    SOURCES "${PRACTICE_TAKES_ICON_DIRECTORY}/practice-takes-128.png"
)

# The space-free binary name is what the Debian package, the NSIS installer,
# and the tooling scripts under tools/scripts/ all expect. Apple is deliberately
# excluded: juce_add_gui_app names both the bundle and its executable after
# PRODUCT_NAME and bakes that same name into the configure-time Info.plist as
# CFBundleExecutable, so renaming the binary afterwards leaves the bundle
# pointing at a file that does not exist and macOS refuses to launch it.
if(NOT APPLE)
    set_target_properties(PracticeTakes PROPERTIES OUTPUT_NAME "PracticeTakes")
endif()

set_target_properties(PracticeTakes PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND DEFINED VCPKG_TARGET_TRIPLET)
    if(DEFINED VCPKG_INSTALLED_DIR)
        set(vcpkg_runtime_directory
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    else()
        set(vcpkg_runtime_directory
            "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
    endif()

    set_target_properties(PracticeTakes PROPERTIES
        BUILD_RPATH "${vcpkg_runtime_directory}")
endif()

target_sources(PracticeTakes
    PRIVATE
        src/bootstrap/main.cpp
        src/application/configuration/AppDefaults.h
        src/application/configuration/SettingsPersistence.cpp
        src/application/configuration/SettingsPersistence.h
        src/application/configuration/SettingsImportPersistence.h
        src/application/configuration/SettingsImportTransaction.h
        src/application/configuration/SettingsTransferCodec.cpp
        src/application/configuration/SettingsTransferCodec.h
        src/application/configuration/SettingsTransferFile.h
        src/application/shell/MainComponent.h
        src/application/shell/MainComponentLifecycle.cpp
        src/application/shell/WorkspaceStartupSequence.h
        src/application/shell/state/appearance/MainComponentAppearance.cpp
        src/application/shell/state/audio/MainComponentAudioState.cpp
        src/application/shell/state/testcontrol/MainComponentTestControl.cpp
        src/application/shell/ui/main_window/MainTitleBar.h
        src/application/shell/ui/main_window/MicrophoneWarning.h
        src/application/shell/ui/performance/PerformanceLabWindow.h
        src/application/shell/ui/feedback/FeedbackWindow.h
        src/application/shell/ui/feedback/MainComponentFeedback.cpp
        src/application/shell/ui/settings/MainComponentSettings.cpp
        src/application/shell/ui/settings/SettingsWindow.h
        src/application/shell/ui/workspace/components/DockedToolPanel.h
        src/application/shell/ui/workspace/components/ToolDragHandle.h
        src/application/shell/ui/workspace/components/ToolOptionsButton.h
        src/application/shell/ui/workspace/components/ToolWindow.h
        src/application/shell/ui/workspace/components/WorkspaceSplitPane.h
        src/application/shell/ui/workspace/components/WorkspaceTabBarButton.h
        src/application/shell/ui/workspace/components/WorkspaceTabbedComponent.h
        src/application/shell/ui/workspace/model/WorkspaceBuiltIns.h
        src/application/shell/ui/workspace/model/WorkspaceCatalogCodec.cpp
        src/application/shell/ui/workspace/model/WorkspaceCatalogCodec.h
        src/application/shell/ui/workspace/model/WorkspaceDocuments.h
        src/application/shell/ui/workspace/model/WorkspaceLayoutState.h
        src/application/shell/ui/workspace/model/WorkspaceMenuModel.h
        src/application/shell/ui/workspace/model/NamedWorkspaceService.h
        src/application/shell/ui/workspace/model/WorkspaceNormalizer.h
        src/application/shell/ui/workspace/model/WorkspaceSnapshotApply.h
        src/application/shell/ui/workspace/model/WorkspaceSnapshotCapture.h
        src/application/shell/ui/workspace/model/WorkspaceToolRegistry.h
        src/application/shell/ui/workspace/model/WorkspaceSessionLifecycle.h
        src/application/shell/ui/workspace/model/WorkspaceToolState.h
        src/application/shell/ui/workspace/shell/MainComponentWorkspaceDrag.cpp
        src/application/shell/ui/workspace/shell/MainComponentWorkspaceLayout.cpp
        src/application/shell/ui/workspace/shell/MainComponentWorkspaceMenu.cpp
        src/application/shell/ui/workspace/shell/MainComponentWorkspaceManagement.cpp
        src/application/shell/ui/workspace/shell/MainComponentWorkspacePresentation.cpp
        src/application/shell/ui/workspace/shell/MainComponentWorkspaceSnapshot.cpp
        src/application/testcontrol/ApprovedWindowStates.cpp
        src/application/testcontrol/ApprovedWindowStates.h
        src/application/testcontrol/TestControlChannel.cpp
        src/application/testcontrol/TestControlChannel.h
        src/application/testcontrol/TestControlClicker.cpp
        src/application/testcontrol/TestControlClicker.h
        src/application/testcontrol/TestControlCommand.cpp
        src/application/testcontrol/TestControlCommand.h
        src/application/testcontrol/TestControlSession.cpp
        src/application/testcontrol/TestControlSession.h
        src/application/testcontrol/TestControlTarget.h
        src/application/theme/Theme.cpp
        src/application/theme/Theme.h
        src/application/theme/ThemeType.h
        src/features/analysis/spectrogram/SpectrogramComponent.cpp
        src/features/analysis/spectrogram/SpectrogramComponent.h
        src/features/analysis/harmonics/HarmonicAnalyzer.cpp
        src/features/analysis/harmonics/HarmonicAnalyzer.h
        src/features/analysis/harmonics/HarmonicAnalyzerComponent.cpp
        src/features/analysis/harmonics/HarmonicAnalyzerComponent.h
        src/features/analysis/tuner/PitchDetector.cpp
        src/features/analysis/tuner/PitchDetector.h
        src/features/analysis/tuner/TunerComponent.cpp
        src/features/analysis/tuner/TunerComponent.h
        src/features/analysis/tuner/TunerDrawing.cpp
        src/features/performance/ApplicationLaunchTimer.cpp
        src/features/performance/ApplicationLaunchTimer.h
        src/features/performance/BenchmarkRecordCodec.cpp
        src/features/performance/BenchmarkRecordCodec.h
        src/features/performance/BenchmarkRecordStore.cpp
        src/features/performance/BenchmarkRecordStore.h
        src/features/performance/BenchmarkComparability.cpp
        src/features/performance/BenchmarkComparability.h
        src/features/performance/BenchmarkComparison.cpp
        src/features/performance/BenchmarkComparison.h
        src/features/performance/BenchmarkRunner.cpp
        src/features/performance/BenchmarkRunner.h
        src/features/performance/HarmonicAnalysisActions.cpp
        src/features/performance/HarmonicAnalysisActions.h
        src/features/performance/InTreeOptimizationStrategy.cpp
        src/features/performance/InTreeOptimizationStrategy.h
        src/features/performance/NonRealTimeTelemetryCollector.cpp
        src/features/performance/NonRealTimeTelemetryCollector.h
        src/features/performance/PerformanceLabController.cpp
        src/features/performance/PerformanceLabController.h
        src/features/performance/PlatformProvenanceProvider.cpp
        src/features/performance/PlatformProvenanceProvider.h
        src/features/performance/RealTimeEventCapture.h
        src/features/performance/SustainedAudioAnalysisScenario.cpp
        src/features/performance/SustainedAudioAnalysisScenario.h
        src/features/performance/StrategyRegistry.cpp
        src/features/performance/StrategyRegistry.h
        src/features/performance/TrialAggregator.cpp
        src/features/performance/TrialAggregator.h
        src/features/feedback/FeedbackComponent.cpp
        src/features/feedback/FeedbackComponent.h
        src/features/feedback/FeedbackInvitationPolicy.h
        src/platform/audio/AudioInputService.cpp
        src/platform/audio/AudioInputService.h
        src/platform/audio/AudioRecoveryPolicy.h
        src/platform/audio/AudioSampleFifo.h
        src/platform/score/Diagnostic.h
        src/platform/score/LyricSyllable.h
        src/platform/score/Measure.h
        src/platform/score/MusicalTime.h
        src/platform/score/Pitch.h
        src/platform/score/Score.h
        src/platform/score/ScoreAttributes.h
        src/platform/score/ScoreBuilder.cpp
        src/platform/score/ScoreBuilder.h
        src/platform/score/ScoreEvent.h
        src/platform/score/ScoreInvariants.cpp
        src/platform/score/ScoreInvariants.h
        src/platform/score/TempoMap.cpp
        src/platform/score/TempoMap.h
        src/platform/score/musicxml/MusicXmlDiagnosticSink.cpp
        src/platform/score/musicxml/MusicXmlDiagnosticSink.h
        src/platform/score/musicxml/MusicXmlImportResult.h
        src/platform/score/musicxml/MusicXmlImporter.cpp
        src/platform/score/musicxml/MusicXmlImporter.h
        src/platform/score/musicxml/XmlDocumentAdapter.cpp
        src/platform/score/musicxml/XmlDocumentAdapter.h
        src/platform/score/musicxml/container/MusicXmlContainer.cpp
        src/platform/score/musicxml/container/MusicXmlContainer.h
        src/platform/score/musicxml/read/MusicXmlMeasureReader.cpp
        src/platform/score/musicxml/read/MusicXmlMeasureReader.h
        src/platform/score/musicxml/read/MusicXmlReadContext.cpp
        src/platform/score/musicxml/read/MusicXmlReadContext.h
        src/platform/score/musicxml/read/MusicXmlScoreReader.cpp
        src/platform/score/musicxml/read/MusicXmlScoreReader.h
        src/platform/score/musicxml/read/MusicXmlSubset.cpp
        src/platform/score/musicxml/read/MusicXmlSubset.h
        src/platform/score/musicxml/read/MusicXmlTieResolver.cpp
        src/platform/score/musicxml/read/MusicXmlTieResolver.h
        src/platform/score/musicxml/read/MusicXmlValueParsing.cpp
        src/platform/score/musicxml/read/MusicXmlValueParsing.h
)

# src/ on the include path so a file deep in the tree includes a sibling
# subsystem by its full path -- "platform/score/Score.h" -- rather than counting
# ../ hops. The test target already does this; this keeps the two consistent, so
# an include that compiles in a test compiles in the application.
target_include_directories(PracticeTakes
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(PracticeTakes
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=${PRACTICE_TAKES_USE_CURL}
        PRACTICE_TAKES_FEEDBACK_ENDPOINT="${PRACTICE_TAKES_FEEDBACK_ENDPOINT}"
        PRACTICE_TAKES_VERSION="${PROJECT_VERSION}"
        PRACTICE_TAKES_COMMIT="${PRACTICE_TAKES_COMMIT}"
        PRACTICE_TAKES_BUILD_TYPE="$<CONFIG>"
        PRACTICE_TAKES_ENABLE_TEST_CONTROL=$<BOOL:${PRACTICE_TAKES_ENABLE_TEST_CONTROL}>
        PRACTICE_TAKES_INSTRUMENTATION_VERSION="1"
        PRACTICE_TAKES_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB=$<BOOL:${PRACTICE_TAKES_ENABLE_PERFORMANCE_LAB}>
)

target_link_libraries(PracticeTakes
    PRIVATE
        PracticeTakesIcon
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
    target_link_libraries(PracticeTakes PRIVATE X11::X11)
endif()

if(BUILD_TESTING)
    add_executable(PracticeTakesTests
        src/tests/application/configuration/AppDefaultsTests.cpp
        src/tests/application/configuration/SettingsPersistenceTests.cpp
        src/tests/application/configuration/SettingsTransferCodecTests.cpp
        src/tests/application/shell/WorkspaceStartupSequenceTests.cpp
        src/tests/application/shell/ui/workspace/components/WorkspacePresentationComponentsTests.cpp
        src/tests/application/shell/ui/workspace/model/NamedWorkspaceServiceTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceBuiltInsTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceCatalogCodecTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceDocumentsTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceLayoutStateTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceMenuModelTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceRecoveryTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceSessionLifecycleTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceSnapshotApplyTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceSnapshotCaptureTests.cpp
        src/tests/application/shell/ui/workspace/model/WorkspaceToolStateTests.cpp
        src/tests/application/testcontrol/ApprovedWindowStatesTests.cpp
        src/tests/application/testcontrol/TestControlClickerTests.cpp
        src/tests/application/testcontrol/TestControlCommandTests.cpp
        src/tests/application/testcontrol/TestControlSessionTests.cpp
        src/tests/application/theme/ThemeContrastTests.cpp
        src/tests/features/analysis/harmonics/HarmonicAnalyzerTests.cpp
        src/tests/features/analysis/tuner/PitchDetectorTests.cpp
        src/tests/features/feedback/FeedbackInvitationPolicyTests.cpp
        src/tests/features/performance/ApplicationLaunchTimerTests.cpp
        src/tests/features/performance/BenchmarkComparabilityTests.cpp
        src/tests/features/performance/BenchmarkComparisonTests.cpp
        src/tests/features/performance/BenchmarkInterfacesTests.cpp
        src/tests/features/performance/BenchmarkRecordCodecTests.cpp
        src/tests/features/performance/BenchmarkRecordStoreTests.cpp
        src/tests/features/performance/BenchmarkRunnerTests.cpp
        src/tests/features/performance/BenchmarkTypesTests.cpp
        src/tests/features/performance/InTreeOptimizationStrategyTests.cpp
        src/tests/features/performance/NonRealTimeTelemetryCollectorTests.cpp
        src/tests/features/performance/PerformanceLabControllerTests.cpp
        src/tests/features/performance/PlatformProvenanceProviderTests.cpp
        src/tests/features/performance/RealTimeEventCaptureTests.cpp
        src/tests/features/performance/StrategyRegistryTests.cpp
        src/tests/features/performance/SustainedAudioAnalysisScenarioTests.cpp
        src/tests/features/performance/TrialAggregatorTests.cpp
        src/tests/platform/audio/AudioRecoveryPolicyTests.cpp
        src/tests/platform/audio/AudioSampleFifoLoadTests.cpp
        src/tests/platform/audio/AudioSampleFifoTests.cpp
        src/tests/platform/score/MusicalTimeTests.cpp
        src/tests/platform/score/PitchTests.cpp
        src/tests/platform/score/ScoreInvariantsTests.cpp
        src/tests/platform/score/ScoreModelTests.cpp
        src/tests/platform/score/TempoMapTests.cpp
        src/tests/support/BenchmarkFakesTests.cpp
        src/application/configuration/SettingsPersistence.cpp
        src/application/configuration/SettingsTransferCodec.cpp
        src/application/shell/ui/workspace/model/WorkspaceCatalogCodec.cpp
        src/application/testcontrol/ApprovedWindowStates.cpp
        src/application/testcontrol/TestControlClicker.cpp
        src/application/testcontrol/TestControlCommand.cpp
        src/application/testcontrol/TestControlSession.cpp
        src/application/theme/Theme.cpp
        src/features/analysis/harmonics/HarmonicAnalyzer.cpp
        src/features/analysis/tuner/PitchDetector.cpp
        src/features/performance/ApplicationLaunchTimer.cpp
        src/features/performance/BenchmarkRecordCodec.cpp
        src/features/performance/BenchmarkRecordStore.cpp
        src/features/performance/BenchmarkComparability.cpp
        src/features/performance/BenchmarkComparison.cpp
        src/features/performance/BenchmarkRunner.cpp
        src/features/performance/HarmonicAnalysisActions.cpp
        src/features/performance/InTreeOptimizationStrategy.cpp
        src/features/performance/NonRealTimeTelemetryCollector.cpp
        src/features/performance/PerformanceLabController.cpp
        src/features/performance/PlatformProvenanceProvider.cpp
        src/features/performance/SustainedAudioAnalysisScenario.cpp
        src/features/performance/StrategyRegistry.cpp
        src/features/performance/TrialAggregator.cpp
        src/platform/score/ScoreBuilder.cpp
        src/platform/score/ScoreInvariants.cpp
        src/platform/score/TempoMap.cpp
    )

    # src/tests/ mirrors src/, so a test's path says which source file it covers.
    # Both roots are on the include path: "platform/score/Score.h" resolves
    # against src/, and shared fixtures such as "support/BenchmarkFakes.h"
    # resolve against src/tests/ from any depth.
    target_include_directories(PracticeTakesTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/tests"
    )

    target_link_libraries(PracticeTakesTests
        PRIVATE
            Catch2::Catch2WithMain
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_graphics
            juce::juce_gui_basics
    )

    if(PRACTICE_TAKES_ENABLE_COVERAGE)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR
                "PRACTICE_TAKES_ENABLE_COVERAGE needs a GCC or Clang toolchain, "
                "but the compiler is ${CMAKE_CXX_COMPILER_ID}")
        endif()

        # -O0 so line counts map to the source a reader is looking at; an
        # optimised build merges and reorders lines and the report stops
        # matching the file.
        target_compile_options(PracticeTakesTests PRIVATE --coverage -O0 -g)
        target_link_options(PracticeTakesTests PRIVATE --coverage)
    endif()

    target_compile_definitions(PracticeTakesTests
        PRIVATE
            PRACTICE_TAKES_VERSION="${PROJECT_VERSION}"
            PRACTICE_TAKES_COMMIT="${PRACTICE_TAKES_COMMIT}"
            PRACTICE_TAKES_BUILD_TYPE="$<CONFIG>"
            PRACTICE_TAKES_INSTRUMENTATION_VERSION="1"
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PRACTICE_TAKES_USE_CURL)
        target_link_libraries(PracticeTakesTests PRIVATE juce::pkgconfig_JUCE_CURL_LINUX_DEPS)
    endif()

    include(Catch)
    catch_discover_tests(PracticeTakesTests)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/Packaging.cmake")
