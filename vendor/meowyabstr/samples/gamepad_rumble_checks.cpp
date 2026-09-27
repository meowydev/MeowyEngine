// Behavioral tests for SDL-backed gamepad rumble (SetGamepadVibration).
//
// The hardware rumble itself (motors physically spinning) CANNOT be proven
// without a connected controller and a human/instrument to feel it; that is
// reported UNVERIFIED here. What this test DOES verify deterministically and
// without hardware:
//   1. Parameter conversion: motor [0,1] -> SDL Uint16, duration s -> ms, incl.
//      clamping and the stop (zero) cases.
//   2. GLFW-slot -> SDL-controller MAPPING by GUID + connection order, including
//      the case where SDL and GLFW enumerate devices in a DIFFERENT order and
//      the duplicate-identical-controller tiebreak. Uses the pure resolver so it
//      needs no real devices.
//   3. Lifecycle: SetGamepadVibration with no controller / invalid indices /
//      stop is graceful (no crash), and works before and after a window exists.
#include <meowyrender/meowyrender.hpp>
#include "core/platform.hpp"   // internal: pure rumble helpers (SDL builds only)

#include <cstdio>
#include <vector>
#include <string>
using namespace meowyrender;

static int failures = 0, skips = 0;
static void check(bool ok, const char* msg) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) ++failures;
}

int main() {
#if !defined(MEOWY_HAVE_SDL_GAMEPAD)
    std::printf("SKIP: built without SDL gamepad support; SetGamepadVibration is a documented no-op\n");
    // Still confirm the API is callable and harmless without SDL.
    SetGamepadVibration(0, 1.0f, 1.0f, 0.5f);
    SetGamepadVibration(-1, 0.0f, 0.0f, 0.0f);
    std::printf("SMOKE TEST PASS (no-op build)\n");
    return 0;
#else
    using namespace meowyrender::detail;

    // --- 1. Parameter conversion --------------------------------------------
    check(RumbleMotorToU16(0.0f) == 0, "motor 0.0 -> 0");
    check(RumbleMotorToU16(1.0f) == 0xFFFF, "motor 1.0 -> 65535");
    check(RumbleMotorToU16(0.5f) == 32768, "motor 0.5 -> 32768 (rounded)");
    check(RumbleMotorToU16(-0.5f) == 0, "negative motor clamps to 0");
    check(RumbleMotorToU16(2.0f) == 0xFFFF, "motor > 1 clamps to 65535");
    check(RumbleDurationToMs(0.0f) == 0, "duration 0s -> 0ms (stop)");
    check(RumbleDurationToMs(0.5f) == 500, "duration 0.5s -> 500ms");
    check(RumbleDurationToMs(2.0f) == 2000, "duration 2s -> 2000ms");
    check(RumbleDurationToMs(-1.0f) == 0, "negative duration -> 0ms (stop)");

    // --- 2. GLFW-slot -> SDL-controller mapping by GUID + rank --------------
    // Distinct controllers, DIFFERENT enumeration order between GLFW and SDL.
    {
        std::vector<std::string> glfw(16);
        glfw[0] = "AAAA";  // GLFW slot 0 = controller A
        glfw[1] = "BBBB";  // GLFW slot 1 = controller B
        // SDL enumerates them in the opposite order:
        std::vector<std::string> sdl = {"BBBB", "AAAA"};
        check(ResolveSdlIndexByGuidRank(glfw, 0, sdl) == 1, "slot0(A) maps to SDL index 1 (order differs)");
        check(ResolveSdlIndexByGuidRank(glfw, 1, sdl) == 0, "slot1(B) maps to SDL index 0 (order differs)");
    }
    // Duplicate identical controllers: match by connection-order rank.
    {
        std::vector<std::string> glfw(16);
        glfw[0] = "DUP"; glfw[1] = "DUP"; glfw[2] = "DUP";
        std::vector<std::string> sdl = {"DUP", "DUP", "DUP"};
        check(ResolveSdlIndexByGuidRank(glfw, 0, sdl) == 0, "1st DUP -> SDL 0");
        check(ResolveSdlIndexByGuidRank(glfw, 1, sdl) == 1, "2nd DUP -> SDL 1");
        check(ResolveSdlIndexByGuidRank(glfw, 2, sdl) == 2, "3rd DUP -> SDL 2");
    }
    // Gaps: GLFW slot 0 empty (disconnected), slot 1 present; SDL has only it.
    {
        std::vector<std::string> glfw(16);
        glfw[1] = "XYZ";
        std::vector<std::string> sdl = {"XYZ"};
        check(ResolveSdlIndexByGuidRank(glfw, 0, sdl) == -1, "empty slot maps to nothing");
        check(ResolveSdlIndexByGuidRank(glfw, 1, sdl) == 0, "present slot maps despite gap");
    }
    // No SDL counterpart (GLFW sees a device SDL doesn't) -> no match, no crash.
    {
        std::vector<std::string> glfw(16); glfw[0] = "ONLYGLFW";
        std::vector<std::string> sdl = {"SOMETHINGELSE"};
        check(ResolveSdlIndexByGuidRank(glfw, 0, sdl) == -1, "unmatched GUID -> -1 (unsupported/absent)");
    }
    // Out-of-range slot.
    {
        std::vector<std::string> glfw(16); glfw[0] = "A";
        std::vector<std::string> sdl = {"A"};
        check(ResolveSdlIndexByGuidRank(glfw, -1, sdl) == -1, "negative slot -> -1");
        check(ResolveSdlIndexByGuidRank(glfw, 99, sdl) == -1, "too-large slot -> -1");
    }

    // --- 3. Lifecycle: graceful with no controller / invalid indices --------
    // Before any window: must not crash even if SDL isn't initialized yet.
    SetGamepadVibration(0, 1.0f, 1.0f, 0.2f);   // no controller connected (CI)
    SetGamepadVibration(-1, 1.0f, 1.0f, 0.2f);  // invalid index
    SetGamepadVibration(99, 1.0f, 1.0f, 0.2f);  // out-of-range index
    check(true, "SetGamepadVibration is graceful with no/invalid controller");

    // With a window (typical usage) + stop semantics.
    InitWindow(64, 48, "gamepad rumble checks");
    SetGamepadVibration(0, 0.8f, 0.4f, 1.0f);   // start (no-op if no device)
    SetGamepadVibration(0, 0.0f, 0.0f, 0.0f);   // explicit stop
    check(true, "start then stop is graceful");

    // Report the real-hardware status honestly.
    bool any = false;
    for (int g = 0; g < 16; ++g) if (IsGamepadAvailable(g)) { any = true;
        std::printf("  (controller present at slot %d: %s)\n", g, GetGamepadName(g)); }
    if (any) {
        std::printf("UNVERIFIED: a controller is present but physical motor movement "
                    "cannot be asserted programmatically; run the manual rumble demo to confirm.\n");
    } else {
        std::printf("UNVERIFIED: no controller connected -- actual hardware rumble not exercised. "
                    "Mapping + conversion + lifecycle verified without hardware.\n");
    }
    CloseWindow();

    std::printf("%s\n", failures ? "SMOKE TEST FAIL" : "SMOKE TEST PASS");
    return failures ? 1 : 0;
#endif
}
