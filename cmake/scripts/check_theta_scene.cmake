# Runs theta_scene at a time and compares its report with the recorded one: the viewports, cameras, lights, mesh jobs and
# 2D layers a frame of the theta document plans. A regression guard of the frame planner (not an oracle).
#   -DTOOL=<theta_scene> -DKX=<theta.kx> -DTIME=<seconds> -DEXPECTED=<scene_tN.txt>
execute_process(COMMAND "${TOOL}" --time "${TIME}" "${KX}" OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "theta_scene failed (${_rc}):\n${_err}")
endif()
file(READ "${EXPECTED}" _want)
if(NOT _out STREQUAL _want)
  message(FATAL_ERROR "the frame at t = ${TIME} s changed.\n--- expected (${EXPECTED})\n${_want}\n--- got\n${_out}")
endif()
message(STATUS "theta_scene: t = ${TIME} s matches ${EXPECTED}")
