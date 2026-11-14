// Build skeleton smoke test: verify the real C++ standard mode and the CTest chain.
// Standard library only. Messages stay ASCII so the source compiles the same way
// under MSVC (CP936/UTF-8) and GCC without extra encoding flags.

#include <cstdio>
#include <span>
#include <version>

namespace {

constexpr long standard_mode() {
#if defined(_MSVC_LANG)
  return _MSVC_LANG;
#else
  return __cplusplus;
#endif
}

}  // namespace

int main() {
  // No branch on a constant here: MSVC /W4 reports C4127 for that.
  // The comparison result drives the output directly.
  const long mode = standard_mode();
  bool ok = mode >= 202002L;
  std::printf("C++ standard mode: %ld (__cplusplus=%ld) %s\n", mode, static_cast<long>(__cplusplus),
              ok ? "C++20+" : "older than C++20");

#if defined(__cpp_lib_span)
  const int sample[]{1, 2, 3};
  const std::span<const int> view{sample};
  std::printf("std::span: __cpp_lib_span=%ld, size=%zu\n", static_cast<long>(__cpp_lib_span),
              view.size());
  ok = ok && view.size() == 3;
#else
  std::printf("std::span: unavailable\n");
#endif

  std::printf("smoke test: %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
