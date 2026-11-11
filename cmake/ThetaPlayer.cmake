# The theta executable. Included from the top-level CMakeLists.txt when THETA_BUILD_PLAYER is ON.

include(ThetaSDL3)

theta_find_sdl3(_have_sdl3)
if(NOT _have_sdl3)
  return()
endif()
set(THETA_PLAYER_AVAILABLE ON)

add_executable(theta src/player/main.c)
target_include_directories(theta PRIVATE ${PROJECT_BINARY_DIR}/generated)
target_link_libraries(theta PRIVATE theta::headers SDL3::SDL3)
theta_configure_target(theta)

if(EMSCRIPTEN)
  include(ThetaWeb)
  theta_configure_web(theta)
  return()
endif()

# the data the player looks for next to the executable
add_custom_command(TARGET theta POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:theta>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THETA_PLAYER_DATA} $<TARGET_FILE_DIR:theta>/assets)

include(GNUInstallDirs)
install(TARGETS theta RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${THETA_PLAYER_DATA}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/theta)
