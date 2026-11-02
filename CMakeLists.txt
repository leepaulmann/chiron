cmake_minimum_required(VERSION 3.20)
project(chiron C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Chiron hosts 32-bit Windows code and must be built for i686 (use `nix develop`).")
endif()

add_compile_definitions(_FILE_OFFSET_BITS=64 _TIME_BITS=64)
# Windows code only keeps the stack 4-byte aligned, so every function we
# expose to it must realign before using SSE spills.
add_compile_options(-Wall -Wno-unused-parameter -mincoming-stack-boundary=2 -fno-strict-aliasing
                    -fno-omit-frame-pointer)
add_link_options(-no-pie)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

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

set(KERNEL32_SOURCES src/win32/kernel32_proc.c src/win32/kernel32_file.c)
shim_exports(kernel32_exports KERNEL32.dll ${KERNEL32_SOURCES})

add_executable(chiron
  src/main.c
  src/install/install.c
  src/loader/pe.c
  src/loader/teb.c
  src/loader/thunks.c
  src/loader/modules.c
  src/win32/registry.c
  src/win32/path.c
  src/win32/handles.c
  ${KERNEL32_SOURCES}
  ${SHIM_EXPORT_SOURCES})
target_include_directories(chiron PRIVATE src/win32)
target_link_libraries(chiron PRIVATE Threads::Threads)

enable_testing()
add_subdirectory(tests)
