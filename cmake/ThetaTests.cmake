# The test runner (tests/test_main.c + tests/suites/*.c) and its registration with CTest. Included from the top-level
# CMakeLists.txt when THETA_BUILD_TESTS is ON.
#
#   ./theta_tests            runs every suite, a few at a time, and prints one line per suite
#   ctest                     the same suites, one CTest test each (tests/suites.def is the single list)
#   cmake --build . --target check

set(THETA_TEST_DIR ${PROJECT_SOURCE_DIR}/tests)
set(THETA_TEST_WORK_DIR ${PROJECT_BINARY_DIR}/tests/work)
file(MAKE_DIRECTORY ${THETA_TEST_WORK_DIR})

file(GLOB _suite_sources CONFIGURE_DEPENDS ${THETA_TEST_DIR}/suites/*.c)

# The suites that draw through a real OpenGL ES driver need EGL; without it they are replaced by stubs that skip.
find_library(THETA_EGL_LIBRARY EGL)
find_library(THETA_GLES_LIBRARY GLESv2)
find_path(THETA_EGL_INCLUDE_DIR EGL/egl.h)
find_path(THETA_GLES_INCLUDE_DIR GLES3/gl3.h)
set(_have_egl OFF)
if(THETA_EGL_LIBRARY AND THETA_GLES_LIBRARY AND THETA_EGL_INCLUDE_DIR AND THETA_GLES_INCLUDE_DIR AND NOT MSVC)
  set(_have_egl ON)
else()
  message(STATUS "theta tests: EGL/GLES development files not found; the OpenGL ES driver suites will skip")
  list(FILTER _suite_sources EXCLUDE REGEX "/(glr_egl|glr_egl_frames|player_scene_egl)\\.c$")
  list(APPEND _suite_sources ${THETA_TEST_DIR}/support/egl_unavailable.c)
endif()

add_executable(theta_tests ${THETA_TEST_DIR}/test_main.c ${_suite_sources})
target_include_directories(theta_tests PRIVATE ${THETA_TEST_DIR} ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(theta_tests PRIVATE theta::headers)
theta_configure_target(theta_tests)
set_target_properties(theta_tests PROPERTIES FOLDER tests)

# Every suite gets its own copy of the libraries (static functions), so that white-box suites can use the internals and no two
# suites share state. The linkage suite undefines these to test the shared layout.
target_compile_definitions(theta_tests PRIVATE
  THETA_TEST_DATA_DIR="${THETA_TEST_DATA_DIR}"
  THETA_ASSETS_DIR="${THETA_ASSETS_DIR}"
  THETA_FIXTURES_DIR="${THETA_FIXTURES_DIR}"
  THETA_TEST_WORK_DIR="${THETA_TEST_WORK_DIR}"
  V2_STATIC WZ3_STATIC GLR_STATIC
  PLAYER_CORE_STATIC PLAYER_ARGS_STATIC PLAYER_CREDITS_STATIC PLAYER_GLCHECK_STATIC
  PLAYER_LOAD_STATIC PLAYER_REPLAY_STATIC PLAYER_SCENE_STATIC PLAYER_TEXT_STATIC)
if(THETA_TEST_SOAK)
  target_compile_definitions(theta_tests PRIVATE THETA_TEST_SOAK)
endif()
if(NOT MSVC)
  # a suite includes only what it needs of the libraries, so some static functions are unused in it
  target_compile_options(theta_tests PRIVATE -Wno-unused-function)
  target_compile_definitions(theta_tests PRIVATE _POSIX_C_SOURCE=200809L)
endif()
if(THETA_SANITIZE)
  target_compile_definitions(theta_tests PRIVATE THETA_TEST_LSAN_SUPP="${THETA_TEST_DIR}/lsan.supp" THETA_TEST_SLOW_BUILD)
endif()
if(_have_egl)
  target_include_directories(theta_tests PRIVATE ${THETA_EGL_INCLUDE_DIR} ${THETA_GLES_INCLUDE_DIR})
  target_link_libraries(theta_tests PRIVATE ${THETA_EGL_LIBRARY} ${THETA_GLES_LIBRARY})
endif()
if(THETA_PLAYER_AVAILABLE)
  target_compile_definitions(theta_tests PRIVATE THETA_PLAYER_EXE="$<TARGET_FILE:theta>")
  add_dependencies(theta_tests theta)
endif()

add_custom_target(check COMMAND theta_tests DEPENDS theta_tests USES_TERMINAL
  COMMENT "Running every test suite")

# ---- CTest: one test per entry of suites.def ---------------------------------------------------------------------------------------
file(STRINGS ${THETA_TEST_DIR}/suites.def _suite_lines REGEX "^SUITE\\(")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${THETA_TEST_DIR}/suites.def)
set(_timeout_scale 1)
if(THETA_SANITIZE)
  set(_timeout_scale 4)
endif()
foreach(_line IN LISTS _suite_lines)
  if(NOT _line MATCHES "^SUITE\\(\"([a-z0-9_]+)\", *[a-z0-9_]+, *([0-9]+), *([A-Z_0-9]+)")
    message(FATAL_ERROR "tests/suites.def: cannot read the entry: ${_line}")
  endif()
  set(_name ${CMAKE_MATCH_1})
  set(_limit ${CMAKE_MATCH_2})
  set(_flags ${CMAKE_MATCH_3})
  if(_name MATCHES "^player_(theta_)?soak_long$" AND NOT THETA_TEST_SOAK)
    continue()
  endif()
  math(EXPR _limit "${_limit} * ${_timeout_scale} + 60")
  add_test(NAME ${_name} COMMAND theta_tests ${_name})
  set(_labels unit)
  if(_name MATCHES "egl")
    set(_labels gl)
  elseif(_name MATCHES "^player_" AND _line MATCHES "suite_player_cli")
    set(_labels player)
  endif()
  set_tests_properties(${_name} PROPERTIES
    TIMEOUT ${_limit} LABELS "${_labels}" SKIP_RETURN_CODE 77
    ENVIRONMENT "THETA_TEST_TIMEOUT_SCALE=${_timeout_scale}")
  if(_flags MATCHES "SUITE_SERIAL")
    set_tests_properties(${_name} PROPERTIES RUN_SERIAL TRUE)
  endif()
endforeach()

# ---- static checks on the sources -----------------------------------------------------------------------------------------------------
# no mutable global state in the headers (the libraries are reentrant: every instance owns its state)
file(GLOB_RECURSE _library_headers CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/theta/core/*.h ${PROJECT_SOURCE_DIR}/include/theta/core/*.inc)
list(JOIN _library_headers "|" _library_headers_arg)
add_test(NAME headers_no_mutable_statics
  COMMAND ${CMAKE_COMMAND} "-DHEADERS=${_library_headers_arg}" -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_no_mutable_statics.cmake)
set_tests_properties(headers_no_mutable_statics PROPERTIES LABELS unit)

# the libraries do their own transcendental math (bit-exact against the reference): the executable must not import any from libm
find_program(THETA_NM nm)
if(THETA_NM AND NOT WIN32 AND NOT APPLE)
  add_test(NAME no_libm_imports
    COMMAND ${CMAKE_COMMAND} -DNM=${THETA_NM} -DEXE=$<TARGET_FILE:theta_tests> -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_no_libm.cmake)
  set_tests_properties(no_libm_imports PROPERTIES LABELS unit)
endif()

include(ThetaAssets)
