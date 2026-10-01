cmake_minimum_required(VERSION 4.0)
project(whas VERSION 0.1.0 LANGUAGES C CXX)

# An unset build type means no optimisation at all, which makes the sand
# simulation roughly 4x slower than a frame allows
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# No C++20 modules here; the dependency scan trips an assertion in ninja 1.13
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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
    # Pinned: main renames APIs (b2Body_ApplyMassFromShapes -> UpdateMassFromShapes)
    GIT_TAG 956ce4e1e8acddd05a21102629e268ff9321ec50
)
FetchContent_MakeAvailable(box2d)

FetchContent_Declare(
    nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG master
)
FetchContent_MakeAvailable(nanosvg)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# WebSocket + HTTP client for online play. TLS (OpenSSL) so the client can
# reach a server behind a TLS proxy or tunnel (wss://, e.g. ngrok); the
# server itself speaks plain ws://.
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v12.0.1
)
FetchContent_MakeAvailable(ixwebsocket)

set(SOURCES
    src/core/bytes.cpp
    src/core/config_json.cpp
    src/engine/snapshot.cpp
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
    src/physics/particle_system.cpp

    src/element/fire.cpp
    src/element/water.cpp
    src/element/earth.cpp
    src/element/steam.cpp
    src/element/cloud.cpp
    src/element/ice.cpp
    src/element/sand.cpp
    src/element/burning.cpp
    src/element/smoke.cpp

    src/audio/audio_manager.cpp
    src/audio/audio_observer.cpp
    src/audio/synths/sand_synth.cpp
    src/audio/synths/wind_synth.cpp
    src/audio/synths/fire_synth.cpp
    src/audio/synths/earth_synth.cpp
    src/audio/synths/water_synth.cpp
    src/audio/synths/light_synth.cpp
    src/audio/synths/spell_synth.cpp
    src/audio/synths/break_synth.cpp
    src/audio/synths/flight_synth.cpp

    src/engine/simulation.cpp
    src/engine/renderer.cpp
    src/engine/view.cpp

    src/ui/ui.cpp
    src/ui/theme.cpp
    src/ui/widgets.cpp
    src/ui/spell_thumbnails.cpp
    src/ui/action_bar.cpp
    src/ui/play_menu.cpp
    src/ui/map_thumbnails.cpp
    src/ui/map_gallery.cpp
    src/ui/map_editor.cpp
    src/ui/grimoire_panel.cpp
    src/ui/audio_settings.cpp

    src/spell/spell_geometry.cpp
    src/spell/svg_library.cpp
    src/spell/spell_store.cpp
    src/spell/spell_json.cpp
    src/spell/spell_shapes.cpp
    src/spell/spell_editor.cpp
    src/spell/glyph_docs.cpp
    src/spell/spell_editor_library.cpp
    src/spell/spell_system.cpp
    src/spell/spell_quant.cpp
    src/spell/spell_library.cpp
    src/spell/spell_rules.cpp
    src/spell/deck_store.cpp

    src/game/character.cpp
    src/game/turn_controller.cpp
    src/game/match.cpp
    src/game/arena_gen.cpp
    src/game/map.cpp
    src/game/character_draw.cpp
    src/game/sandbox.cpp
    src/game/flight_trail.cpp
    src/game/replay.cpp
    src/game/replay_store.cpp
    src/game/replay_view.cpp

    src/net/plan_codec.cpp
    src/net/net_client.cpp
    src/net/lockstep_client.cpp
    src/core/sha256.cpp
    src/game/game.cpp
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

# Lockstep multiplayer needs every client to compute the exact same floats:
# no fused multiply-add contraction, no fast-math reassociation.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(WHAS_DETERMINISTIC_FP -ffp-contract=off -fno-fast-math)
elseif(MSVC)
    set(WHAS_DETERMINISTIC_FP /fp:precise)
endif()
target_compile_options(box2d PRIVATE ${WHAS_DETERMINISTIC_FP})

# Identifies the simulation build; clients with different ids can't play
# together because their floats may not match
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE WHAS_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
set(WHAS_SIM_BUILD_ID "${WHAS_GIT_HASH}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_BUILD_TYPE}")

add_library(whas ${SOURCES})
target_compile_options(whas PUBLIC ${WHAS_DETERMINISTIC_FP})
target_compile_definitions(whas PUBLIC WHAS_SIM_BUILD_ID="${WHAS_SIM_BUILD_ID}")
target_compile_features(whas PUBLIC cxx_std_20)
target_include_directories(whas
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${IMGUI_DIR}
    ${RLIMGUI_DIR}
    ${nanosvg_SOURCE_DIR}/src
)
target_link_libraries(whas PUBLIC box2d nanosvg nlohmann_json::nlohmann_json ixwebsocket)

add_executable(whasg src/main.cpp)
# target_link_libraries(whasg PRIVATE whas)
# target_link_libraries(whasg PRIVATE raylib rlimgui imgui)
target_link_libraries(whasg PRIVATE whas raylib rlimgui imgui box2d)

install(TARGETS whasg DESTINATION bin)

# Headless tests (no window): determinism, spells, burning
option(WHAS_BUILD_TESTS "Build the whas_tests target" ON)
if(WHAS_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(whas_tests
        tests/determinism_test.cpp
        tests/burning_test.cpp
        tests/match_test.cpp
        tests/spell_golden_test.cpp
        tests/spell_modifier_test.cpp
        tests/net_test.cpp
        tests/replay_test.cpp
        tests/bench_test.cpp
        tests/audio_test.cpp
        tests/map_test.cpp
    )
    target_link_libraries(whas_tests PRIVATE whas raylib box2d Catch2::Catch2WithMain)
    target_compile_definitions(whas_tests PRIVATE WHAS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()
