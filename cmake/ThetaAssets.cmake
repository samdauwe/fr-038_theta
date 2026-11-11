# Tests of the production data and of what the runtime makes of it. Included from ThetaTests.cmake; none needs a display.
#
#   assets_sha256     theta.kx and theta.ogg match the SHA-256 recorded in tests/data/theta/assets.sha256
#   theta_inventory   the document has 1329 operators in 67 classes and every class is known to the class metadata
#   theta_precalc     every operator of the document runs; a fingerprint over every mesh the run produces
#   theta_scene_t*    what the frame planner puts in a frame of the title, the tunnel, the later scenes, the walker and the credits
#   ogg_pcm_oracle    ogg.h decodes theta.ogg like ffmpeg does (only where ffmpeg is installed)

add_test(NAME assets_sha256
  COMMAND ${CMAKE_COMMAND} -DDIR=${THETA_ASSETS_DIR} -DSUMS=${THETA_FIXTURES_DIR}/assets.sha256
          -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_assets_sha256.cmake)
set_tests_properties(assets_sha256 PROPERTIES LABELS unit)

add_test(NAME theta_inventory
  COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:theta_inventory> -DKX=${THETA_ASSETS_DIR}/theta.kx
          -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_theta_inventory.cmake)
set_tests_properties(theta_inventory PROPERTIES LABELS unit)
add_dependencies(theta_tests theta_inventory)

# a regression fingerprint, not an oracle: it changes when an operator changes
add_test(NAME theta_precalc
  COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:theta_precalc> -DKX=${THETA_ASSETS_DIR}/theta.kx
          -DFINGERPRINT=0xbb6141f7 -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_theta_precalc.cmake)
set_tests_properties(theta_precalc PROPERTIES LABELS unit TIMEOUT 300)
add_dependencies(theta_tests theta_precalc)

foreach(_t IN ITEMS 2 14 24 210 250)
  add_test(NAME theta_scene_t${_t}
    COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:theta_scene> -DKX=${THETA_ASSETS_DIR}/theta.kx -DTIME=${_t}
            -DEXPECTED=${THETA_FIXTURES_DIR}/scene_t${_t}.txt -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_theta_scene.cmake)
  set_tests_properties(theta_scene_t${_t} PROPERTIES LABELS unit)
  add_dependencies(theta_tests theta_scene)
endforeach()

find_program(THETA_FFMPEG ffmpeg)
if(THETA_FFMPEG)
  add_executable(ogg_pcm ${THETA_TEST_DIR}/oracle/ogg_pcm.c)
  target_link_libraries(ogg_pcm PRIVATE theta::headers)
  theta_configure_target(ogg_pcm)
  set_target_properties(ogg_pcm PROPERTIES FOLDER tests)
  add_test(NAME ogg_pcm_oracle
    COMMAND ${CMAKE_COMMAND} -DFFMPEG=${THETA_FFMPEG} -DTOOL=$<TARGET_FILE:ogg_pcm> -DOGG=${THETA_ASSETS_DIR}/theta.ogg
            -DWORK=${THETA_TEST_WORK_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/scripts/check_ogg_pcm.cmake)
  set_tests_properties(ogg_pcm_oracle PROPERTIES LABELS "unit;oracle" TIMEOUT 300)
  add_dependencies(theta_tests ogg_pcm)
else()
  message(STATUS "theta tests: no ffmpeg; the comparison of ogg.h with another decoder is off")
endif()
