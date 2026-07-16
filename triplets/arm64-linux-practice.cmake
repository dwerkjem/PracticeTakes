set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# JUCE loads X11 by SONAME at runtime, so this triplet builds shared libraries.
# The vcpkg X11 ports are empty system-library placeholders on Linux unless
# this is enabled. Practice Takes intentionally manages them through vcpkg.
set(X_VCPKG_FORCE_VCPKG_X_LIBRARIES ON)
