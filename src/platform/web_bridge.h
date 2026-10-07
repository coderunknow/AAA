#pragma once
// Thin bridge to the HTML shell (loading overlay, errors). No-ops (logging) on native.
namespace aaa::web {

void reportLoading(const char* stage, float fraction);
void reportReady();
void reportError(const char* message);
void reportPause(bool paused);  // shows / hides the HTML pause hint
void reportPrompt(const char* text);  // contextual interaction prompt ("" hides it)

}  // namespace aaa::web
