# Score playback (openspec/changes/archive/2026-11-08-score-playback): TinySoundFont, the player,
# and GeneralUser GS, the sound. See tools/packaging/sounds/README.md.
#
# Each file is downloaded from its exact commit and checked against its SHA-256,
# rather than cloned. Neither repository tags its releases, and a shallow clone
# can only fetch a tagged tip. GeneralUser's history also holds every 32 MB
# version of the bank. A file already present with the right hash is not
# downloaded again.
set(PRACTICE_TAKES_TSF_COMMIT 853a0a171759f1ddba0de1442133a75912bbeffa)
set(PRACTICE_TAKES_SOUNDFONT_COMMIT 684543d5e5efaef08d02be50dcda8d552478fa60) # v2.0.3

set(PRACTICE_TAKES_PLAYBACK_DIR "${CMAKE_BINARY_DIR}/_deps/score-playback")
set(PRACTICE_TAKES_TSF_INCLUDE_DIR "${PRACTICE_TAKES_PLAYBACK_DIR}/tinysoundfont")
set(PRACTICE_TAKES_SOUNDFONT_NAME "GeneralUser-GS.sf2")
set(PRACTICE_TAKES_SOUNDFONT_FILE
    "${PRACTICE_TAKES_PLAYBACK_DIR}/sounds/${PRACTICE_TAKES_SOUNDFONT_NAME}")
set(PRACTICE_TAKES_SOUNDFONT_LICENSE
    "${PRACTICE_TAKES_PLAYBACK_DIR}/sounds/GeneralUser-GS-LICENSE.txt")
set(PRACTICE_TAKES_TSF_LICENSE
    "${PRACTICE_TAKES_PLAYBACK_DIR}/sounds/TinySoundFont-LICENSE.txt")

function(practice_takes_download url destination sha256)
    if(EXISTS "${destination}")
        file(SHA256 "${destination}" present)
        if(present STREQUAL sha256)
            return()
        endif()
    endif()

    message(STATUS "Downloading ${url}")
    file(DOWNLOAD "${url}" "${destination}"
        EXPECTED_HASH SHA256=${sha256}
        TLS_VERIFY ON
        STATUS status
    )
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 reason)
        message(FATAL_ERROR "Could not download ${url}: ${reason}")
    endif()
endfunction()

set(tsf_base
    "https://raw.githubusercontent.com/schellingb/TinySoundFont/${PRACTICE_TAKES_TSF_COMMIT}")
set(soundfont_base
    "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${PRACTICE_TAKES_SOUNDFONT_COMMIT}")

practice_takes_download("${tsf_base}/tsf.h"
    "${PRACTICE_TAKES_TSF_INCLUDE_DIR}/tsf.h"
    70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197)
practice_takes_download("${tsf_base}/LICENSE"
    "${PRACTICE_TAKES_TSF_LICENSE}"
    ef10cb839de73fb6d5d32ea7151979e87809a6df70dc7e7ee8457ae1c9e7aa7e)
practice_takes_download("${soundfont_base}/${PRACTICE_TAKES_SOUNDFONT_NAME}"
    "${PRACTICE_TAKES_SOUNDFONT_FILE}"
    9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe)
practice_takes_download("${soundfont_base}/documentation/LICENSE.txt"
    "${PRACTICE_TAKES_SOUNDFONT_LICENSE}"
    7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be)

# What travels with the application: the bank and both licences.
set(PRACTICE_TAKES_SOUND_FILES
    "${PRACTICE_TAKES_SOUNDFONT_FILE}"
    "${PRACTICE_TAKES_SOUNDFONT_LICENSE}"
    "${PRACTICE_TAKES_TSF_LICENSE}"
)
