# Reimplemented game functions, tested on made-up data.
set(ENGINE_SOURCES ${CMAKE_SOURCE_DIR}/src/engine/base.c ${CMAKE_SOURCE_DIR}/src/engine/tech.c ${CMAKE_SOURCE_DIR}/src/engine/map.c ${CMAKE_SOURCE_DIR}/src/engine/mapgen.c ${CMAKE_SOURCE_DIR}/src/engine/quiet.c ${CMAKE_SOURCE_DIR}/src/engine/production.c ${CMAKE_SOURCE_DIR}/src/engine/upkeep.c ${CMAKE_SOURCE_DIR}/src/engine/social.c ${CMAKE_SOURCE_DIR}/src/engine/faction.c ${CMAKE_SOURCE_DIR}/src/engine/strategy.c ${CMAKE_SOURCE_DIR}/src/engine/build.c ${CMAKE_SOURCE_DIR}/src/engine/veh.c ${CMAKE_SOURCE_DIR}/src/engine/spot.c ${CMAKE_SOURCE_DIR}/src/engine/council.c ${CMAKE_SOURCE_DIR}/src/engine/path.c ${CMAKE_SOURCE_DIR}/src/engine/score.c ${CMAKE_SOURCE_DIR}/src/engine/move.c ${CMAKE_SOURCE_DIR}/src/engine/combat.c ${CMAKE_SOURCE_DIR}/src/engine/events.c ${CMAKE_SOURCE_DIR}/src/engine/ai.c ${CMAKE_SOURCE_DIR}/src/engine/setup.c
    ${CMAKE_SOURCE_DIR}/src/engine/hooks.c ${CMAKE_SOURCE_DIR}/src/engine/code.c ${CMAKE_SOURCE_DIR}/src/engine/game.c)
foreach(name tech map mapgen base social faction strategy build veh spot council path score move combat events ai setup hooks)
  add_executable(engine_${name} engine_${name}.c ${ENGINE_SOURCES})
  add_test(NAME engine_${name} COMMAND engine_${name})
endforeach()

# Loader tests use a small Win32 program built with mingw, so they run without
# any game files.
find_program(MINGW_CC i686-w64-mingw32-gcc)
if(NOT MINGW_CC)
  message(STATUS "i686-w64-mingw32-gcc not found; loader tests disabled")
  return()
endif()

# Each test program is a freestanding Win32 executable linked only against
# kernel32, like the game.
set(TEST_PES)
foreach(name hello seh overlay)
  set(exe ${CMAKE_CURRENT_BINARY_DIR}/${name}.exe)
  add_custom_command(
    OUTPUT ${exe}
    COMMAND ${MINGW_CC} -O1 -ffreestanding -nostdlib -fno-stack-protector -fno-builtin
            -Wl,--entry=_start@0 -Wl,--dynamicbase -Wl,--subsystem,windows
            -o ${exe} ${CMAKE_CURRENT_SOURCE_DIR}/pe/${name}.c -lkernel32
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pe/${name}.c)
  list(APPEND TEST_PES ${exe})
endforeach()
add_custom_target(test_pe ALL DEPENDS ${TEST_PES})

foreach(mode preferred relocated)
  add_test(NAME loader_${mode}
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_hello.sh $<TARGET_FILE:chiron>
                   ${CMAKE_CURRENT_BINARY_DIR}/hello.exe ${mode})
endforeach()
add_test(NAME seh
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_hello.sh $<TARGET_FILE:chiron>
                 ${CMAKE_CURRENT_BINARY_DIR}/seh.exe preferred)
add_test(NAME overlay
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_hello.sh $<TARGET_FILE:chiron>
                 ${CMAKE_CURRENT_BINARY_DIR}/overlay.exe overlay)

# Every shim must pop as many argument bytes as the Windows function it
# replaces; mingw's import libraries record the real sizes.
execute_process(COMMAND ${MINGW_CC} -print-file-name=libkernel32.a OUTPUT_VARIABLE MINGW_KERNEL32
                OUTPUT_STRIP_TRAILING_WHITESPACE)
get_filename_component(MINGW_LIB_DIR ${MINGW_KERNEL32} DIRECTORY)
add_test(NAME stdcall_sizes
         COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/check_stdcall.py $<TARGET_FILE:chiron>
                 ${MINGW_LIB_DIR} ${SHIM_EXPORT_SOURCES})
