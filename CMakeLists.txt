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
        src/main.cpp
        src/audio/AudioInputService.cpp
        src/audio/AudioInputService.h
        src/audio/AudioSampleFifo.h
        src/feedback/FeedbackComponent.cpp
        src/feedback/FeedbackComponent.h
        src/feedback/FeedbackInvitationPolicy.h
        src/app/AppDefaults.h
        src/app/AppWindows.h
        src/app/MainComponent.cpp
        src/app/MainComponent.h
        src/app/MainTitleBar.h
        src/app/MicrophoneWarning.h
        src/app/ThemeType.h
        src/spectrogram/SpectrogramComponent.cpp
        src/spectrogram/SpectrogramComponent.h
        src/tuner/TunerComponent.cpp
        src/tuner/TunerDrawing.cpp
        src/tuner/TunerComponent.h
        src/app/Theme.h
        src/Theme.cpp
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

if(BUILD_TESTING)
    add_executable(PracticeTakesTests
        tests/AppDefaultsTests.cpp
        tests/AudioSampleFifoTests.cpp
        tests/FeedbackInvitationPolicyTests.cpp
    )

    target_include_directories(PracticeTakesTests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    target_link_libraries(PracticeTakesTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    include(Catch)
    catch_discover_tests(PracticeTakesTests)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake")
