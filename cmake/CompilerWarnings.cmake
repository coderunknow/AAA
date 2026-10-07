# Shared warning/diagnostic flags for first-party targets.
function(aaa_target_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter
      -Wno-missing-field-initializers)
  endif()
  target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()
