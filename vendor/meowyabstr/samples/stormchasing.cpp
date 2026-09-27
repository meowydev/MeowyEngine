// meowyrender sample - StormChasing
//
// A deliberately tiny application showing that ordinary usage stays simple with
// runtime backend selection: InitWindow picks the best available backend
// automatically, and the app just draws its name. An optional `--backend`
// argument overrides the choice using the library's string parser; the library
// still falls back automatically if that backend is unavailable.
//
//   StormChasing                 # automatic (best for the platform)
//   StormChasing --backend metal
//   StormChasing --backend vulkan
//   StormChasing --backend opengl
//
// The MEOWY_BACKEND environment variable is also honored (lower precedence than
// an explicit --backend / SetPreferredBackend).
#include <meowyrender/meowyrender.hpp>
#include <cstring>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    // Command-line override (parsing lives in the app; the library only provides
    // the string->Backend helper so it never owns argv).
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            meowyrender::Backend b;
            if (meowyrender::ParseBackend(argv[++i], b))
                meowyrender::SetPreferredBackend(b);
            else
                std::fprintf(stderr, "StormChasing: unknown --backend '%s'\n", argv[i]);
        }
    }

    meowyrender::InitWindow(800, 600, "StormChasing");

    // Headless/CI escape hatch so this sample can be smoke-tested without a
    // person closing the window.
    int frames = 0, frameLimit = 0;
    if (const char* f = std::getenv("MEOWY_STORMCHASING_FRAMES")) frameLimit = std::atoi(f);

    while (!meowyrender::WindowShouldClose()) {
        meowyrender::BeginDrawing();
        meowyrender::ClearBackground(meowyrender::BLACK);
        meowyrender::DrawText(
            meowyrender::GetActiveBackendName(),
            20, 20, 24, meowyrender::WHITE);
        meowyrender::EndDrawing();
        if (frameLimit > 0 && ++frames >= frameLimit) break;
    }

    meowyrender::CloseWindow();
    return 0;
}
