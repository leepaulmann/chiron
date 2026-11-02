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
foreach(name hello seh)
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
