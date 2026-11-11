# Runs theta_inventory on the document and checks what it measured: 1329 operators in 67 classes, the timing and that every
# class is known to the class metadata.
#   -DTOOL=<theta_inventory> -DKX=<theta.kx>
execute_process(COMMAND "${TOOL}" --expect-ops 1329 --expect-classes 67 "${KX}"
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "theta_inventory failed (${_rc}):\n${_err}\n${_out}")
endif()
foreach(_needle
    "document: 2004 layout, 1329 operators, 67 classes, 89 events, 61 splines"
    "timing: 138.000 BPM, 609.000 beats"
    "classes without class metadata: 0")
  string(FIND "${_out}" "${_needle}" _at)
  if(_at LESS 0)
    message(FATAL_ERROR "theta_inventory output lacks: ${_needle}\n${_out}")
  endif()
endforeach()
message(STATUS "theta_inventory: 1329 operators in 67 classes")
