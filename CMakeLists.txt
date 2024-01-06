cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# stop in-source builds; force the use of a build directory
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

project(EyeTrackVR-Overlay)

# add source files and libraries
add_subdirectory(${PROJECT_SOURCE_DIR}/external)
file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.h)
file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)
include_directories(${OPENVR_INCLUDE_DIR} ${PROJECT_SOURCE_DIR}/src)

add_executable(EyeTrackVR-Overlay ${SOURCES})
target_link_libraries(EyeTrackVR-Overlay Boost::boost)
target_link_libraries(EyeTrackVR-Overlay ${OPENVR_LIBRARIES})

# copy resources to build directory
add_custom_command(TARGET EyeTrackVR-Overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENVR_RUNTIME}" $<TARGET_FILE_DIR:EyeTrackVR-Overlay>
)
add_custom_command(TARGET EyeTrackVR-Overlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/src/assets" $<TARGET_FILE_DIR:EyeTrackVR-Overlay>/assets
)