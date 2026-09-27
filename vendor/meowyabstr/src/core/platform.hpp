// meowyrender - src/core/platform.hpp  (internal)
// GLFW-based windowing + input plumbing shared by all desktop backends.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace meowyrender::detail {

// Create the platform window for a specific backend. For OpenGL, requests a
// GL 3.3 core context; for Metal/Vulkan, requests GLFW_NO_API (the backend owns
// the surface). `backend` is the internal BackendKind value (0=OpenGL, 1=Metal,
// 2=Vulkan) matching backend::BackendKind; passed as int to keep this header
// free of the backend interface include. Returns null on failure.
void* PlatformCreateWindow(int width, int height, const char* title, int backend);
void PlatformDestroyWindow(void* window);

// Poll OS events and roll the per-frame input state forward.
void PlatformPollInput();

// Register GLFW callbacks that write into the shared InputState.
void PlatformInstallCallbacks(void* window);
void PlatformFramebufferSize(int* width,int* height);

// Release the SDL gamepad-rumble subsystem and any open controller handles.
// Called from CloseWindow so rumble lifetime tracks the window. No-op on
// platforms/builds without the SDL rumble path (e.g. visionOS).
void ShutdownGamepadRumble();

// --- Pure, hardware-independent pieces of the SDL rumble path, exposed for
// --- unit testing (mapping + parameter conversion). Available whenever the SDL
// --- rumble path is compiled in.
#if defined(MEOWY_HAVE_SDL_GAMEPAD)
// Convert a raylib motor strength [0,1] to an SDL 16-bit rumble amplitude.
std::uint16_t RumbleMotorToU16(float motor);
// Convert a raylib duration in seconds to an SDL duration in milliseconds.
std::uint32_t RumbleDurationToMs(float seconds);
// Resolve which SDL controller a GLFW joystick slot maps to, purely from GUID
// lists (no live devices). `glfwGuids[i]` is the GUID of GLFW joystick slot i
// (empty string if that slot is not connected); `sdlGuids[k]` is the GUID of
// the k-th SDL gamepad in SDL enumeration order. Returns the index into
// `sdlGuids` of the matching controller, or -1 if none. Identical GUIDs are
// matched by connection order (Nth GLFW device with a GUID -> Nth SDL device
// with that GUID), so this does NOT assume the two libraries enumerate alike.
int ResolveSdlIndexByGuidRank(const std::vector<std::string>& glfwGuids, int glfwSlot,
                              const std::vector<std::string>& sdlGuids);
#endif

} // namespace meowyrender::detail
