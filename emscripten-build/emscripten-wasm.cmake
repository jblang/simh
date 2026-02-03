# Custom Emscripten toolchain for SIMH WASM build.
# Use with: cmake -DCMAKE_TOOLCHAIN_FILE=./emscripten-wasm.cmake ..

# First, include the standard Emscripten toolchain
set(EMSCRIPTEN_TOOLCHAIN_FILE "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
if(NOT EXISTS "${EMSCRIPTEN_TOOLCHAIN_FILE}")
    # Try homebrew location
    set(EMSCRIPTEN_TOOLCHAIN_FILE "/opt/homebrew/Cellar/emscripten/5.0.0/libexec/cmake/Modules/Platform/Emscripten.cmake")
endif()

if(NOT EXISTS "${EMSCRIPTEN_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Could not find Emscripten toolchain file")
endif()

include("${EMSCRIPTEN_TOOLCHAIN_FILE}")

# Output .js instead of .html — the HTML shell is not needed.
set(CMAKE_EXECUTABLE_SUFFIX ".js")
set(CMAKE_EXECUTABLE_SUFFIX_C ".js")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".js")

# Emscripten linker flags:
#   MODULARIZE=1        — export a factory function instead of a global Module
#   EXPORT_NAME         — name of the factory function
#   EXPORTED_FUNCTIONS  — C functions callable from JS via ccall/cwrap
#   EXPORTED_RUNTIME_METHODS — Emscripten APIs available on the Module object
#   ALLOW_MEMORY_GROWTH — allow heap to grow dynamically
#   ALLOW_TABLE_GROWTH  — allow function table to grow (needed for function pointers)
#   STACK_SIZE          — 1MB stack (default 64KB too small for SIMH command buffers)
#   FORCE_FILESYSTEM    — enable virtual filesystem (needed for scripts/ATTACH)
#   NO_EXIT_RUNTIME     — keep the runtime alive after main() returns
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=createI650Module \
    -s ASYNCIFY=1 \
    -s EXPORTED_FUNCTIONS=['_simh_init','_simh_cmd','_simh_step','_simh_stop','_simh_is_running','_simh_is_busy','_simh_get_yield_steps','_simh_set_yield_steps','_main'] \
    -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS'] \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s ALLOW_TABLE_GROWTH=1 \
    -s STACK_SIZE=1048576 \
    -s EXIT_RUNTIME=0 \
    -s NO_EXIT_RUNTIME=1 \
    -s FORCE_FILESYSTEM=1 \
    --preload-file ${CMAKE_SOURCE_DIR}/I650/sw@/sw \
    --preload-file ${CMAKE_SOURCE_DIR}/I650/tests@/tests" CACHE STRING "Emscripten linker flags" FORCE)

message(STATUS "Using emscripten-wasm toolchain (MODULARIZE, ASYNCIFY, preloaded filesystem)")
message(STATUS "Executables will have .js suffix")
