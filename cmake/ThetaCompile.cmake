# Compile settings shared by every target of the project.
#
#   theta_configure_target(<target>)
#
# Applies the warning set and, with THETA_SANITIZE, AddressSanitizer + UndefinedBehaviorSanitizer. The numeric requirements
# (no floating-point contraction, no fast-math) travel with theta::headers instead, so that consumers inherit them.

function(theta_configure_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
  endif()
  if(THETA_SANITIZE AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
