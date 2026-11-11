# Decodes assets/theta.ogg with ffmpeg (raw float, stereo) and requires that ogg.h gives the same PCM, whole and after seeks.
# The reference file (92 MB) is written to the build tree, not to /tmp, and removed.
#   -DFFMPEG=<ffmpeg> -DTOOL=<ogg_pcm> -DOGG=<theta.ogg> -DWORK=<dir>
set(ref "${WORK}/theta_ogg_reference.f32")
execute_process(COMMAND "${FFMPEG}" -v error -y -i "${OGG}" -f f32le -ac 2 "${ref}" RESULT_VARIABLE rc ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "ffmpeg failed (${rc}): ${err}")
endif()
execute_process(COMMAND "${TOOL}" "${OGG}" "${ref}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(REMOVE "${ref}")
message(STATUS "${out}")
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "ogg.h differs from ffmpeg's decoder:\n${out}${err}")
endif()
