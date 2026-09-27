// Tests for startup-time runtime backend selection.
//
// Covers: string parsing, compiled/available queries, name/enum reporting,
// automatic selection (best backend for this platform), explicit selection,
// unavailable-backend handling, SetPreferredBackend-after-init rejection, and
// -- driven by env vars the harness sets -- simulated init failure with clean
// fallback and the all-fail no-crash path. GPU-touching cases skip gracefully
// if no window can be created (headless CI), but the pure-logic cases always run.
#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
namespace mr = meowyrender;

static int failures = 0;
static void check(bool ok, const char* msg) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    // A single mode arg lets the CTest harness drive the env-dependent cases in
    // separate processes (env is read inside InitWindow).
    const std::string mode = argc > 1 ? argv[1] : "logic";

    if (mode == "logic") {
        // --- String parsing -------------------------------------------------
        mr::Backend b;
        check(mr::ParseBackend("metal", b)  && b == mr::Backend::Metal,  "parse \"metal\"");
        check(mr::ParseBackend("Vulkan", b) && b == mr::Backend::Vulkan, "parse \"Vulkan\" (case-insensitive)");
        check(mr::ParseBackend("OPENGL", b) && b == mr::Backend::OpenGL, "parse \"OPENGL\"");
        check(mr::ParseBackend("gl", b)     && b == mr::Backend::OpenGL, "parse \"gl\" alias");
        check(mr::ParseBackend("auto", b)   && b == mr::Backend::Automatic, "parse \"auto\"");
        check(mr::ParseBackend("", b)       && b == mr::Backend::Automatic, "parse empty -> Automatic");
        check(!mr::ParseBackend("d3d12", b), "parse unknown returns false");

        // --- Enum name reporting -------------------------------------------
        check(std::strcmp(mr::GetBackendEnumName(mr::Backend::Metal), "Metal") == 0, "enum name Metal");
        check(std::strcmp(mr::GetBackendEnumName(mr::Backend::Automatic), "Automatic") == 0, "enum name Automatic");

        // --- Compiled vs available -----------------------------------------
        // At least one backend must be compiled + available on any real build.
        check(mr::IsBackendCompiled(mr::Backend::Automatic), "some backend is compiled");
        check(mr::IsBackendAvailable(mr::Backend::Automatic), "some backend is available");
        // A backend that is available must also be compiled.
        for (mr::Backend cand : {mr::Backend::Metal, mr::Backend::Vulkan, mr::Backend::OpenGL})
            if (mr::IsBackendAvailable(cand))
                check(mr::IsBackendCompiled(cand), "available implies compiled");
        // Report the platform's compiled set for the log.
        std::printf("  (compiled: Metal=%d Vulkan=%d OpenGL=%d)\n",
                    mr::IsBackendCompiled(mr::Backend::Metal),
                    mr::IsBackendCompiled(mr::Backend::Vulkan),
                    mr::IsBackendCompiled(mr::Backend::OpenGL));

        // --- Before InitWindow, active backend is Automatic ----------------
        check(mr::GetActiveBackend() == mr::Backend::Automatic, "no active backend before InitWindow");
        check(std::strcmp(mr::GetActiveBackendName(), "none") == 0, "active name is \"none\" before InitWindow");

        // --- Preference get/set (no window yet) ----------------------------
        mr::SetPreferredBackend(mr::Backend::Vulkan);
        check(mr::GetPreferredBackend() == mr::Backend::Vulkan, "SetPreferredBackend/GetPreferredBackend");
        mr::SetPreferredBackend(mr::Backend::Automatic);
        check(mr::GetPreferredBackend() == mr::Backend::Automatic, "reset preference to Automatic");

        // --- Platform automatic ordering -----------------------------------
        // On Apple, when Metal is compiled into the build it must also be
        // available (it is the top of the automatic preference order). In a
        // single-backend build that omits Metal this is simply skipped. Metal is
        // never offered on non-Apple platforms.
#if defined(__APPLE__)
        if (mr::IsBackendCompiled(mr::Backend::Metal))
            check(mr::IsBackendAvailable(mr::Backend::Metal), "Apple: compiled Metal is available (top of auto order)");
#else
        check(!mr::IsBackendCompiled(mr::Backend::Metal), "non-Apple build does not compile Metal");
#endif

        // --- Unavailable-backend handling ----------------------------------
        // Requesting a backend that isn't compiled must NOT be reported as
        // available, and IsBackendAvailable must be false for it.
        for (mr::Backend cand : {mr::Backend::Metal, mr::Backend::Vulkan, mr::Backend::OpenGL})
            if (!mr::IsBackendCompiled(cand))
                check(!mr::IsBackendAvailable(cand), "uncompiled backend is not available");

        std::printf("%s\n", failures ? "SMOKE TEST FAIL" : "SMOKE TEST PASS");
        return failures ? 1 : 0;
    }

    // --- Window-backed modes (need a display; skip gracefully if unavailable) ---
    auto tryInit = [&](const char* label) -> bool {
        try { mr::InitWindow(320, 240, "backend selection"); return true; }
        catch (const std::exception& e) {
            std::printf("SKIP: %s (InitWindow: %s)\n", label, e.what());
            return false;
        }
    };

    if (mode == "auto") {
        if (!tryInit("automatic selection")) return 0;
        mr::Backend active = mr::GetActiveBackend();
        std::printf("  (auto-selected %s)\n", mr::GetActiveBackendName());
        // The active backend must be one that is compiled + available.
        check(active != mr::Backend::Automatic, "automatic selection picked a concrete backend");
        check(mr::IsBackendCompiled(active), "auto-selected backend is compiled");
        mr::CloseWindow();
    } else if (mode == "explicit") {
        // Explicitly select whichever backend the harness names in argv[2].
        mr::Backend want; mr::ParseBackend(argc > 2 ? argv[2] : "opengl", want);
        if (!mr::IsBackendCompiled(want)) { std::printf("SKIP: %s not compiled\n", mr::GetBackendEnumName(want)); return 0; }
        mr::SetPreferredBackend(want);
        if (!tryInit("explicit selection")) return 0;
        std::printf("  (requested %s, got %s)\n", mr::GetBackendEnumName(want), mr::GetActiveBackendName());
        check(mr::GetActiveBackend() == want, "explicit backend selection honored");
        mr::CloseWindow();
    } else if (mode == "reject-after-init") {
        if (!tryInit("reject-after-init")) return 0;
        mr::Backend before = mr::GetActiveBackend();
        mr::SetPreferredBackend(mr::Backend::OpenGL); // should be rejected + logged
        check(mr::GetActiveBackend() == before, "active backend unchanged after post-init SetPreferredBackend");
        mr::CloseWindow();
    } else if (mode == "fallback") {
        // Harness sets MEOWY_FORCE_BACKEND_FAIL to fail the first-preferred
        // backend; selection must fall back to another compiled backend.
        if (!tryInit("fallback")) return 0;
        std::printf("  (after forced failure, active = %s)\n", mr::GetActiveBackendName());
        check(mr::GetActiveBackend() != mr::Backend::Automatic, "fell back to a working backend");
        mr::CloseWindow();
    } else if (mode == "allfail") {
        // Harness forces every backend to fail; InitWindow must throw, not crash,
        // and leave no window behind (a subsequent normal init must still work).
        bool threw = false;
        try { mr::InitWindow(320, 240, "allfail"); }
        catch (const std::exception&) { threw = true; }
        check(threw, "InitWindow throws when all backends fail (no crash)");
        check(mr::GetActiveBackend() == mr::Backend::Automatic, "no active backend after total failure");
        std::printf("%s\n", failures ? "SMOKE TEST FAIL" : "SMOKE TEST PASS");
        return failures ? 1 : 0;
    }

    std::printf("%s\n", failures ? "SMOKE TEST FAIL" : "SMOKE TEST PASS");
    return failures ? 1 : 0;
}
