// Entry point: SDL3 main callbacks (the same code path drives the browser's
// requestAnimationFrame loop and native desktop builds).
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>

#include "app/app.h"
#include "core/version.h"

SDL_AppResult SDL_AppInit(void** state, int argc, char** argv) {
  const aaa::AppOptions opts = aaa::parseOptions(argc, argv);
  if (opts.showVersion) {
    std::printf("Mistpine %s\n", aaa::appVersion());  // --version: print and exit 0
    return SDL_APP_SUCCESS;
  }
  auto* app = new aaa::App();
  *state = app;
  return app->init(opts) ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* state) {
  auto* app = static_cast<aaa::App*>(state);
  if (app->iterate()) return SDL_APP_CONTINUE;
  return app->exitCode() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
  auto* app = static_cast<aaa::App*>(state);
  return app->event(*event) ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
}

void SDL_AppQuit(void* state, SDL_AppResult) {
  auto* app = static_cast<aaa::App*>(state);
  if (app) {
    app->shutdown();
    delete app;
  }
}
