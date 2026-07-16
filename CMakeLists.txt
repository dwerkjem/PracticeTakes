cmake_minimum_required(VERSION 3.25)

project(
    PracticeTakes
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep JUCE's optional browser/network dependencies out of this tuner.
# These directory-level definitions also apply while JUCE builds juceaide.
add_compile_definitions(
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_XCURSOR=0
)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(PracticeTakes
    PRODUCT_NAME "Practice Takes"
    COMPANY_NAME "Derek R. Neilson"
    BUNDLE_ID "org.derekrneilson.practicetakes"
)

juce_generate_juce_header(PracticeTakes)

target_sources(PracticeTakes
    PRIVATE
        src/main.cpp
        src/TunerComponent.cpp
        src/TunerComponent.h
)

target_compile_definitions(PracticeTakes
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(PracticeTakes
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
