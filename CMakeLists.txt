cmake_minimum_required(VERSION 3.25)

set(PRACTICE_TAKES_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

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

set(PRACTICE_TAKES_FEEDBACK_ENDPOINT
    "https://practice-takes-feedback-intake.derekrneilson.workers.dev" CACHE STRING
    "HTTPS base URL for the Practice Takes feedback intake service")

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

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

if(BUILD_TESTING)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(Catch2)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchJuceAlsaChannelMismatch.cmake")
    practice_takes_patch_juce_alsa_channel_mismatch("${juce_SOURCE_DIR}")
endif()

juce_add_gui_app(PracticeTakes
    ${PRACTICE_TAKES_JUCE_APP_OPTIONS}
    VERSION ${PROJECT_VERSION}
    PRODUCT_NAME "Practice Takes"
    COMPANY_NAME "Derek R. Neilson"
    BUNDLE_ID "org.derekrneilson.practicetakes"
)

juce_generate_juce_header(PracticeTakes)

set_target_properties(PracticeTakes PROPERTIES
    OUTPUT_NAME "PracticeTakes"
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
        src/application/shell/MainComponent.h
        src/application/shell/MainComponentLifecycle.cpp
        src/application/shell/state/appearance/MainComponentAppearance.cpp
        src/application/shell/state/audio/MainComponentAudioState.cpp
        src/application/shell/ui/main_window/MainTitleBar.h
        src/application/shell/ui/main_window/MicrophoneWarning.h
        src/application/shell/ui/feedback/FeedbackWindow.h
        src/application/shell/ui/feedback/MainComponentFeedback.cpp
        src/application/shell/ui/settings/MainComponentSettings.cpp
        src/application/shell/ui/settings/SettingsWindow.h
        src/application/shell/ui/workspace/DockedToolPanel.h
        src/application/shell/ui/workspace/MainComponentWorkspaceDrag.cpp
        src/application/shell/ui/workspace/MainComponentWorkspaceLayout.cpp
        src/application/shell/ui/workspace/MainComponentWorkspaceMenu.cpp
        src/application/shell/ui/workspace/MainComponentWorkspacePresentation.cpp
        src/application/shell/ui/workspace/ToolDragHandle.h
        src/application/shell/ui/workspace/ToolOptionsButton.h
        src/application/shell/ui/workspace/ToolWindow.h
        src/application/shell/ui/workspace/WorkspaceLayoutState.h
        src/application/shell/ui/workspace/WorkspaceSplitPane.h
        src/application/shell/ui/workspace/WorkspaceToolState.h
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
        src/features/performance/BenchmarkRecordCodec.cpp
        src/features/performance/BenchmarkRecordCodec.h
        src/features/performance/InTreeOptimizationStrategy.cpp
        src/features/performance/InTreeOptimizationStrategy.h
        src/features/feedback/FeedbackComponent.cpp
        src/features/feedback/FeedbackComponent.h
        src/features/feedback/FeedbackInvitationPolicy.h
        src/services/audio/AudioInputService.cpp
        src/services/audio/AudioInputService.h
        src/services/audio/AudioRecoveryPolicy.h
        src/services/audio/AudioSampleFifo.h
)

target_compile_definitions(PracticeTakes
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=${PRACTICE_TAKES_USE_CURL}
        PRACTICE_TAKES_FEEDBACK_ENDPOINT="${PRACTICE_TAKES_FEEDBACK_ENDPOINT}"
)

target_link_libraries(PracticeTakes
    PRIVATE
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
        tests/AppDefaultsTests.cpp
        tests/AudioRecoveryPolicyTests.cpp
        tests/AudioSampleFifoTests.cpp
        tests/BenchmarkFakesTests.cpp
        tests/BenchmarkInterfacesTests.cpp
        tests/BenchmarkRecordCodecTests.cpp
        tests/BenchmarkTypesTests.cpp
        tests/FeedbackInvitationPolicyTests.cpp
        tests/HarmonicAnalyzerTests.cpp
        tests/InTreeOptimizationStrategyTests.cpp
        tests/PitchDetectorTests.cpp
        tests/SettingsPersistenceTests.cpp
        tests/ThemeContrastTests.cpp
        tests/WorkspaceLayoutStateTests.cpp
        tests/WorkspaceToolStateTests.cpp
        src/application/configuration/SettingsPersistence.cpp
        src/application/theme/Theme.cpp
        src/features/analysis/harmonics/HarmonicAnalyzer.cpp
        src/features/analysis/tuner/PitchDetector.cpp
        src/features/performance/BenchmarkRecordCodec.cpp
        src/features/performance/InTreeOptimizationStrategy.cpp
    )

    target_include_directories(PracticeTakesTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    target_link_libraries(PracticeTakesTests
        PRIVATE
            Catch2::Catch2WithMain
            juce::juce_data_structures
            juce::juce_dsp
            juce::juce_graphics
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PRACTICE_TAKES_USE_CURL)
        target_link_libraries(PracticeTakesTests PRIVATE juce::pkgconfig_JUCE_CURL_LINUX_DEPS)
    endif()

    include(Catch)
    catch_discover_tests(PracticeTakesTests)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake")
