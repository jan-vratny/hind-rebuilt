cmake_minimum_required(VERSION 3.16)
project(hind LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED CONFIG)
find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)

set(WARNINGS -Wall -Wextra -Wno-missing-field-initializers)

# Simulation core: no SDL/GL dependency, shared by the game and the tests.
add_library(hind_core STATIC
  src/world/terrain.cpp
  src/sim/heli.cpp
  src/sim/weapons.cpp
  src/sim/targets.cpp
  src/sim/wso.cpp
  src/sim/wing_arms.cpp
  src/sim/threats.cpp
  src/sim/enemy.cpp
  src/sim/battle.cpp
  src/sim/aircombat.cpp
  src/sim/spotter.cpp
  src/sim/mission.cpp
  src/sim/pilot_log.cpp
  src/sim/topten.cpp
  src/sim/orders.cpp
  src/sim/troops.cpp
  src/sim/debris.cpp
  src/sim/bowman.cpp
  src/gamedata/original.cpp
  src/gamedata/anim.cpp
  src/gamedata/cd.cpp
  src/gamedata/install.cpp
  src/gamedata/campaign.cpp
  src/platform/config.cpp
  src/gamedata/picture.cpp
  src/gamedata/zone.cpp
  src/gamedata/zonemap.cpp
  src/gamedata/mission.cpp
  src/gamedata/sound.cpp
  src/render/daylight.cpp
  src/net/link.cpp
  src/net/mirror.cpp
)
target_include_directories(hind_core PUBLIC src ${GLM_INCLUDE_DIR})
# The remake's own data (its missions), found from the build tree as a last resort.
target_compile_definitions(hind_core PUBLIC HIND_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
target_compile_options(hind_core PRIVATE ${WARNINGS})
if(WIN32)
  # Winsock for the network game.
  target_link_libraries(hind_core PUBLIC ws2_32)
endif()

add_executable(hind
  src/main.cpp
  src/platform/input.cpp
  src/platform/joystick.cpp
  src/render/gl.cpp
  src/render/glcore.cpp
  src/render/mesh.cpp
  src/render/shadow.cpp
  src/render/livery.cpp
  src/render/recognition.cpp
  src/render/terrain_renderer.cpp
  src/render/sky.cpp
  src/render/hud.cpp
  src/render/models.cpp
  src/render/drones.cpp
  src/render/camera.cpp
  src/render/cockpit.cpp
  src/render/font.cpp
  src/render/overlay.cpp
  src/render/screenpass.cpp
  src/render/gauges.cpp
  src/render/effects.cpp
  src/render/vehicles.cpp
  src/render/original_mi24.cpp
  src/render/figures.cpp
  src/render/bow.cpp
  src/render/canvas.cpp
  src/menu/planner.cpp
  src/menu/raster.cpp
  src/menu/frontend.cpp
  src/menu/setup.cpp
  src/platform/filedialog.cpp
  src/audio/mixer.cpp
  src/audio/game_audio.cpp
)
target_include_directories(hind PRIVATE src)
target_link_libraries(hind PRIVATE hind_core SDL2::SDL2)
target_compile_options(hind PRIVATE ${WARNINGS})
if(WIN32)
  # The .exe's icon.
  enable_language(RC)
  target_sources(hind PRIVATE src/hind.rc)
  # Windows: SDL's WinMain, the C++ runtime linked in (only SDL2.dll goes with the game), and
  # SDL2.dll next to hind.exe.
  target_link_libraries(hind PRIVATE SDL2::SDL2main SDL2::SDL2)  # main before the library it calls
  # The shell's file dialog (the first start's Browse).
  target_link_libraries(hind PRIVATE ole32 uuid)
  target_link_options(hind PRIVATE -static)
  get_target_property(SDL2_DLL SDL2::SDL2 IMPORTED_LOCATION)
  if(SDL2_DLL)
    add_custom_command(TARGET hind POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL2_DLL} $<TARGET_FILE_DIR:hind>)
  endif()
endif()

enable_testing()
add_executable(hind_tests tests/test_flight.cpp)
target_link_libraries(hind_tests PRIVATE hind_core)
if(WIN32)
  target_link_options(hind_tests PRIVATE -static)
endif()
target_compile_options(hind_tests PRIVATE ${WARNINGS})
add_test(NAME flight_model COMMAND hind_tests)
