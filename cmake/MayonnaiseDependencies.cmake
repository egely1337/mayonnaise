# Dependency resolution: prefer system packages, fall back to FetchContent.
# With MAYONNAISE_USE_SYSTEM_DEPS=ON the system packages are required.
include(FetchContent)

if(MAYONNAISE_USE_SYSTEM_DEPS)
  set(_mayo_find REQUIRED)
else()
  set(_mayo_find QUIET)
endif()

# --- SDL3 -------------------------------------------------------------------
find_package(SDL3 CONFIG ${_mayo_find} COMPONENTS SDL3)
if(NOT TARGET SDL3::SDL3)
  message(STATUS "Mayonnaise: fetching SDL3")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(SDL3)
  set(MAYONNAISE_FETCHED_DEPS ON)
else()
  message(STATUS "Mayonnaise: using system SDL3 ${SDL3_VERSION}")
endif()

# --- FreeType -----------------------------------------------------------------
find_package(Freetype ${_mayo_find})
if(NOT TARGET Freetype::Freetype)
  message(STATUS "Mayonnaise: fetching FreeType")
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-14-3
    GIT_SHALLOW TRUE
    SYSTEM)
  FetchContent_MakeAvailable(freetype)
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  endif()
  set(MAYONNAISE_FETCHED_DEPS ON)
else()
  message(STATUS "Mayonnaise: using system FreeType")
endif()

# --- doctest (tests only) -----------------------------------------------------
if(MAYONNAISE_BUILD_TESTS)
  find_package(doctest CONFIG ${_mayo_find})
  if(NOT TARGET doctest::doctest)
    message(STATUS "Mayonnaise: fetching doctest")
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
      GIT_REPOSITORY https://github.com/doctest/doctest.git
      GIT_TAG v2.5.3
      GIT_SHALLOW TRUE
      SYSTEM)
    FetchContent_MakeAvailable(doctest)
  endif()
endif()

find_package(OpenGL REQUIRED COMPONENTS OpenGL)

# --- Vulkan (optional renderer backend) ----------------------------------------
# Only the headers and a GLSL -> SPIR-V compiler are needed at build time: libvulkan is loaded at run time
# through SDL, and the app falls back to OpenGL when it is missing or unsuitable.
if(MAYONNAISE_VULKAN)
  find_program(MAYONNAISE_GLSLC glslc)
  find_program(MAYONNAISE_GLSLANG glslangValidator)
  find_path(MAYONNAISE_VULKAN_INCLUDE_DIR vulkan/vulkan_core.h)
  if(NOT MAYONNAISE_VULKAN_INCLUDE_DIR AND NOT MAYONNAISE_USE_SYSTEM_DEPS)
    message(STATUS "Mayonnaise: fetching Vulkan-Headers")
    FetchContent_Declare(vulkan_headers
      GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
      GIT_TAG v1.4.357
      GIT_SHALLOW TRUE
      SOURCE_SUBDIR no-cmake  # headers only: don't add its project (and its install rules)
      SYSTEM)
    FetchContent_MakeAvailable(vulkan_headers)
    set(MAYONNAISE_VULKAN_INCLUDE_DIR ${vulkan_headers_SOURCE_DIR}/include CACHE PATH "" FORCE)
  endif()
  if(NOT MAYONNAISE_VULKAN_INCLUDE_DIR OR (NOT MAYONNAISE_GLSLC AND NOT MAYONNAISE_GLSLANG))
    message(WARNING "Mayonnaise: Vulkan backend disabled (needs the Vulkan headers and glslc or glslangValidator)")
    set(MAYONNAISE_VULKAN OFF)
  else()
    message(STATUS "Mayonnaise: Vulkan backend enabled")
  endif()
endif()
