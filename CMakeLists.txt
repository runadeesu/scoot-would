cmake_minimum_required(VERSION 3.24)

project(scoot_would
    VERSION 1.0.0
    DESCRIPTION "scoot would - freestyle scooter game on a custom C++ runtime"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CONFIGURATION_TYPES Debug RelWithDebInfo Release CACHE STRING "" FORCE)

option(SCOOT_BUILD_TESTS "Build unit tests" ON)
option(SCOOT_BUILD_TOOLS "Build asset tools (host only)" ON)
option(SCOOT_DEV_FEATURES "Developer console, editor and debug overlay available (toggle in game)" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Dependencies)

add_subdirectory(src)

if(SCOOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SCOOT_BUILD_TOOLS AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tools)
endif()
