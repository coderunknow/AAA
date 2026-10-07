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
EM_JS(void, aaa_js_pause, (int p), { if (Module.aaaPause) Module.aaaPause(!!p); });
EM_JS(void, aaa_js_prompt, (const char* t), { if (Module.aaaPrompt) Module.aaaPrompt(UTF8ToString(t)); });
EM_JS(void, aaa_js_start, (int s), { if (Module.aaaStart) Module.aaaStart(!!s); });
EM_JS(void, aaa_js_hud, (const char* j), { if (Module.aaaHud) Module.aaaHud(JSON.parse(UTF8ToString(j))); });
EM_JS(void, aaa_js_notify, (const char* t), { if (Module.aaaNotify) Module.aaaNotify(UTF8ToString(t)); });
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

void reportPause(bool paused) {
#if defined(__EMSCRIPTEN__)
  aaa_js_pause(paused ? 1 : 0);
#else
  (void)paused;
#endif
}

void reportPrompt(const char* text) {
#if defined(__EMSCRIPTEN__)
  aaa_js_prompt(text);
#else
  (void)text;
#endif
}

void reportStart(bool hasSave) {
#if defined(__EMSCRIPTEN__)
  aaa_js_start(hasSave ? 1 : 0);
#else
  (void)hasSave;
#endif
}

void reportHud(const char* json) {
#if defined(__EMSCRIPTEN__)
  aaa_js_hud(json);
#else
  (void)json;
#endif
}

void notify(const char* text) {
#if defined(__EMSCRIPTEN__)
  aaa_js_notify(text);
#endif
  AAA_LOG_INFO("[notice] %s", text);
}

}  // namespace aaa::web
