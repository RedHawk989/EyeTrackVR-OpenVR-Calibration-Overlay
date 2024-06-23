add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/boost)
find_library(OPENVR_LIBRARIES
    NAMES
        openvr_api
    PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenVR/bin
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenVR/lib
    PATH_SUFFIXES
        win64
        linux64
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED
)


find_file(OPENVR_RUNTIME
    NAMES
        openvr_api.dll
        libopenvr_api.so
    PATHS
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenVR/bin
        ${CMAKE_CURRENT_SOURCE_DIR}/OpenVR/lib
    PATH_SUFFIXES
        win64
        linux64
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED
)

set(
    OPENVR_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/OpenVR/headers
    CACHE
    PATH
    "OpenVR include directory"
)

message(STATUS "OpenVR runtime: ${OPENVR_RUNTIME}")
message(STATUS "OpenVR library: ${OPENVR_LIBRARIES}")
message(STATUS "OpenVR include: ${OPENVR_INCLUDE_DIR}")