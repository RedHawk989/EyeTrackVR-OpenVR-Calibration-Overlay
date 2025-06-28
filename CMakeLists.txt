cmake_minimum_required(VERSION 3.15)
project(EyeTrackVR-Overlay LANGUAGES CXX) # Always specify LANGUAGES for clarity and specific CXX features

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Good practice to disable GNU extensions unless specifically needed

# vcpkg toolchain
# This line is usually passed on the CMake command line.
# If you keep it here, it will override any command-line setting.
# set(CMAKE_TOOLCHAIN_FILE "C:/Users/beaul/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

# --- Removed manual include_directories and link_directories for vcpkg packages ---
# find_package(cppzmq CONFIG REQUIRED) will handle these via imported targets.

# Set OpenVR manually (vendored library)
set(OPENVR_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/external/openvr/headers")
set(OPENVR_LIBRARIES "${PROJECT_SOURCE_DIR}/external/openvr/lib/win64/openvr_api.lib")
set(OPENVR_RUNTIME "${PROJECT_SOURCE_DIR}/external/openvr/bin/win64/openvr_api.dll")

# Find cppzmq (and implicitly zeromq via vcpkg)
find_package(cppzmq CONFIG REQUIRED)

if(cppzmq_FOUND)
    message(STATUS "Found cppzmq.")
else()
    message(FATAL_ERROR "cppzmq not found. Ensure it's installed via vcpkg.")
endif()

# Source files (using target_sources is often preferred over GLOB_RECURSE for larger projects)
file(GLOB_RECURSE SOURCES
    "${PROJECT_SOURCE_DIR}/src/*.h"
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

add_executable(EyeTrackVR-Overlay ${SOURCES})

# Target-specific include directories (more precise than global include_directories)
target_include_directories(EyeTrackVR-Overlay PRIVATE
    ${PROJECT_SOURCE_DIR}/src # For your project's headers
    ${OPENVR_INCLUDE_DIR}     # For OpenVR headers
)

target_link_libraries(EyeTrackVR-Overlay PRIVATE
    ${OPENVR_LIBRARIES} # Link the OpenVR static library
    cppzmq              # Link cppzmq (vcpkg provides an imported target for this)
)

# Post-build commands for copying runtime dependencies and assets
add_custom_command(TARGET EyeTrackVR-Overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENVR_RUNTIME}" $<TARGET_FILE_DIR:EyeTrackVR-Overlay>
)

add_custom_command(TARGET EyeTrackVR-Overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/src/assets" $<TARGET_FILE_DIR:EyeTrackVR-Overlay>/assets
)