set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# The vcpkg X11 ports are empty system-library placeholders on Linux unless
# this is enabled. Practice Takes intentionally manages these headers and
# libraries through vcpkg.
set(X_VCPKG_FORCE_VCPKG_X_LIBRARIES ON)
