# JUCE 8.0.12 starts capture-only ALSA devices from the first read, but its
# worker waits for data before issuing that read. Explicitly start a prepared
# capture stream so the initial wait does not consume its two-second timeout.
function(practice_takes_patch_juce_alsa_input_startup juce_source_directory)
    set(alsa_file
        "${juce_source_directory}/modules/juce_audio_devices/native/juce_ALSA_linux.cpp")

    if(NOT EXISTS "${alsa_file}")
        message(FATAL_ERROR "Could not find JUCE ALSA backend at ${alsa_file}")
    endif()

    file(READ "${alsa_file}" alsa_source)
    string(REPLACE "\r\n" "\n" alsa_source "${alsa_source}")

    set(original_code [=[
        if (outputDevice != nullptr && JUCE_ALSA_FAILED (snd_pcm_prepare (outputDevice->handle)))
            return;

        startThread (Priority::high);
]=])

    set(replacement_code [=[
        if (outputDevice != nullptr && JUCE_ALSA_FAILED (snd_pcm_prepare (outputDevice->handle)))
            return;

        if (inputDevice != nullptr && outputDevice == nullptr
            && JUCE_ALSA_FAILED (snd_pcm_start (inputDevice->handle)))
            return;

        startThread (Priority::high);
]=])

    string(FIND "${alsa_source}" "${replacement_code}" replacement_position)

    if(NOT replacement_position EQUAL -1)
        return()
    endif()

    string(FIND "${alsa_source}" "${original_code}" original_position)

    if(original_position EQUAL -1)
        message(FATAL_ERROR
            "JUCE's ALSA startup sequence changed; the capture startup patch must be reviewed.")
    endif()

    string(REPLACE "${original_code}" "${replacement_code}"
        patched_source "${alsa_source}")
    file(WRITE "${alsa_file}" "${patched_source}")

    message(STATUS "Applied JUCE ALSA capture startup patch")
endfunction()
