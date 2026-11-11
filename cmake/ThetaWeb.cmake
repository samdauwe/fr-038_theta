# The browser build of the player (Emscripten): WebAssembly, WebGL 2 and the single page in web/index.html.
#
#   theta_configure_web(<target>)
#
# <build>/dist receives everything a static host serves: index.html, index.js, index.wasm and index.data. The data files of the production
# (theta.kx and the 4 MB soundtrack) are packed into index.data, not linked into the module: the wasm stays small (its budget is
# WASM_BUDGET_BYTES in the CI workflow), the page shows the download of index.data as progress, and the browser caches it on
# its own. There are no pthreads: a page can only share memory between threads with COOP/COEP headers and static hosting (GitHub Pages)
# does not send them, so the player loads in time slices on the main thread instead (--single-thread, see src/player/main.c).

function(theta_configure_web target)
  set(dist ${PROJECT_BINARY_DIR}/dist)
  set(data ${THETA_PLAYER_DATA})
  set_target_properties(${target} PROPERTIES OUTPUT_NAME index SUFFIX ".js" RUNTIME_OUTPUT_DIRECTORY ${dist})

  target_link_options(${target} PRIVATE
    -sMODULARIZE=1 -sEXPORT_NAME=createTheta
    -sENVIRONMENT=web
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,wasmMemory -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=128MB -sMAXIMUM_MEMORY=${THETA_WEB_MAX_MEMORY} -sSTACK_SIZE=1MB
    $<$<CONFIG:Debug>:-sASSERTIONS=1>)
  foreach(file IN LISTS data)
    get_filename_component(name ${file} NAME)
    target_link_options(${target} PRIVATE "SHELL:--preload-file ${file}@/assets/${name}")
  endforeach()
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${data})

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/web/index.html ${dist}/index.html)
endfunction()
