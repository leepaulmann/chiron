cmake_minimum_required(VERSION 3.20)
project(chiron C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Chiron hosts 32-bit Windows code and must be built for i686 (use `nix develop`).")
endif()

add_compile_definitions(_GNU_SOURCE _FILE_OFFSET_BITS=64 _TIME_BITS=64)
# Windows code only keeps the stack 4-byte aligned, so every function we
# expose to it must realign before using SSE spills. Signed arithmetic wraps,
# as it does in the game code that reimplementations must match.
add_compile_options(-Wall -Wno-unused-parameter -mincoming-stack-boundary=2 -fno-strict-aliasing
                    -fno-omit-frame-pointer -fwrapv)
add_link_options(-no-pie)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

# Each shim DLL's export table is generated from its WINAPI functions.
function(shim_exports symbol dll)
  set(out ${CMAKE_BINARY_DIR}/gen/${symbol}.c)
  list(TRANSFORM ARGN PREPEND ${CMAKE_SOURCE_DIR}/)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
    COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/gen_exports.py ${symbol} ${dll} ${out} ${ARGN}
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/gen_exports.py ${ARGN})
  set(SHIM_EXPORT_SOURCES ${SHIM_EXPORT_SOURCES} ${out} PARENT_SCOPE)
endfunction()

set(KERNEL32_SOURCES src/win32/kernel32_proc.c src/win32/kernel32_file.c src/win32/seh.c)
set(USER32_SOURCES src/win32/user32_window.c src/win32/user32_msg.c src/win32/user32_misc.c src/win32/user32_listbox.c)
set(GDI32_SOURCES src/win32/gdi32_obj.c src/win32/gdi32_draw.c src/win32/gdi32_text.c)
shim_exports(kernel32_exports KERNEL32.dll ${KERNEL32_SOURCES})
shim_exports(user32_exports USER32.dll ${USER32_SOURCES})
shim_exports(gdi32_exports GDI32.dll ${GDI32_SOURCES})
shim_exports(advapi32_exports ADVAPI32.dll src/win32/advapi32.c)
shim_exports(winmm_exports WINMM.dll src/win32/winmm.c)
shim_exports(dsound_exports DSOUND.dll src/win32/dsound.c)
shim_exports(ole32_exports ole32.dll src/win32/ole32.c)
shim_exports(dplayx_exports DPLAYX.dll src/win32/dplayx.c)
shim_exports(ddraw_exports DDRAW.dll src/win32/ddraw.c)

add_executable(chiron
  src/main.c
  src/install/install.c
  src/engine/base.c
  src/engine/code.c
  src/engine/game.c
  src/engine/hooks.c
  src/engine/map.c src/engine/mapgen.c
  src/engine/patches.c
  src/engine/production.c
  src/engine/quiet.c
  src/engine/upkeep.c
  src/engine/soak.c
  src/engine/social.c
  src/engine/faction.c
  src/engine/diplomacy.c
  src/engine/strategy.c
  src/engine/build.c
  src/engine/veh.c
  src/engine/score.c
  src/engine/tech.c
  src/engine/spot.c
  src/engine/council.c
  src/engine/path.c
  src/engine/palette.c src/engine/buffer.c
  src/engine/text.c
  src/engine/sprite.c
  src/engine/texture.c
  src/engine/bufobj.c
  src/engine/move.c
  src/engine/events.c
  src/engine/combat.c
  src/engine/ai.c
  src/engine/setup.c
  src/engine/probe.c
  src/engine/save.c
  src/engine/rules.c
  src/loader/pe.c
  src/loader/teb.c
  src/loader/thunks.c
  src/loader/modules.c
  src/win32/registry.c
  src/win32/path.c
  src/win32/handles.c
  src/win32/sdl_host.c
  src/win32/advapi32.c
  src/win32/winmm.c
  src/win32/dsound.c
  src/win32/ole32.c
  src/win32/dplayx.c
  src/win32/ddraw.c
  ${KERNEL32_SOURCES}
  ${USER32_SOURCES}
  ${GDI32_SOURCES}
  ${SHIM_EXPORT_SOURCES})
target_include_directories(chiron PRIVATE src/win32)
target_link_libraries(chiron PRIVATE Threads::Threads PkgConfig::SDL2 PkgConfig::FREETYPE PkgConfig::FONTCONFIG m)

enable_testing()
add_subdirectory(tests)
