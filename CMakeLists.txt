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

# ImGui and rlImGui dependencies
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    rlImGui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG main
)
FetchContent_MakeAvailable(rlImGui)

FetchContent_Declare(
    box2d
    GIT_REPOSITORY https://github.com/erincatto/box2d.git
    GIT_TAG main
)
FetchContent_MakeAvailable(box2d)

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
    src/physics/rigid_body_system.cpp
    src/physics/geometry_utils.cpp

    src/element/fire.cpp
    src/element/water.cpp
    src/element/earth.cpp
    src/element/steam.cpp
    src/element/cloud.cpp
    src/element/ice.cpp
    src/element/sand.cpp

    src/engine/simulation.cpp
    src/engine/renderer.cpp

    src/ui/ui.cpp
)

set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(RLIMGUI_DIR ${rlimgui_SOURCE_DIR})

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

add_library(rlimgui STATIC ${RLIMGUI_DIR}/rlImGui.cpp)
target_include_directories(rlimgui PUBLIC ${RLIMGUI_DIR})
target_link_libraries(rlimgui PUBLIC imgui raylib)

add_library(whas ${SOURCES})
target_compile_features(whas PUBLIC cxx_std_20)
target_include_directories(whas
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${IMGUI_DIR}
    ${RLIMGUI_DIR}
)
target_link_libraries(whas PUBLIC box2d)

add_executable(whasg src/main.cpp)
# target_link_libraries(whasg PRIVATE whas)
# target_link_libraries(whasg PRIVATE raylib rlimgui imgui)
target_link_libraries(whasg PRIVATE whas raylib rlimgui imgui box2d)

install(TARGETS whasg DESTINATION bin)
