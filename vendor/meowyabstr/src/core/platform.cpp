// meowyrender - src/core/platform.cpp  (internal)
#include "core/platform.hpp"
#include "core/mr_state.hpp"
#include "meowyrender/meowyrender.hpp"
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

// For the Vulkan backend, hand GLFW the Vulkan loader entry point directly so
// it doesn't have to dlopen libvulkan by bare name (which fails to find the
// Homebrew/MoltenVK loader outside the default dyld search paths on macOS).
#if defined(MEOWY_HAVE_BACKEND_VULKAN)
#  define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

// SDL is used solely for gamepad rumble on the desktop (GLFW) backend: GLFW 3.4
// exposes no haptics API. GLFW retains ownership of windowing and all input;
// SDL only initializes its gamepad subsystem. Guarded so builds without SDL
// (MEOWY_USE_SDL_GAMEPAD=OFF or SDL3 not found) still compile and degrade to a
// documented no-op.
#if defined(MEOWY_HAVE_SDL_GAMEPAD)
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#endif

namespace meowyrender::detail {

// Defined in modules/rcore_events.cpp; the GLFW drop callback fills it.
extern std::vector<std::string> g_droppedFiles;

namespace {
GLFWcursor* activeCursor=nullptr;

void KeyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (key < 0 || key >= 512) return;
    auto& in = State().input;
    if (action == GLFW_PRESS) {
        in.keysCurrent[static_cast<std::size_t>(key)] = true;
        in.lastKeyPressed = key;
        // Requested exit key closes the window (raylib SetExitKey behavior).
        if (key == in.exitKey && in.exitKey != 0) glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (action == GLFW_RELEASE) {
        in.keysCurrent[static_cast<std::size_t>(key)] = false;
    } else if (action == GLFW_REPEAT) {
        in.keysRepeat[static_cast<std::size_t>(key)] = true;
        in.lastKeyPressed = key;
    }
}

void CharCallback(GLFWwindow*, unsigned int codepoint) {
    State().input.lastCharPressed = static_cast<int>(codepoint);
}

void MouseButtonCallback(GLFWwindow*, int button, int action, int /*mods*/) {
    if (button < 0 || button >= 8) return;
    State().input.mouseCurrent[static_cast<std::size_t>(button)] =
        (action == GLFW_PRESS);
}

void CursorPosCallback(GLFWwindow*, double x, double y) {
    State().input.mousePosition = {static_cast<float>(x), static_cast<float>(y)};
}

void ScrollCallback(GLFWwindow*, double xoff, double yoff) {
    auto& in = State().input;
    in.mouseWheel = static_cast<float>(yoff);
    in.mouseWheelV = {static_cast<float>(xoff), static_cast<float>(yoff)};
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto& s = State();
    glfwGetWindowSize(window,&s.screenWidth,&s.screenHeight);
    s.windowResized = true;
    if (s.backend) s.backend->Resize(width, height);
}

void DropCallback(GLFWwindow*, int count, const char** paths) {
    g_droppedFiles.clear();
    for (int i = 0; i < count; ++i) g_droppedFiles.emplace_back(paths[i]);
}

} // namespace

// backend: 0=OpenGL, 1=Metal, 2=Vulkan (matches backend::BackendKind order).
void* PlatformCreateWindow(int width, int height, const char* title, int backend) {
    const bool isOpenGL = (backend == 0);
#if defined(MEOWY_HAVE_BACKEND_VULKAN)
    // Provide the loader explicitly (we link Vulkan::Vulkan, so the symbol is
    // available) before glfwInit so glfwVulkanSupported() works reliably. Only
    // needed for the Vulkan backend, but harmless to always set when compiled.
    if (backend == 2) glfwInitVulkanLoader(vkGetInstanceProcAddr);
#endif
    if (glfwInit() != GLFW_TRUE) return nullptr;

    // Backend chooses the client API at RUNTIME. OpenGL needs a GL context;
    // Metal and Vulkan manage their own surfaces, so request GLFW_NO_API.
    if (isOpenGL) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE); // required on macOS
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }

    // Apply window config flags set via SetConfigFlags before creation.
    const unsigned int flags = State().configFlags;
    glfwWindowHint(GLFW_RESIZABLE, (flags & 0x00000004) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, (flags & 0x00000008) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, (flags & 0x00000080) ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, (flags & 0x00001000) ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, (flags & 0x00000400) ? GLFW_TRUE : GLFW_FALSE);
    if (flags & 0x00000020) glfwWindowHint(GLFW_SAMPLES, 4); // MSAA 4x hint

    GLFWwindow* window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return nullptr;
    }

    // VSYNC hint (only meaningful once a GL context is current).
    if (isOpenGL && (flags & 0x00000040)) {
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
    }
    return window;
}

void PlatformDestroyWindow(void* handle) {
    auto* window=static_cast<GLFWwindow*>(handle);
    if(activeCursor){glfwDestroyCursor(activeCursor);activeCursor=nullptr;}
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
}

void PlatformInstallCallbacks(void* handle) {
    auto* window=static_cast<GLFWwindow*>(handle);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetDropCallback(window, DropCallback);
}

void PlatformPollInput() {
    auto& in = State().input;
    // Roll current -> previous before polling so "pressed/released" edges work.
    in.keysPrevious = in.keysCurrent;
    in.keysRepeat = {};
    in.mousePrevious = in.mouseCurrent;
    in.mousePrevPosition = in.mousePosition;
    in.mouseWheel = 0.0f;
    in.mouseWheelV = {0.0f, 0.0f};
    in.lastKeyPressed = 0;
    in.lastCharPressed = 0;
    in.previousGamepads=in.gamepads;

    glfwPollEvents();
    constexpr int buttons[]={-1,GLFW_GAMEPAD_BUTTON_DPAD_UP,GLFW_GAMEPAD_BUTTON_DPAD_RIGHT,GLFW_GAMEPAD_BUTTON_DPAD_DOWN,GLFW_GAMEPAD_BUTTON_DPAD_LEFT,
        GLFW_GAMEPAD_BUTTON_Y,GLFW_GAMEPAD_BUTTON_B,GLFW_GAMEPAD_BUTTON_A,GLFW_GAMEPAD_BUTTON_X,GLFW_GAMEPAD_BUTTON_LEFT_BUMPER,-1,
        GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER,-1,GLFW_GAMEPAD_BUTTON_BACK,GLFW_GAMEPAD_BUTTON_GUIDE,GLFW_GAMEPAD_BUTTON_START,GLFW_GAMEPAD_BUTTON_LEFT_THUMB,GLFW_GAMEPAD_BUTTON_RIGHT_THUMB};
    for(int i=0;i<16;++i) {
        auto& target=in.gamepads[i];target={};GLFWgamepadstate source{};
        target.available=glfwGetGamepadState(GLFW_JOYSTICK_1+i,&source)==GLFW_TRUE;
        if(!target.available)continue;
        for(int button=1;button<18;++button)if(buttons[button]>=0)target.buttons[button]=source.buttons[buttons[button]]==GLFW_PRESS;
        for(int axis=0;axis<6;++axis)target.axes[axis]=source.axes[axis];
        target.buttons[10]=source.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]>0.1f;target.buttons[12]=source.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]>0.1f;
        target.axisCount=6;
        const char* gpName=glfwGetGamepadName(GLFW_JOYSTICK_1+i);
        if(!gpName)gpName=glfwGetJoystickName(GLFW_JOYSTICK_1+i);
        target.name=gpName?gpName:"";
    }

    in.mouseDelta = {in.mousePosition.x - in.mousePrevPosition.x,
                     in.mousePosition.y - in.mousePrevPosition.y};

    // Desktop touch emulation: while the left mouse button is held, expose a
    // single touch point at the cursor so touch-based game code still works.
    if (in.mouseCurrent[static_cast<std::size_t>(MouseButton::Left)]) {
        in.touchCount = 1;
        in.touchPositions[0] = in.mousePosition;
        in.touchIds[0] = 0;
    } else {
        in.touchCount = 0;
    }

    // Cursor-on-screen: hovered and within the framebuffer bounds.
    auto* win = static_cast<GLFWwindow*>(State().window);
    if (win) {
        const bool hovered = glfwGetWindowAttrib(win, GLFW_HOVERED) == GLFW_TRUE;
        in.cursorOnScreen = hovered &&
            in.mousePosition.x >= 0 && in.mousePosition.y >= 0 &&
            in.mousePosition.x < State().screenWidth && in.mousePosition.y < State().screenHeight;
    }
}
void PlatformFramebufferSize(int* width,int* height) {glfwGetFramebufferSize(static_cast<GLFWwindow*>(State().window),width,height);}

#if defined(MEOWY_HAVE_SDL_GAMEPAD)
// ---------------------------------------------------------------------------
// Gamepad rumble via SDL (desktop GLFW backend only).
//
// The public gamepad index is a GLFW joystick slot. SDL enumerates devices
// independently, so we never assume slot == SDL index. Instead we match by
// joystick GUID: GLFW's glfwGetJoystickGUID and SDL's gamepad GUID both use the
// SDL GUID string format (vendor/product/version derived from the HID identity).
// When multiple identical controllers share a GUID we disambiguate by
// connection order (the Nth GLFW device with a GUID maps to the Nth SDL device
// with that GUID). SDL owns only its gamepad subsystem; GLFW keeps all input.
// ---------------------------------------------------------------------------

// Pure helpers (unit-tested via platform.hpp): motor [0,1] -> SDL Uint16,
// duration s -> ms, and GLFW-slot -> SDL-index mapping by GUID + rank.
std::uint16_t RumbleMotorToU16(float motor) {
    if (!(motor > 0.0f)) return 0;           // handles NaN and <=0
    if (motor >= 1.0f) return 0xFFFF;
    return static_cast<std::uint16_t>(motor * 65535.0f + 0.5f);
}
std::uint32_t RumbleDurationToMs(float seconds) {
    if (!(seconds > 0.0f)) return 0;          // 0 / negative / NaN -> stop
    double ms = static_cast<double>(seconds) * 1000.0;
    if (ms > 4294967294.0) return 0xFFFFFFFEu; // clamp below UINT32_MAX
    return static_cast<std::uint32_t>(ms + 0.5);
}
int ResolveSdlIndexByGuidRank(const std::vector<std::string>& glfwGuids, int glfwSlot,
                              const std::vector<std::string>& sdlGuids) {
    if (glfwSlot < 0 || glfwSlot >= static_cast<int>(glfwGuids.size())) return -1;
    const std::string& guid = glfwGuids[glfwSlot];
    if (guid.empty()) return -1;              // slot not connected
    // Rank = how many connected GLFW slots up to and including glfwSlot share
    // this GUID (1-based among identical devices).
    int rank = 0;
    for (int s = 0; s <= glfwSlot; ++s)
        if (!glfwGuids[s].empty() && glfwGuids[s] == guid) ++rank;
    if (rank == 0) return -1;
    // Return the rank-th SDL controller with the same GUID.
    int seen = 0;
    for (int i = 0; i < static_cast<int>(sdlGuids.size()); ++i)
        if (sdlGuids[i] == guid && ++seen == rank) return i;
    return -1;
}

class GamepadRumbleManager {
public:
    bool Ready() const { return ready_; }

    // Lazily initialize only SDL's gamepad subsystem (no video / no event-loop
    // ownership). Returns false if SDL is unavailable.
    bool Ensure() {
        if (ready_) return true;
        if (triedInit_ && !ready_) return false;
        triedInit_ = true;
        // SDL_InitSubSystem is refcounted and safe alongside anything else; we
        // deliberately avoid SDL_INIT_VIDEO/EVENTS so we don't fight GLFW.
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            std::fprintf(stderr, "[meowyrender] SDL gamepad init failed: %s\n", SDL_GetError());
            return false;
        }
        ready_ = true;
        return true;
    }

    // Resolve the SDL controller matching GLFW joystick slot `glfwSlot`, open it
    // (cached), and rumble. Graceful on every failure (no crash, no throw).
    void Rumble(int glfwSlot, float leftMotor, float rightMotor, float seconds) {
        if (glfwSlot < 0 || glfwSlot >= 16) return;
        if (!Ensure()) return;
        // Refresh SDL's device list/state without pumping the shared event queue
        // in a way that would steal GLFW's events.
        SDL_UpdateGamepads();

        SDL_JoystickID id = ResolveInstanceForSlot(glfwSlot);
        if (id == 0) return;                       // no matching SDL controller

        SDL_Gamepad* gp = OpenCached(id);
        if (!gp) return;                           // couldn't open / disconnected

        const std::uint16_t low = RumbleMotorToU16(leftMotor);   // low-frequency (large) motor
        const std::uint16_t high = RumbleMotorToU16(rightMotor); // high-frequency (small) motor
        const std::uint32_t ms = RumbleDurationToMs(seconds);
        if (!SDL_RumbleGamepad(gp, low, high, ms)) {
            // Controller has no rumble motors, or the driver rejected it. Not an
            // error we surface to the app; rumble simply does nothing here.
        }
    }

    // Map a GLFW joystick slot to an SDL joystick instance id. Builds the GUID
    // lists from the live GLFW and SDL device sets, then defers the matching to
    // the pure, unit-tested ResolveSdlIndexByGuidRank. Returns 0 if no match.
    SDL_JoystickID ResolveInstanceForSlot(int glfwSlot) {
        // GLFW slot GUIDs (index i = GLFW_JOYSTICK_1 + i; "" if not present).
        std::vector<std::string> glfwGuids(16);
        for (int s = 0; s < 16; ++s) {
            if (!glfwJoystickPresent(GLFW_JOYSTICK_1 + s)) continue;
            const char* g = glfwGetJoystickGUID(GLFW_JOYSTICK_1 + s);
            if (g) glfwGuids[s] = g;
        }
        // SDL gamepads in enumeration order, with parallel instance-id list.
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids) return 0;
        std::vector<std::string> sdlGuids;
        std::vector<SDL_JoystickID> sdlIds;
        sdlGuids.reserve(count); sdlIds.reserve(count);
        for (int i = 0; i < count; ++i) {
            char sdlGuid[64] = {0};
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]), sdlGuid, sizeof(sdlGuid));
            sdlGuids.emplace_back(sdlGuid);
            sdlIds.push_back(ids[i]);
        }
        SDL_free(ids);
        int idx = ResolveSdlIndexByGuidRank(glfwGuids, glfwSlot, sdlGuids);
        return idx >= 0 ? sdlIds[static_cast<std::size_t>(idx)] : 0;
    }

    void Shutdown() {
        for (auto& [id, gp] : open_) if (gp) SDL_CloseGamepad(gp);
        open_.clear();
        if (ready_) { SDL_QuitSubSystem(SDL_INIT_GAMEPAD); ready_ = false; }
        triedInit_ = false;
    }

    ~GamepadRumbleManager() { Shutdown(); }

private:
    // Open (or reuse) an SDL_Gamepad for an instance id, dropping stale handles
    // for controllers that have disconnected.
    SDL_Gamepad* OpenCached(SDL_JoystickID id) {
        if (auto it = open_.find(id); it != open_.end()) {
            if (it->second && SDL_GamepadConnected(it->second)) return it->second;
            if (it->second) SDL_CloseGamepad(it->second);
            open_.erase(it);
        }
        if (!SDL_IsGamepad(id)) return nullptr;
        SDL_Gamepad* gp = SDL_OpenGamepad(id);
        if (gp) open_.emplace(id, gp);
        return gp;
    }

    bool ready_ = false;
    bool triedInit_ = false;
    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> open_;
};

GamepadRumbleManager& GamepadRumble() {
    static GamepadRumbleManager instance;
    return instance;
}
void ShutdownGamepadRumble() { GamepadRumble().Shutdown(); }
#else
void ShutdownGamepadRumble() {}
#endif // MEOWY_HAVE_SDL_GAMEPAD

} // namespace meowyrender::detail

namespace meowyrender {
using detail::State;
void OpenURL(const std::string& url) {
    if(!url.starts_with("https://")&&!url.starts_with("http://")&&!url.starts_with("mailto:"))throw std::invalid_argument("Unsupported URL scheme");
#if defined(_WIN32)
    ShellExecuteA(nullptr,"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
#else
#if defined(__APPLE__)
    const char* launcher="open";
#else
    const char* launcher="xdg-open";
#endif
    char* arguments[]={const_cast<char*>(launcher),const_cast<char*>(url.c_str()),nullptr};pid_t process=0;
    if(posix_spawnp(&process,launcher,nullptr,nullptr,arguments,environ)==0)waitpid(process,nullptr,0);
#endif
}
void SetMouseCursor(MouseCursor cursor) {
    if(!State().window)return;
    constexpr int shapes[]={GLFW_ARROW_CURSOR,GLFW_ARROW_CURSOR,GLFW_IBEAM_CURSOR,GLFW_CROSSHAIR_CURSOR,GLFW_POINTING_HAND_CURSOR,GLFW_RESIZE_EW_CURSOR,GLFW_RESIZE_NS_CURSOR,GLFW_RESIZE_NWSE_CURSOR,GLFW_RESIZE_NESW_CURSOR,GLFW_RESIZE_ALL_CURSOR,GLFW_NOT_ALLOWED_CURSOR};
    int index=static_cast<int>(cursor);if(index<0||index>=11)return;
    auto* next=glfwCreateStandardCursor(shapes[index]);glfwSetCursor(static_cast<GLFWwindow*>(State().window),next);
    if(detail::activeCursor)glfwDestroyCursor(detail::activeCursor);detail::activeCursor=next;
}
bool WindowShouldClose() {
    auto& s = State();
    if (!static_cast<GLFWwindow*>(s.window)) return true;
    return s.closeRequested || glfwWindowShouldClose(static_cast<GLFWwindow*>(s.window)) != 0;
}

void ToggleFullscreen() {
    // Minimal implementation: toggles a borderless fullscreen on the primary
    // monitor. Full multi-monitor handling is left to platform extension.
    auto& s = State();
    if (!static_cast<GLFWwindow*>(s.window)) return;
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    if (!s.fullscreen) {
        glfwSetWindowMonitor(static_cast<GLFWwindow*>(s.window), mon, 0, 0, mode->width, mode->height,
                             mode->refreshRate);
        s.fullscreen = true;
    } else {
        glfwSetWindowMonitor(static_cast<GLFWwindow*>(s.window), nullptr, 100, 100,
                             s.screenWidth, s.screenHeight, 0);
        s.fullscreen = false;
    }
}

void SetWindowTitle(const std::string& title) {
    if (static_cast<GLFWwindow*>(State().window)) glfwSetWindowTitle(static_cast<GLFWwindow*>(State().window), title.c_str());
}
void SetWindowSize(int w, int h) {
    if (static_cast<GLFWwindow*>(State().window)) glfwSetWindowSize(static_cast<GLFWwindow*>(State().window), w, h);
}
void SetWindowPosition(int x, int y) {
    if (static_cast<GLFWwindow*>(State().window)) glfwSetWindowPos(static_cast<GLFWwindow*>(State().window), x, y);
}
int GetScreenWidth() { return State().screenWidth; }
int GetScreenHeight() { return State().screenHeight; }
Vector2 GetWindowPosition() {
    int x = 0, y = 0;
    if (static_cast<GLFWwindow*>(State().window)) glfwGetWindowPos(static_cast<GLFWwindow*>(State().window), &x, &y);
    return {static_cast<float>(x), static_cast<float>(y)};
}// ===========================================================================
// Monitors
// ===========================================================================
int GetMonitorCount() {
    int count = 0;
    glfwGetMonitors(&count);
    return count;
}

int GetCurrentMonitor() {
    // Best-effort: find the monitor containing the window center.
    auto& s = State();
    if (!static_cast<GLFWwindow*>(s.window)) return 0;
    int wx = 0, wy = 0;
    glfwGetWindowPos(static_cast<GLFWwindow*>(s.window), &wx, &wy);
    int count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0;
        glfwGetMonitorPos(mons[i], &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(mons[i]);
        if (wx >= mx && wx < mx + mode->width && wy >= my && wy < my + mode->height)
            return i;
    }
    return 0;
}

static GLFWmonitor* MonitorAt(int index) {
    int count = 0;
    GLFWmonitor** mons = glfwGetMonitors(&count);
    if (index < 0 || index >= count) return glfwGetPrimaryMonitor();
    return mons[index];
}

Vector2 GetMonitorPosition(int monitor) {
    int x = 0, y = 0;
    glfwGetMonitorPos(MonitorAt(monitor), &x, &y);
    return {static_cast<float>(x), static_cast<float>(y)};
}
int GetMonitorWidth(int monitor) {
    const GLFWvidmode* m = glfwGetVideoMode(MonitorAt(monitor));
    return m ? m->width : 0;
}
int GetMonitorHeight(int monitor) {
    const GLFWvidmode* m = glfwGetVideoMode(MonitorAt(monitor));
    return m ? m->height : 0;
}
int GetMonitorRefreshRate(int monitor) {
    const GLFWvidmode* m = glfwGetVideoMode(MonitorAt(monitor));
    return m ? m->refreshRate : 0;
}
const char* GetMonitorName(int monitor) {
    const char* n = glfwGetMonitorName(MonitorAt(monitor));
    return n ? n : "";
}
int GetMonitorPhysicalWidth(int monitor) {
    int w = 0, h = 0;
    glfwGetMonitorPhysicalSize(MonitorAt(monitor), &w, &h);
    return w;
}
int GetMonitorPhysicalHeight(int monitor) {
    int w = 0, h = 0;
    glfwGetMonitorPhysicalSize(MonitorAt(monitor), &w, &h);
    return h;
}
void SetWindowMonitor(int monitor) {
    auto& s = State();
    if (!static_cast<GLFWwindow*>(s.window)) return;
    GLFWmonitor* mon = MonitorAt(monitor);
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    glfwSetWindowMonitor(static_cast<GLFWwindow*>(s.window), mon, 0, 0, mode->width, mode->height, mode->refreshRate);
}

// ===========================================================================
// Clipboard
// ===========================================================================
void SetClipboardText(const std::string& text) {
    if (static_cast<GLFWwindow*>(State().window)) glfwSetClipboardString(static_cast<GLFWwindow*>(State().window), text.c_str());
}
const char* GetClipboardText() {
    if (!static_cast<GLFWwindow*>(State().window)) return "";
    const char* s = glfwGetClipboardString(static_cast<GLFWwindow*>(State().window));
    return s ? s : "";
}

// ===========================================================================
// Render (framebuffer) dimensions + DPI
// ===========================================================================
int GetRenderWidth() {
    auto* w = static_cast<GLFWwindow*>(State().window);
    if (!w) return State().screenWidth;
    int fw = 0, fh = 0; glfwGetFramebufferSize(w, &fw, &fh);
    return fw;
}
int GetRenderHeight() {
    auto* w = static_cast<GLFWwindow*>(State().window);
    if (!w) return State().screenHeight;
    int fw = 0, fh = 0; glfwGetFramebufferSize(w, &fw, &fh);
    return fh;
}
Vector2 GetWindowScaleDPI() {
    auto* w = static_cast<GLFWwindow*>(State().window);
    if (!w) return {1.0f, 1.0f};
    float sx = 1.0f, sy = 1.0f; glfwGetWindowContentScale(w, &sx, &sy);
    return {sx, sy};
}
void* GetWindowHandle() { return State().window; }

// ===========================================================================
// Window state (raylib FLAG_* bits map to GLFW attributes/behaviors)
// ===========================================================================
namespace {
GLFWwindow* Win() { return static_cast<GLFWwindow*>(State().window); }
}

bool IsWindowState(unsigned int flag) {
    auto* w = Win();
    if (!w) return (State().configFlags & flag) != 0;
    switch (flag) {
        case FLAG_FULLSCREEN_MODE:    return State().fullscreen;
        case FLAG_WINDOW_RESIZABLE:   return glfwGetWindowAttrib(w, GLFW_RESIZABLE) == GLFW_TRUE;
        case FLAG_WINDOW_UNDECORATED: return glfwGetWindowAttrib(w, GLFW_DECORATED) == GLFW_FALSE;
        case FLAG_WINDOW_HIDDEN:      return glfwGetWindowAttrib(w, GLFW_VISIBLE) == GLFW_FALSE;
        case FLAG_WINDOW_MINIMIZED:   return glfwGetWindowAttrib(w, GLFW_ICONIFIED) == GLFW_TRUE;
        case FLAG_WINDOW_MAXIMIZED:   return glfwGetWindowAttrib(w, GLFW_MAXIMIZED) == GLFW_TRUE;
        case FLAG_WINDOW_TOPMOST:     return glfwGetWindowAttrib(w, GLFW_FLOATING) == GLFW_TRUE;
        default:                      return (State().configFlags & flag) != 0;
    }
}
void SetWindowState(unsigned int flags) {
    auto* w = Win();
    State().configFlags |= flags;
    if (!w) return;
    if (flags & FLAG_WINDOW_RESIZABLE)   glfwSetWindowAttrib(w, GLFW_RESIZABLE, GLFW_TRUE);
    if (flags & FLAG_WINDOW_UNDECORATED) glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_FALSE);
    if (flags & FLAG_WINDOW_TOPMOST)     glfwSetWindowAttrib(w, GLFW_FLOATING, GLFW_TRUE);
    if (flags & FLAG_WINDOW_HIDDEN)      glfwHideWindow(w);
    if (flags & FLAG_WINDOW_MINIMIZED)   glfwIconifyWindow(w);
    if (flags & FLAG_WINDOW_MAXIMIZED)   glfwMaximizeWindow(w);
}
void ClearWindowState(unsigned int flags) {
    auto* w = Win();
    State().configFlags &= ~flags;
    if (!w) return;
    if (flags & FLAG_WINDOW_RESIZABLE)   glfwSetWindowAttrib(w, GLFW_RESIZABLE, GLFW_FALSE);
    if (flags & FLAG_WINDOW_UNDECORATED) glfwSetWindowAttrib(w, GLFW_DECORATED, GLFW_TRUE);
    if (flags & FLAG_WINDOW_TOPMOST)     glfwSetWindowAttrib(w, GLFW_FLOATING, GLFW_FALSE);
    if (flags & FLAG_WINDOW_HIDDEN)      glfwShowWindow(w);
    if (flags & FLAG_WINDOW_MINIMIZED)   glfwRestoreWindow(w);
    if (flags & FLAG_WINDOW_MAXIMIZED)   glfwRestoreWindow(w);
}
bool IsWindowHidden()    { return Win() && glfwGetWindowAttrib(Win(), GLFW_VISIBLE) == GLFW_FALSE; }
bool IsWindowMinimized() { return Win() && glfwGetWindowAttrib(Win(), GLFW_ICONIFIED) == GLFW_TRUE; }
bool IsWindowMaximized() { return Win() && glfwGetWindowAttrib(Win(), GLFW_MAXIMIZED) == GLFW_TRUE; }
bool IsWindowFocused()   { return !Win() || glfwGetWindowAttrib(Win(), GLFW_FOCUSED) == GLFW_TRUE; }
void MinimizeWindow()    { if (Win()) glfwIconifyWindow(Win()); }
void MaximizeWindow()    { if (Win()) glfwMaximizeWindow(Win()); }
void RestoreWindow()     { if (Win()) glfwRestoreWindow(Win()); }
void SetWindowMinSize(int width, int height) {
    if (Win()) glfwSetWindowSizeLimits(Win(), width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
}
void SetWindowMaxSize(int width, int height) {
    if (Win()) glfwSetWindowSizeLimits(Win(), GLFW_DONT_CARE, GLFW_DONT_CARE, width, height);
}
void SetWindowOpacity(float opacity) { if (Win()) glfwSetWindowOpacity(Win(), opacity); }
void SetWindowFocused()  { if (Win()) glfwFocusWindow(Win()); }
void SetWindowIcon(Image image) {
    if (!Win() || !image.data) return;
    GLFWimage icon{}; icon.width = image.width; icon.height = image.height;
    icon.pixels = static_cast<unsigned char*>(image.data); // expects RGBA8
    glfwSetWindowIcon(Win(), 1, &icon);
}
void SetWindowIcons(Image* images, int count) {
    if (!Win() || !images || count <= 0) return;
    std::vector<GLFWimage> icons(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        icons[i].width = images[i].width; icons[i].height = images[i].height;
        icons[i].pixels = static_cast<unsigned char*>(images[i].data);
    }
    glfwSetWindowIcon(Win(), count, icons.data());
}

// ===========================================================================
// Cursor
// ===========================================================================
void ShowCursor()      { if (Win()) glfwSetInputMode(Win(), GLFW_CURSOR, GLFW_CURSOR_NORMAL); State().input.cursorHidden = false; }
void HideCursor()      { if (Win()) glfwSetInputMode(Win(), GLFW_CURSOR, GLFW_CURSOR_HIDDEN); State().input.cursorHidden = true; }
bool IsCursorHidden()  { return State().input.cursorHidden; }
void EnableCursor()    { if (Win()) glfwSetInputMode(Win(), GLFW_CURSOR, GLFW_CURSOR_NORMAL); State().input.cursorHidden = false; }
void DisableCursor()   { if (Win()) glfwSetInputMode(Win(), GLFW_CURSOR, GLFW_CURSOR_DISABLED); State().input.cursorHidden = true; }
bool IsCursorOnScreen(){ return State().input.cursorOnScreen; }

// ===========================================================================
// Mouse position / keyboard / gamepad platform bits
// ===========================================================================
void SetMousePosition(int x, int y) {
    if (Win()) glfwSetCursorPos(Win(), x, y);
    State().input.mousePosition = {static_cast<float>(x), static_cast<float>(y)};
}
const char* GetKeyName(KeyboardKey key) {
    const char* n = glfwGetKeyName(static_cast<int>(key), 0);
    return n ? n : "";
}
int SetGamepadMappings(const std::string& mappings) {
    int glfwResult = glfwUpdateGamepadMappings(mappings.c_str()) == GLFW_TRUE ? 1 : 0;
#if defined(MEOWY_HAVE_SDL_GAMEPAD)
    // Keep SDL's mapping DB in sync so the rumble path resolves the same
    // controllers GLFW does. Best-effort; failure doesn't affect the GLFW result.
    if (detail::GamepadRumble().Ready()) SDL_AddGamepadMapping(mappings.c_str());
#endif
    return glfwResult;
}

// SetGamepadVibration: the public `gamepad` index is a GLFW joystick slot
// (0-based). GLFW 3.4 cannot rumble, so we drive SDL's gamepad subsystem. SDL
// enumerates devices independently, so we map the GLFW slot to the matching SDL
// controller by joystick GUID (both libraries use the same SDL-format GUID),
// disambiguating identical GUIDs by connection order. Motors are [0,1]; duration
// is in seconds (raylib semantics). Zero motors / zero duration stop rumble.
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration) {
#if defined(MEOWY_HAVE_SDL_GAMEPAD)
    detail::GamepadRumble().Rumble(gamepad, leftMotor, rightMotor, duration);
#else
    // Built without SDL: no desktop haptics available. Documented no-op.
    (void)gamepad; (void)leftMotor; (void)rightMotor; (void)duration;
#endif
}


void RunApplication(int width,int height,const std::string& title,std::function<void()> frame,std::function<void()> initialize,std::function<void()> cleanup) {
    if(!frame)throw std::invalid_argument("RunApplication requires a frame callback");
    InitWindow(width,height,title);
    bool initialized=false,cleanupStarted=false;
    try {if(initialize)initialize();initialized=true;while(!WindowShouldClose()){BeginDrawing();frame();EndDrawing();}cleanupStarted=true;if(cleanup)cleanup();}
    catch(...){if(initialized&&!cleanupStarted&&cleanup){try{cleanup();}catch(...){}}CloseWindow();throw;}
    CloseWindow();
}
}
