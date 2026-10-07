#include <cstring>

#include "test.h"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int run = 0;
  for (auto& c : aaatest::registry()) {
    if (filter && !std::strstr(c.name, filter)) continue;
    const int before = aaatest::failures();
    c.fn();
    ++run;
    std::printf("%s %s\n", aaatest::failures() == before ? "[ ok ]" : "[FAIL]", c.name);
  }
  std::printf("%d tests, %d failed checks\n", run, aaatest::failures());
  return aaatest::failures() == 0 ? 0 : 1;
}
