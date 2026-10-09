#pragma once
// Single source of truth for the app version: the CMake project VERSION (0.1.0),
// passed to first-party targets as AAA_VERSION_STRING (PROMPT §8.9).
namespace aaa {

inline const char* appVersion() {
#ifdef AAA_VERSION_STRING
  return AAA_VERSION_STRING;
#else
  return "0.1.0";
#endif
}

}  // namespace aaa
