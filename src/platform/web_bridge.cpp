#include "platform/web_bridge.h"

#include "core/log.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// Module.aaa* hooks are defined by web/shell.html; guarded so a missing hook never crashes.
EM_JS(void, aaa_js_loading, (const char* stage, float f), {
  if (Module.aaaLoading) Module.aaaLoading(UTF8ToString(stage), f);
});
EM_JS(void, aaa_js_ready, (), { if (Module.aaaReady) Module.aaaReady(); });
EM_JS(void, aaa_js_error, (const char* msg), { if (Module.aaaError) Module.aaaError(UTF8ToString(msg)); });
EM_JS(void, aaa_js_state, (const char* j), {
  // Read-only state snapshot for the automated browser playtest.
  window.__mistpineState = JSON.parse(UTF8ToString(j));
});
EM_JS(void, aaa_js_bench, (const char* j), { window.__mistpineBench = UTF8ToString(j); });
#endif

namespace aaa::web {

void reportLoading(const char* stage, float fraction) {
#if defined(__EMSCRIPTEN__)
  aaa_js_loading(stage, fraction);
#else
  (void)stage;
  (void)fraction;
#endif
}

void reportReady() {
#if defined(__EMSCRIPTEN__)
  aaa_js_ready();
#endif
  AAA_LOG_INFO("ready");
}

void reportError(const char* message) {
#if defined(__EMSCRIPTEN__)
  aaa_js_error(message);
#endif
  AAA_LOG_ERROR("%s", message);
}

void reportState(const char* json) {
#if defined(__EMSCRIPTEN__)
  aaa_js_state(json);
#else
  (void)json;
#endif
}

void reportBench(const char* json) {
#if defined(__EMSCRIPTEN__)
  aaa_js_bench(json);
#else
  (void)json;
#endif
}

}  // namespace aaa::web
