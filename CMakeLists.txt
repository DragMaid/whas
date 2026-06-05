cmake_minimum_required(VERSION 4.0)
project(whas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Raylib dependency
include(FetchContent)
FetchContent_Declare(
    raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
)
FetchContent_MakeAvailable(raylib)

set(SOURCES
    src/element/base/factory.cpp
    src/element/base/properties.cpp
    src/element/base/registry.cpp

    src/world/chunk_manager.cpp
    src/world/grid.cpp

    src/physics/movement_system.cpp
    src/physics/erosion_system.cpp
    src/physics/heat_system.cpp
    src/physics/pressure_system.cpp
    src/physics/fluid_movement_system.cpp

    src/element/fire.cpp
    src/element/water.cpp
    src/element/earth.cpp
    src/element/steam.cpp
    src/element/cloud.cpp
    src/element/ice.cpp

    src/engine/simulation.cpp
    src/engine/renderer.cpp

    src/ui/ui.cpp
)

add_library(whas ${SOURCES})
target_compile_features(whas PUBLIC cxx_std_20)
target_include_directories(whas
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

add_executable(whasg src/main.cpp)
target_link_libraries(whasg PRIVATE whas)
target_link_libraries(whasg PRIVATE raylib)

install(TARGETS whasg DESTINATION bin)
