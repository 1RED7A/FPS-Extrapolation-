cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optional but recommended for Geode mods
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(FPSExtrapolation VERSION 1.0.0)

# Add all .cpp files inside src/ (works even if you add more files later)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable.")
else()
    message(STATUS "Found Geode SDK: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Link and create the .geode file (this is the modern/recommended way)
setup_geode_mod(${PROJECT_NAME})

# Android-specific fix (recommended)
if (ANDROID)
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-z,max-page-size=4096")
endif()
