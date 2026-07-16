# JUCE 8.0.12's CallbackMaxSizeEnforcer assumes that the channel count
# reported by AudioIODevice::getActive*Channels() always matches the channel
# pointer count supplied by the backend. ALSA can violate that assumption when
# a device has a larger minimum hardware channel count than the selected input.
function(practice_takes_patch_juce_alsa_channel_mismatch juce_source_directory)
    set(manager_file
        "${juce_source_directory}/modules/juce_audio_devices/audio_io/juce_AudioDeviceManager.cpp")

    if(NOT EXISTS "${manager_file}")
        message(FATAL_ERROR "Could not find JUCE AudioDeviceManager.cpp at ${manager_file}")
    endif()

    file(READ "${manager_file}" manager_source)

    set(original_code [=[
        jassert ((int) storedInputChannels.size()  == numInputChannels);
        jassert ((int) storedOutputChannels.size() == numOutputChannels);
]=])

    set(replacement_code [=[
        // Some ALSA devices report a minimum hardware channel count that
        // differs from the pointer count delivered to the callback. Keep the
        // scratch arrays aligned with the callback to avoid out-of-bounds
        // transforms and null channel pointers.
        if ((int) storedInputChannels.size() != numInputChannels)
            storedInputChannels.resize ((size_t) numInputChannels);

        if ((int) storedOutputChannels.size() != numOutputChannels)
            storedOutputChannels.resize ((size_t) numOutputChannels);
]=])

    string(FIND "${manager_source}" "${replacement_code}" replacement_position)

    if(NOT replacement_position EQUAL -1)
        return()
    endif()

    string(FIND "${manager_source}" "${original_code}" original_position)

    if(original_position EQUAL -1)
        message(FATAL_ERROR
            "JUCE's callback wrapper changed; the ALSA channel mismatch patch must be reviewed.")
    endif()

    string(REPLACE "${original_code}" "${replacement_code}"
        patched_source "${manager_source}")
    file(WRITE "${manager_file}" "${patched_source}")

    message(STATUS "Applied JUCE ALSA callback channel-count guard")
endfunction()
