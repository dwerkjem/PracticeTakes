include(GNUInstallDirs)

# Keep the installed layout conventional for every native package generator.
# Linux packages install the executable in /usr/bin, Windows installers use
# their private bin directory, and macOS packages use the JUCE .app bundle.
install(TARGETS PracticeTakes
    BUNDLE DESTINATION "."
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/org.derekrneilson.practicetakes.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/practice-takes"
    )
elseif(WIN32)
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        DESTINATION "."
    )
endif()

# Include the MSVC runtime beside the executable so the Windows installer does
# not require the user to download the Visual C++ redistributable separately.
if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    include(InstallRequiredSystemLibraries)
endif()

set(PRACTICE_TAKES_PACKAGE_FILE_NAME "" CACHE STRING
    "Optional native installer file name without its extension")

set(CPACK_PACKAGE_NAME "PracticeTakes")
set(CPACK_PACKAGE_VENDOR "Derek R. Neilson")
set(CPACK_PACKAGE_CONTACT "Derek R. Neilson")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Desktop tools for music practice and live microphone analysis")
set(CPACK_PACKAGE_HOMEPAGE_URL
    "https://github.com/dwerkjem/PracticeTakes")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Practice Takes")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_VERBATIM_VARIABLES YES)

if(PRACTICE_TAKES_PACKAGE_FILE_NAME)
    set(CPACK_PACKAGE_FILE_NAME "${PRACTICE_TAKES_PACKAGE_FILE_NAME}")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_DEBIAN_PACKAGE_NAME "practice-takes")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Derek R. Neilson")
    set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE
        "https://github.com/dwerkjem/PracticeTakes")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

    if(PRACTICE_TAKES_PACKAGE_FILE_NAME)
        set(CPACK_DEBIAN_FILE_NAME
            "${PRACTICE_TAKES_PACKAGE_FILE_NAME}.deb")
    else()
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    endif()
elseif(WIN32)
    # CPack uses this pair to create the Start Menu shortcut. Executables are
    # searched for in bin by default; keep that path explicit for readability.
    set(CPACK_PACKAGE_EXECUTABLES "PracticeTakes" "Practice Takes")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "${CMAKE_INSTALL_BINDIR}")
    set(CPACK_NSIS_DISPLAY_NAME "Practice Takes")
    set(CPACK_NSIS_PACKAGE_NAME "Practice Takes")
    set(CPACK_NSIS_INSTALLED_ICON_NAME
        "${CMAKE_INSTALL_BINDIR}\\\\PracticeTakes.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_UNINSTALL_NAME "Uninstall Practice Takes")
    set(CPACK_NSIS_HELP_LINK
        "https://github.com/dwerkjem/PracticeTakes/issues")
    set(CPACK_NSIS_URL_INFO_ABOUT
        "https://github.com/dwerkjem/PracticeTakes")
    set(CPACK_NSIS_CONTACT "Derek R. Neilson")
endif()

include(CPack)
