# Shared warning/diagnostic flags for first-party targets (PROMPT §8.9: zero warnings
# under the existing warning flags; -Werror on GCC/Clang/em++, /WX for MSVC).
# Only first-party targets call aaa_target_defaults — third-party code (bgfx, SDL,
# stb, …) keeps its own warning policy.
option(AAA_SANITIZERS "Instrument first-party code with AddressSanitizer + UBSan" OFF)

function(aaa_target_defaults target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter
      -Wno-missing-field-initializers -Werror)
    if(AAA_SANITIZERS)
      # UBSan halts on the first undefined behaviour so the CI job actually fails.
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined
        -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
  target_compile_features(${target} PUBLIC cxx_std_20)
endfunction()
