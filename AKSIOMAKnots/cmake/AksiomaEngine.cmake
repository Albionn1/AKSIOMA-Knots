# AKSIOMA Engine, the 3D renderer: a project of its own that this app uses.
#
# By default the release this app is made for (AKSIOMA_ENGINE_TAG) is fetched
# from GitHub once and built with it: every build gets the same engine.
#
# To work on the engine and the app together, point at a working copy:
#   1. -DAKSIOMA_ENGINE_DIR=<folder>  (CMake cache), or
#   2. the AKSIOMA_ENGINE_DIR environment variable.
#
# Afterwards the targets Aksioma::Core and Aksioma::Engine exist.
if(TARGET AksiomaCore)
    return()
endif()

set(AKSIOMA_ENGINE_DIR "" CACHE PATH "A working copy of AKSIOMA Engine (empty: the pinned release)")
set(AKSIOMA_ENGINE_TAG "v0.1.1" CACHE STRING "The release of AKSIOMA Engine this app is built with")

# (the engine's examples and tests are its own business)
set(AKSIOMA_ENGINE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(AKSIOMA_ENGINE_TESTS OFF CACHE BOOL "" FORCE)

set(_aksioma_working "${AKSIOMA_ENGINE_DIR}")

if(NOT _aksioma_working AND DEFINED ENV{AKSIOMA_ENGINE_DIR})
    file(TO_CMAKE_PATH "$ENV{AKSIOMA_ENGINE_DIR}" _aksioma_working)
endif()

if(_aksioma_working)
    if(NOT EXISTS "${_aksioma_working}/CMakeLists.txt" OR NOT EXISTS "${_aksioma_working}/include/aksioma")
        message(FATAL_ERROR "AKSIOMA_ENGINE_DIR is ${_aksioma_working}, but AKSIOMA Engine is not there.")
    endif()

    message(STATUS "AKSIOMA Engine: the working copy in ${_aksioma_working}")
    add_subdirectory("${_aksioma_working}" "${CMAKE_BINARY_DIR}/aksioma_engine")
else()
    message(STATUS "AKSIOMA Engine: release ${AKSIOMA_ENGINE_TAG}")

    include(FetchContent)

    FetchContent_Declare(aksioma_engine
        GIT_REPOSITORY https://github.com/Albionn1/AKSIOMA-Engine.git
        GIT_TAG ${AKSIOMA_ENGINE_TAG}
        GIT_SHALLOW TRUE)

    FetchContent_MakeAvailable(aksioma_engine)
endif()
