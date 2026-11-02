# Loader tests use a small Win32 program built with mingw, so they run without
# any game files.
find_program(MINGW_CC i686-w64-mingw32-gcc)
if(NOT MINGW_CC)
  message(STATUS "i686-w64-mingw32-gcc not found; loader tests disabled")
  return()
endif()

set(HELLO_EXE ${CMAKE_CURRENT_BINARY_DIR}/hello.exe)
add_custom_command(
  OUTPUT ${HELLO_EXE}
  COMMAND ${MINGW_CC} -O1 -ffreestanding -nostdlib -fno-stack-protector -fno-builtin
          -Wl,--entry=_start@0 -Wl,--dynamicbase -Wl,--subsystem,windows
          -o ${HELLO_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/pe/hello.c -lkernel32
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pe/hello.c)
add_custom_target(test_pe ALL DEPENDS ${HELLO_EXE})

foreach(mode preferred relocated)
  add_test(NAME loader_${mode}
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_hello.sh $<TARGET_FILE:chiron> ${HELLO_EXE} ${mode})
endforeach()
