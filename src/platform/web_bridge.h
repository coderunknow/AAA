#pragma once
// Thin bridge to the HTML shell. The DOM retains only the loading overlay and
// fatal-error / WebGL2-unsupported messaging; menus, HUD, prompts and toasts are
// rendered in-engine (PROMPT §8.5). No-ops (logging) on native.
namespace aaa::web {

void reportLoading(const char* stage, float fraction);
void reportReady();
void reportError(const char* message);
// JSON snapshot of the UI/game state for the browser harness (window.__mistpineState).
void reportState(const char* json);

}  // namespace aaa::web
