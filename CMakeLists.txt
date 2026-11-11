cmake_minimum_required(VERSION 3.25)

project(theta
  VERSION 0.1.0
  DESCRIPTION "Theta (Farbrausch fr-038) native player: C99 libraries, SDL3 and OpenGL ES 3"
  LANGUAGES C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(EMSCRIPTEN)
  set(CMAKE_C_EXTENSIONS ON) # EM_ASM, and SDL3's own use of it, do not compile in -std=c99
endif()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- data --------------------------------------------------------------------------------------------------------
# assets/ holds the production data (theta.kx, theta.ogg; CC BY-SA 3.0, see assets/ATTRIBUTION.md); tests/data holds the
# reference fixtures of the test suites (tests/data/theta those derived from the production data).
set(THETA_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(THETA_TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
set(THETA_FIXTURES_DIR ${THETA_TEST_DATA_DIR}/theta)
# What the player loads: copied next to the executable, installed and embedded in the web build.
set(THETA_PLAYER_DATA ${THETA_ASSETS_DIR}/theta.kx ${THETA_ASSETS_DIR}/theta.ogg)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- options -----------------------------------------------------------------------------------------------------------------
set(_is_root OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_is_root ON)
endif()
# the tools and the tests are native programs; the browser build is the player only
set(_native_default ${_is_root})
if(EMSCRIPTEN)
  set(_native_default OFF)
endif()

option(THETA_BUILD_PLAYER "Build the SDL3 player (theta)" ON)
option(THETA_BUILD_TOOLS "Build the command-line tools (theta_inventory, theta_precalc, theta_scene)" ${_native_default})
option(THETA_BUILD_TESTS "Build the test runner (theta_tests) and register it with CTest" ${_native_default})
option(THETA_FETCH_SDL3 "Fetch and build SDL3 when no installed SDL3 >= 3.2 is found" ON)
set(THETA_SDL3_TAG "release-3.4.8" CACHE STRING "SDL3 tag used when it is fetched")
set(THETA_WEB_MAX_MEMORY "2GB" CACHE STRING "Emscripten builds: the largest the WebAssembly memory may grow to")
option(THETA_V2_RONAN "Include the Ronan speech synthesizer (V2 channel 15)" ON)
option(THETA_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(THETA_TEST_SOAK "Add the long soak test (the whole demo repeatedly, ten minutes of wall time)" OFF)

include(ThetaCompile)

# ---- libraries -----------------------------------------------------------------------------------------------------------------
# The libraries are single headers in the style of stb: define <NAME>_IMPLEMENTATION in exactly one translation unit
# (see doc/ARCHITECTURE.md). They only carry usage requirements. Bit-exact results need strict binary32 arithmetic, so
# every consumer is compiled without floating-point contraction.
add_library(theta_headers INTERFACE)
add_library(theta::headers ALIAS theta_headers)
target_include_directories(theta_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(theta_headers INTERFACE c_std_99)
if(NOT THETA_V2_RONAN)
  target_compile_definitions(theta_headers INTERFACE V2_RONAN=0)
endif()
if(MSVC)
  target_compile_options(theta_headers INTERFACE /fp:precise)
else()
  target_compile_options(theta_headers INTERFACE -ffp-contract=off)
  target_link_libraries(theta_headers INTERFACE m)
endif()

# ---- licenses ----------------------------------------------------------------------------------------------------------------
include(EmbedLicenses)
# The notices of everything the player is built from, embedded in the executable (theta --licenses). The header is
# generated into the build tree; the player and its tests include it.
set(_licenses ${PROJECT_SOURCE_DIR}/third_party/licenses)
theta_embed_licenses(${PROJECT_BINARY_DIR}/generated/licenses_data.h
  "werkkzeug3 operator library (Farbrausch fr_public), BSD license" ${_licenses}/werkkzeug3-LICENSE.txt
  "V2 synthesizer (v2redux, after the original Farbrausch V2), public domain" ${_licenses}/v2redux-LICENSE.txt
  "GLU tessellator (libtess.js, SGI sample implementation), SGI Free Software License B" ${_licenses}/libtess-LICENSE.txt
  "Arimo and Gelasio outlines (text and Font3D glyph tables), SIL Open Font License 1.1" ${_licenses}/font3d-LICENSE.txt
  "fr-038 theta production data and music, Creative Commons Attribution-ShareAlike 3.0" ${PROJECT_SOURCE_DIR}/assets/ATTRIBUTION.md
  "SDL3, zlib license" ${_licenses}/SDL3-NOTICE.txt
  "Ogg Vorbis decoder and JPEG decoder (stb_vorbis, stb_image by Sean Barrett), MIT or public domain" ${_licenses}/stb-LICENSE.txt)

# ---- player --------------------------------------------------------------------------------------------------------------------
set(THETA_PLAYER_AVAILABLE OFF)
if(THETA_BUILD_PLAYER)
  include(ThetaPlayer)
endif()

# ---- tools ---------------------------------------------------------------------------------------------------------------------
if(THETA_BUILD_TOOLS OR THETA_BUILD_TESTS)
  foreach(tool IN ITEMS theta_inventory theta_precalc theta_scene)
    add_executable(${tool} tools/${tool}.c)
    target_link_libraries(${tool} PRIVATE theta::headers)
    theta_configure_target(${tool})
    set_target_properties(${tool} PROPERTIES FOLDER tools)
  endforeach()
endif()

# ---- tests ---------------------------------------------------------------------------------------------------------------------
if(THETA_BUILD_TESTS)
  enable_testing()
  include(ThetaTests)
endif()

# ---- formatting ----------------------------------------------------------------------------------------------------------------
# `cmake --build <dir> --target format` applies .clang-format; `format-check` fails when a file does not conform. The fragments
# (*.inc) have no language of their own for clang-format, so they are formatted as C.
find_program(THETA_CLANG_FORMAT clang-format)
if(THETA_CLANG_FORMAT AND _is_root)
  file(GLOB_RECURSE _formatted CONFIGURE_DEPENDS
    include/theta/*.h include/theta/*.inc src/*.c tests/*.c tests/*.h tools/*.c)
  add_custom_target(format COMMAND ${THETA_CLANG_FORMAT} -i ${_formatted}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMENT "Formatting the sources" VERBATIM)
  add_custom_target(format-check COMMAND ${THETA_CLANG_FORMAT} --dry-run -Werror ${_formatted}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMENT "Checking the formatting of the sources" VERBATIM)
endif()

# ---- install -------------------------------------------------------------------------------------------------------------------
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h" PATTERN "*.inc")
