# Emscripten HTML output override
# Include this file to force HTML output instead of JS

if(EMSCRIPTEN)
    # Override the default .js suffix
    set(CMAKE_EXECUTABLE_SUFFIX_C ".html" CACHE STRING "" FORCE)
    set(CMAKE_EXECUTABLE_SUFFIX_CXX ".html" CACHE STRING "" FORCE)
    set(CMAKE_EXECUTABLE_SUFFIX ".html" CACHE STRING "" FORCE)

    message(STATUS "Emscripten HTML mode enabled - executables will have .html suffix")
endif()
