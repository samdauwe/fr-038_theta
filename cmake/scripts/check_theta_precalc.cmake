# Runs theta_precalc on the document and checks what it reports: every operator runs, every mesh operator makes drawable
# geometry, and the fingerprint over all meshes is the recorded one. The fingerprint is a regression guard, not an oracle: it
# changes when an operator changes.
#   -DTOOL=<theta_precalc> -DKX=<theta.kx> -DFINGERPRINT=<0x...>
execute_process(COMMAND "${TOOL}" --expect-ok "${KX}" OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "theta_precalc failed (${_rc}):\n${_err}\n${_out}")
endif()
foreach(_needle
    "precalc: ok, 1329 operators, 0 failed"
    "meshes: 733 of 733 mesh operators, 2140553 vertices, 5169408 indices"
    "0 not drawable"
    "mesh fingerprint ${FINGERPRINT}"
    "unexpected: 0 operators without a handler or bitmap, 0 failed, 0 without a mesh")
  string(FIND "${_out}" "${_needle}" _at)
  if(_at LESS 0)
    message(FATAL_ERROR "theta_precalc output lacks: ${_needle}\n${_out}")
  endif()
endforeach()
message(STATUS "theta_precalc: every mesh operator produced drawable geometry, ${FINGERPRINT}")
