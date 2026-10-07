#pragma once
// Tiny self-registering test harness (no external dependency).
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace aaatest {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
}  // namespace aaatest

#define AAA_CAT2(a, b) a##b
#define AAA_CAT(a, b) AAA_CAT2(a, b)
#define TEST_CASE(name) \
  static void AAA_CAT(test_fn_, __LINE__)(); \
  static ::aaatest::Registrar AAA_CAT(test_reg_, __LINE__)(name, AAA_CAT(test_fn_, __LINE__)); \
  static void AAA_CAT(test_fn_, __LINE__)()
#define CHECK(cond) do { if (!(cond)) { ++::aaatest::failures(); \
  std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { const double _a = (a), _b = (b); if (std::fabs(_a - _b) > (eps)) { \
  ++::aaatest::failures(); std::printf("  FAIL %s:%d: %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)
