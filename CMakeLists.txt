cmake_minimum_required(VERSION 3.14)
project(simonsays CXX)

set(CMAKE_CXX_STANDARD 17)

# RealSense ID SDK path (override with -DRSID_SDK_PATH=...)
set(RSID_SDK_PATH "C:/Users/cmatthie/Documents/SDK_2.7.3.0701_471615c_Standard" CACHE PATH "RealSense ID SDK root")
if(NOT EXISTS "${RSID_SDK_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "RealSense ID SDK not found at ${RSID_SDK_PATH}. Set RSID_SDK_PATH to the SDK root.")
endif()

# Build the SDK library (no samples, no tools)
set(RSID_SAMPLES OFF CACHE BOOL "" FORCE)
set(RSID_TOOLS OFF CACHE BOOL "" FORCE)
set(RSID_MASTER_PROJECT OFF)
add_subdirectory(${RSID_SDK_PATH} ${CMAKE_BINARY_DIR}/rsid_build)

# SDL2 for stick man window
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_path(SDL2_INCLUDE_DIR SDL.h
        HINTS ENV SDL2DIR
        PATH_SUFFIXES include/SDL2 include)
    find_library(SDL2_LIBRARY SDL2)
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
        set(SDL2_LIBRARIES ${SDL2_LIBRARY})
    endif()
endif()

if(NOT SDL2_FOUND)
    message(WARNING "SDL2 not found. Build will create a console-only version (no stick man window).")
    add_executable(simonsays src/main.cpp)
    target_compile_definitions(simonsays PRIVATE SIMONSAYS_NO_SDL)
else()
    add_executable(simonsays src/main.cpp)
    target_include_directories(simonsays PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(simonsays PRIVATE ${SDL2_LIBRARIES})
endif()

target_include_directories(simonsays PRIVATE ${RSID_SDK_PATH}/include)
target_link_libraries(simonsays PRIVATE rsid)

# Copy SDL2 DLL to output on Windows if found
if(WIN32 AND SDL2_FOUND)
    get_target_property(_sdl2_loc ${SDL2_LIBRARIES} LOCATION)
    if(_sdl2_loc)
        get_filename_component(_sdl2_dir ${_sdl2_loc} DIRECTORY)
        file(GLOB _sdl2_dll "${_sdl2_dir}/../bin/SDL2.dll" "${_sdl2_dir}/SDL2.dll" "${_sdl2_dir}/*/SDL2.dll")
        if(_sdl2_dll)
            add_custom_command(TARGET simonsays POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sdl2_dll} $<TARGET_FILE_DIR:simonsays>)
        endif()
    endif()
endif()
