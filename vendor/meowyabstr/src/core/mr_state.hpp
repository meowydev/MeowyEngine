// meowyrender - src/core/mr_state.hpp  (internal)
// Shared global engine state: active backend, window, batch buffer, input,
// timing. Access via the accessor functions; a single translation unit owns
// the definition.
#pragma once

#include <memory>
#include <vector>
#include <array>
#include <string>
#include <cstdint>

#include "backend/render_backend.hpp"
#include "meowyrender/mr_types.hpp"
#include "meowyrender/meowyrender.hpp"  // for meowyrender::Backend

struct GLFWwindow;

namespace meowyrender::detail {

// Immediate-mode vertex batch. Drawing calls append here; the batch flushes
// when the bound texture changes, the draw mode changes, or the frame ends.
struct Batch {
    std::vector<backend::Vertex> verts;
    backend::DrawMode mode = backend::DrawMode::Triangles;
    unsigned int textureId = 0;
};

struct InputState {
    struct Gamepad {bool available=false;std::array<bool,18> buttons{};std::array<float,6> axes{};
                    int axisCount=0;std::string name;};
    std::array<Gamepad,16> gamepads{},previousGamepads{};
    // Current and previous frame key/button states, indexed by code.
    std::array<bool, 512> keysCurrent{};
    std::array<bool, 512> keysPrevious{};
    std::array<bool, 512> keysRepeat{};   // set when GLFW reports GLFW_REPEAT
    std::array<bool, 8> mouseCurrent{};
    std::array<bool, 8> mousePrevious{};
    Vector2 mousePosition{};
    Vector2 mousePrevPosition{};
    Vector2 mouseDelta{};
    Vector2 mouseOffset{};
    Vector2 mouseScale{1.0f, 1.0f};
    float mouseWheel = 0.0f;       // vertical wheel (raylib GetMouseWheelMove)
    Vector2 mouseWheelV{};         // both axes (raylib GetMouseWheelMoveV)
    int lastKeyPressed = 0;
    int lastCharPressed = 0;
    int exitKey = 256;             // KEY_ESCAPE by default (raylib)
    // Touch points (populated from mouse on desktop; real touches on visionOS).
    static constexpr int kMaxTouch = 8;
    std::array<Vector2, kMaxTouch> touchPositions{};
    std::array<int, kMaxTouch> touchIds{};
    int touchCount = 0;
    // Cursor visibility/lock state (desktop GLFW).
    bool cursorHidden = false;
    bool cursorOnScreen = true;
};

struct TimingState {
    double currentTime = 0.0;
    double previousTime = 0.0;
    double frameTime = 0.0;
    double targetFrameTime = 0.0; // 0 = uncapped
    int fps = 0;
    double fpsAccum = 0.0;
    int fpsFrames = 0;
};

struct EngineState {
    void* window = nullptr;
    std::unique_ptr<backend::RenderBackend> backend;

    // Runtime backend selection. preferredBackend is set by SetPreferredBackend
    // before InitWindow (Automatic = pick best). activeKind reflects the backend
    // actually initialized (valid only while backend != null).
    Backend preferredBackend = Backend::Automatic;
    backend::BackendKind activeKind{};

    // Dear ImGui: true once InitImGui() has successfully set up the active
    // backend's ImGui integration. Gates the per-frame new-frame/render hooks.
    bool imguiReady = false;

    // Deferred screenshot path. TakeScreenshot() records the path here rather
    // than reading immediately; EndDrawing() captures the frame after ImGui has
    // rendered but before the backend presents (so backends that expose only
    // the in-flight drawable/swapchain image - Metal, Vulkan - see valid data,
    // and the screenshot includes ImGui overlays). Empty = no pending capture.
    std::string pendingScreenshotPath;

    int screenWidth = 0;
    int screenHeight = 0;
    int targetWidth = 0,targetHeight = 0;
    bool stereo = false;
    VrStereoConfig stereoConfig;
    unsigned int skyboxShader=0,activeShader=0;
    bool windowReady = false;
    bool closeRequested = false;
    bool windowResized = false;
    bool fullscreen = false;
    bool eventWaiting = false;
    unsigned int configFlags = 0;

    Batch batch;
    Matrix projection;      // current 2D projection
    Matrix modelview;       // current 2D modelview (camera)

    Font defaultFont;
    bool defaultFontLoaded = false;

    // Cascaded shadow bookkeeping (set between BeginShadowCascades/EndShadowCascades).
    static constexpr int kMaxShadowCascades = 4;
    Matrix cascadeMatrices[kMaxShadowCascades]{};
    float cascadeSplits[kMaxShadowCascades]{};
    Matrix cascadeViewMatrix{};
    int cascadeCount = 0;

    // Optional user-set texture/source rect for shapes drawing (raylib parity).
    Texture2D shapesTexture{};
    Rectangle shapesTextureRec{0, 0, 1, 1};
    bool shapesTextureSet = false;

    InputState input;
    TimingState timing;
    backend::Lighting lighting;
};

// The single global engine state instance.
EngineState& State();
double ClockSeconds();

// Transparent (BLEND) draw deferral (defined in models.cpp). BeginMode3D enables
// deferral; EndMode3D flushes the back-to-front sorted queue.
void SetTransparentDeferral(bool on);
void FlushTransparentQueue();

// Batch helpers (defined in the batch/renderer core).
// Flush the current batch to the backend and clear it.
void FlushBatch();
void SubmitVertices(const backend::Vertex* vertices,size_t count,backend::DrawMode mode,unsigned int texture,
                    const Matrix* instances=nullptr,int instanceCount=0);
// Ensure the batch is set up for (mode, textureId); flushes first if needed.
void SetBatchState(backend::DrawMode mode, unsigned int textureId);
// Append one vertex to the current batch (screen-space position).
void PushVertex(float x, float y, float u, float v, Color c);

} // namespace meowyrender::detail
