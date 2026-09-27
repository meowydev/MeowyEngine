// meowyrender - src/core/core.cpp
// Core lifecycle + batch renderer + timing. Owns the single EngineState.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"
#include "core/platform.hpp"
#include "backend/render_backend.hpp"



#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace meowyrender {

// The default font is built by the text module.
namespace detail { Font BuildDefaultFont(); }

namespace detail {
double ClockSeconds() {static const auto epoch=std::chrono::steady_clock::now();return std::chrono::duration<double>(std::chrono::steady_clock::now()-epoch).count();}

EngineState& State() {
    static EngineState state;
    return state;
}

void FlushBatch() {
    auto& s = State();
    if (s.batch.verts.empty() || !s.backend) {
        s.batch.verts.clear();
        return;
    }
    SubmitVertices(s.batch.verts.data(), s.batch.verts.size(),s.batch.mode,s.batch.textureId);
    s.batch.verts.clear();
}

void SubmitVertices(const backend::Vertex* vertices,size_t count,backend::DrawMode mode,unsigned int texture,const Matrix* instances,int instanceCount) {
    auto& s=State();if(!s.backend)return;
    auto draw=[&]{if(instances)s.backend->DrawVerticesInstanced(vertices,count,texture,instances,instanceCount);
                 else s.backend->DrawVertices(vertices,count,mode,texture);};
    if(!s.stereo) {draw();return;}
    auto restore=[&]{s.backend->SetViewport({0,0,1,1});s.backend->SetProjection(s.projection);s.backend->SetModelview(s.modelview);};
    try {
        for(int eye=0;eye<2;++eye) {
            s.backend->SetViewport({eye*0.5f,0,0.5f,1});
            s.backend->SetProjection(s.stereoConfig.projection[eye]);
            s.backend->SetModelview(MatrixMultiply(s.modelview,s.stereoConfig.viewOffset[eye]));
            draw();
        }
    } catch(...) {restore();throw;}
    restore();
}

void SetBatchState(backend::DrawMode mode, unsigned int textureId) {
    auto& s = State();
    if (s.batch.mode != mode || s.batch.textureId != textureId) {
        FlushBatch();
        s.batch.mode = mode;
        s.batch.textureId = textureId;
    }
}

void PushVertex(float x, float y, float u, float v, Color c) {
    State().batch.verts.push_back(
        backend::Vertex{x, y, 0.0f, u, v, c.r, c.g, c.b, c.a});
}

} // namespace detail

using detail::State;

// ---------------------------------------------------------------------------
// Backend selection + info
// ---------------------------------------------------------------------------
namespace {
using backend::BackendKind;

// Map the public Backend enum <-> the internal BackendKind (Automatic has none).
bool ToKind(Backend b, BackendKind& out) {
    switch (b) {
        case Backend::OpenGL: out = BackendKind::OpenGL; return true;
        case Backend::Metal:  out = BackendKind::Metal;  return true;
        case Backend::Vulkan: out = BackendKind::Vulkan; return true;
        case Backend::Automatic: return false;
    }
    return false;
}
Backend FromKind(BackendKind k) {
    switch (k) {
        case BackendKind::OpenGL: return Backend::OpenGL;
        case BackendKind::Metal:  return Backend::Metal;
        case BackendKind::Vulkan: return Backend::Vulkan;
    }
    return Backend::OpenGL;
}

// The platform's automatic preference order (most-preferred first). Only
// backends compiled into this build are listed by the caller.
std::vector<BackendKind> PlatformPreferenceOrder() {
#if defined(MEOWY_PLATFORM_VISIONOS)
    return {BackendKind::Metal};
#elif defined(__APPLE__)
    return {BackendKind::Metal, BackendKind::Vulkan, BackendKind::OpenGL};
#else
    // Windows and Linux: Vulkan first, then OpenGL. (No Metal.)
    return {BackendKind::Vulkan, BackendKind::OpenGL};
#endif
}

// Cheap runtime availability probe: is the backend compiled AND usable now?
// (Deliberately conservative -- the definitive check is a real Init() attempt,
// which the fallback loop performs.) A forced-failure override also reports the
// backend as usable here so the *ordering* is exercised; the failure surfaces
// during Init to test fallback.
bool BackendUsableNow(BackendKind kind) {
    if (!backend::IsBackendCompiled(kind)) return false;
    return true; // real usability is confirmed by Init(); see the fallback loop.
}

const char* KindShortName(BackendKind k) {
    switch (k) { case BackendKind::OpenGL: return "OpenGL"; case BackendKind::Metal: return "Metal"; case BackendKind::Vulkan: return "Vulkan"; }
    return "Unknown";
}

// A deterministic init-failure hook for testing fallback. MEOWY_FORCE_BACKEND_FAIL
// is a comma-separated list of backend names whose Init should be treated as
// failed (e.g. "metal" or "metal,vulkan").
bool ForcedToFail(BackendKind kind) {
    const char* env = std::getenv("MEOWY_FORCE_BACKEND_FAIL");
    if (!env || !*env) return false;
    std::string list = env, want = KindShortName(kind);
    for (char& c : list) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // substring match on comma-separated tokens
    std::size_t pos = 0;
    while (pos < list.size()) {
        std::size_t comma = list.find(',', pos);
        std::string tok = list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        if (tok == want) return true;
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return false;
}
} // namespace

void SetPreferredBackend(Backend backend) {
    auto& s = State();
    if (s.windowReady && s.backend) {
        std::fprintf(stderr,
            "[meowyrender] SetPreferredBackend(%s) ignored: a window is already "
            "initialized with the %s backend. GPU resources belong to the active "
            "backend; live backend switching is not supported. Call it before "
            "InitWindow (or after CloseWindow).\n",
            GetBackendEnumName(backend), GetActiveBackendName());
        return;
    }
    s.preferredBackend = backend;
}
Backend GetPreferredBackend() { return State().preferredBackend; }

Backend GetActiveBackend() {
    auto& s = State();
    return s.backend ? FromKind(s.activeKind) : Backend::Automatic;
}
const char* GetActiveBackendName() {
    auto& s = State();
    return s.backend ? s.backend->Name() : "none";
}
bool IsBackendCompiled(Backend backend) {
    if (backend == Backend::Automatic)
        return backend::IsBackendCompiled(BackendKind::OpenGL) ||
               backend::IsBackendCompiled(BackendKind::Metal) ||
               backend::IsBackendCompiled(BackendKind::Vulkan);
    BackendKind k; return ToKind(backend, k) && backend::IsBackendCompiled(k);
}
bool IsBackendAvailable(Backend backend) {
    if (backend == Backend::Automatic) {
        for (BackendKind k : PlatformPreferenceOrder())
            if (BackendUsableNow(k)) return true;
        return false;
    }
    BackendKind k; return ToKind(backend, k) && BackendUsableNow(k);
}
const char* GetBackendEnumName(Backend backend) {
    switch (backend) {
        case Backend::Automatic: return "Automatic";
        case Backend::Metal:     return "Metal";
        case Backend::Vulkan:    return "Vulkan";
        case Backend::OpenGL:    return "OpenGL";
    }
    return "Unknown";
}
bool ParseBackend(const std::string& name, Backend& out) {
    std::string n; n.reserve(name.size());
    for (char c : name) n += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (n.empty() || n == "auto" || n == "automatic") { out = Backend::Automatic; return true; }
    if (n == "metal")  { out = Backend::Metal;  return true; }
    if (n == "vulkan") { out = Backend::Vulkan; return true; }
    if (n == "opengl" || n == "gl") { out = Backend::OpenGL; return true; }
    return false;
}

// Legacy compatibility: report the ACTIVE backend at runtime (was compile-time).
BackendType GetBackendType() {
    switch (State().activeKind) {
        case BackendKind::OpenGL: return BackendType::OpenGL;
        case BackendKind::Metal:  return BackendType::Metal;
        case BackendKind::Vulkan: return BackendType::Vulkan;
    }
    return BackendType::OpenGL;
}
const char* GetBackendName() {
    auto& s = State();
    return s.backend ? s.backend->Name() : "none";
}

// --- Dear ImGui integration -------------------------------------------------
bool IsImGuiAvailable() {
#if defined(MEOWY_WITH_IMGUI)
    // Compiled in; availability then depends on a live backend that supports it.
    // We can only truly know after InitWindow, but report true for the compiled
    // capability so callers can branch before a window exists.
    return true;
#else
    return false;
#endif
}

bool InitImGui() {
#if defined(MEOWY_WITH_IMGUI)
    auto& s = State();
    if (!s.backend) {
        std::fprintf(stderr, "[meowyrender] InitImGui: call after InitWindow()\n");
        return false;
    }
    if (s.imguiReady) return true;  // idempotent
    if (!s.backend->ImGuiInit(s.window)) {
        std::fprintf(stderr, "[meowyrender] InitImGui: the %s backend does not support ImGui\n",
                     s.backend->Name());
        return false;
    }
    s.imguiReady = true;
    std::printf("[meowyrender] Dear ImGui ready on %s\n", s.backend->Name());
    return true;
#else
    std::fprintf(stderr, "[meowyrender] InitImGui: built without ImGui (configure with MEOWY_WITH_IMGUI=ON)\n");
    return false;
#endif
}

void ShutdownImGui() {
    auto& s = State();
    if (s.imguiReady && s.backend) s.backend->ImGuiShutdown();
    s.imguiReady = false;
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------
void InitWindow(int width, int height, const std::string& title) {
    auto& s = State();
    if (s.window || s.backend) CloseWindow();
    if (width <= 0 || height <= 0) throw std::invalid_argument("InitWindow: dimensions must be positive");
    s.input = {};
    s.closeRequested=false;
    s.timing = {};
    s.screenWidth = width;
    s.screenHeight = height;

    // --- Resolve the ordered list of backends to try ------------------------
    // Precedence (highest first): explicit SetPreferredBackend > MEOWY_BACKEND
    // env var > automatic per-platform order. In every case, if the chosen
    // backend fails we fall back through the remaining supported backends.
    Backend preferred = s.preferredBackend;
    const char* source = "automatic";
    if (preferred != Backend::Automatic) {
        source = "SetPreferredBackend";
    } else if (const char* env = std::getenv("MEOWY_BACKEND"); env && *env) {
        Backend parsed;
        if (ParseBackend(env, parsed)) { preferred = parsed; source = "MEOWY_BACKEND env"; }
        else std::fprintf(stderr, "[meowyrender] ignoring unrecognized MEOWY_BACKEND='%s'\n", env);
    }

    std::vector<BackendKind> order = PlatformPreferenceOrder();
    std::vector<BackendKind> candidates;
    BackendKind preferredKind{};
    if (preferred != Backend::Automatic && ToKind(preferred, preferredKind)
        && backend::IsBackendCompiled(preferredKind)) {
        candidates.push_back(preferredKind);           // preferred first
    } else if (preferred != Backend::Automatic) {
        std::fprintf(stderr, "[meowyrender] requested backend %s (%s) is not compiled in; "
                     "using automatic selection\n", GetBackendEnumName(preferred), source);
    }
    for (BackendKind k : order)                         // then the platform order
        if (backend::IsBackendCompiled(k) &&
            std::find(candidates.begin(), candidates.end(), k) == candidates.end())
            candidates.push_back(k);

    if (candidates.empty())
        throw std::runtime_error("InitWindow: no rendering backend is compiled into this build");

    // --- Try each candidate, cleaning up fully between failed attempts ------
    std::string lastError;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const BackendKind kind = candidates[i];
        std::printf("[meowyrender] attempting backend: %s\n", KindShortName(kind));

        // Window is created per-attempt with backend-appropriate hints. On
        // visionOS the platform returns the pre-existing UIView regardless.
        s.window = detail::PlatformCreateWindow(width, height, title.c_str(), static_cast<int>(kind));
        if (!s.window) {
            lastError = std::string("window creation failed for ") + KindShortName(kind);
            std::fprintf(stderr, "[meowyrender]   %s\n", lastError.c_str());
            continue; // no window to tear down; try next
        }
        detail::PlatformInstallCallbacks(s.window);

        s.backend = backend::CreateBackend(kind);
        bool ok = false;
        if (s.backend && !ForcedToFail(kind)) {
            backend::ContextConfig cfg;
            cfg.width = width; cfg.height = height;
            cfg.configFlags = s.configFlags; cfg.window = s.window;
            detail::PlatformFramebufferSize(&cfg.width, &cfg.height);
            try { ok = s.backend->Init(cfg); }
            catch (const std::exception& e) { lastError = e.what(); ok = false; }
            catch (...) { lastError = "unknown exception during Init"; ok = false; }
        } else if (ForcedToFail(kind)) {
            lastError = std::string(KindShortName(kind)) + " forced to fail (MEOWY_FORCE_BACKEND_FAIL)";
        } else {
            lastError = std::string("could not construct ") + KindShortName(kind) + " backend";
        }

        if (ok) {
            s.activeKind = kind;
            break;
        }

        // Failed: tear down everything this attempt created before the next one.
        std::fprintf(stderr, "[meowyrender]   backend %s unavailable: %s\n",
                     KindShortName(kind), lastError.empty() ? "initialization failed" : lastError.c_str());
        if (s.backend) { s.backend->Shutdown(); s.backend.reset(); }
        if (s.window) { detail::PlatformDestroyWindow(s.window); s.window = nullptr; }
        detail::ShutdownGamepadRumble();
    }

    if (!s.backend) {
        // Every candidate failed; leave no window/backend behind.
        throw std::runtime_error("InitWindow: no available graphics backend could be initialized" +
                                 (lastError.empty() ? std::string() : (" (last error: " + lastError + ")")));
    }

    // Backend is live: finish window bring-up.
    try {
        s.defaultFont = detail::BuildDefaultFont();
        s.defaultFontLoaded = true;
    } catch (...) {
        CloseWindow();
        throw;
    }

    s.timing.previousTime = detail::ClockSeconds();
    s.windowReady = true;
    detail::PlatformPollInput();
    if (source && preferred != Backend::Automatic && FromKind(s.activeKind) != preferred) {
        std::printf("[meowyrender] window ready | backend: %s (fell back from %s)\n",
                    GetBackendName(), GetBackendEnumName(preferred));
    } else {
        std::printf("[meowyrender] window ready | backend: %s (selection: %s)\n",
                    GetBackendName(), source);
    }
}

void CloseWindow() {
    auto& s = State();
    // Tear down ImGui while the backend/context is still alive.
    if (s.imguiReady && s.backend) { s.backend->ImGuiShutdown(); s.imguiReady = false; }
    if(s.skyboxShader&&s.backend)s.backend->DestroyShaderProgram(s.skyboxShader);
    s.skyboxShader=s.activeShader=0;s.stereo=false;
    if (s.defaultFontLoaded) {
        // UnloadFont protects the shared default font; release its arrays here.
        delete[] s.defaultFont.recs;
        delete[] s.defaultFont.glyphs;
        s.defaultFont = {};
        s.defaultFontLoaded = false;
    }
    s.batch.verts.clear();
    if (s.backend) {
        s.backend->Shutdown();
        s.backend.reset();
    }
    if (s.window) detail::PlatformDestroyWindow(s.window);
    detail::ShutdownGamepadRumble();
    s.window = nullptr;
    s.windowReady = false;
}

bool IsWindowReady() { return State().windowReady; }
void RequestWindowClose() {State().closeRequested=true;}
bool IsWindowResized() { return State().windowResized; }
bool IsWindowFullscreen() { return State().fullscreen; }



// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------
void BeginDrawing() {
    auto& s = State();

    s.timing.currentTime = detail::ClockSeconds();
    s.timing.frameTime = s.timing.currentTime - s.timing.previousTime;
    s.timing.previousTime = s.timing.currentTime;

    // FPS counter (updates ~4x/sec).
    s.timing.fpsAccum += s.timing.frameTime;
    s.timing.fpsFrames++;
    if (s.timing.fpsAccum >= 0.25) {
        s.timing.fps = static_cast<int>(s.timing.fpsFrames / s.timing.fpsAccum);
        s.timing.fpsAccum = 0.0;
        s.timing.fpsFrames = 0;
    }

    if (!s.backend) return;
    s.backend->BeginFrame();
    s.targetWidth=s.screenWidth;s.targetHeight=s.screenHeight;
    s.backend->SetViewport({0,0,1,1});

    // Default 2D orthographic projection: origin top-left, y down.
    s.projection = MatrixOrtho(0, s.screenWidth, s.screenHeight, 0, -1.0, 1.0);
    s.modelview = MatrixIdentity();
    s.backend->SetProjection(s.projection);
    s.backend->SetModelview(s.modelview);

    // Start the ImGui frame so the app can call raw ImGui:: widgets between
    // BeginDrawing and EndDrawing (no explicit per-frame ImGui begin needed).
    if (s.imguiReady) s.backend->ImGuiNewFrame();
}

void EndDrawing() {
    auto& s = State();
    detail::FlushBatch();
    // Render ImGui into the backend's current frame target, after scene draws
    // and before the backend presents/swaps.
    if (s.imguiReady && s.backend) s.backend->ImGuiRender();
    // Honor a deferred screenshot while the frame is still active: the backend
    // can read its in-flight drawable/swapchain image (which EndFrame will
    // present and release), and the capture includes any ImGui overlay.
    if (!s.pendingScreenshotPath.empty() && s.backend) {
        Image image = s.backend->ReadScreen();
        const std::string path = std::move(s.pendingScreenshotPath);
        s.pendingScreenshotPath.clear();
        if (!image.data || !ExportImage(image, path))
            std::fprintf(stderr, "[meowyrender] screenshot capture failed: %s\n", path.c_str());
        if (image.data) UnloadImage(image);
    }
    if (s.backend) s.backend->EndFrame();
    s.windowResized = false;
    detail::PlatformPollInput();

    // Frame cap (busy-friendly sleep) when a target FPS is set.
#if !defined(MEOWY_PLATFORM_VISIONOS)
    if (s.timing.targetFrameTime > 0.0) {
        const double frameEnd = detail::ClockSeconds();
        const double elapsed = frameEnd - s.timing.currentTime;
        const double toWait = s.timing.targetFrameTime - elapsed;
        if (toWait > 0.0) {
            std::this_thread::sleep_for(
                std::chrono::duration<double>(toWait));
        }
    }
#endif
}

void ClearBackground(Color color) {
    detail::FlushBatch();
    if (State().backend) State().backend->Clear(color);
}

void BeginMode2D(Camera2D camera) {
    auto& s = State();
    detail::FlushBatch();
    // modelview = translate(-target) * scale(zoom) * rotate * translate(offset)
    Matrix origin = MatrixTranslate(-camera.target.x, -camera.target.y, 0.0f);
    Matrix rotation = MatrixIdentity();
    const float c = std::cos(camera.rotation * DEG2RAD);
    const float sn = std::sin(camera.rotation * DEG2RAD);
    rotation.m0 = c; rotation.m4 = -sn; rotation.m1 = sn; rotation.m5 = c;
    Matrix scale = MatrixScale(camera.zoom, camera.zoom, 1.0f);
    Matrix offset = MatrixTranslate(camera.offset.x, camera.offset.y, 0.0f);
    s.modelview = MatrixMultiply(MatrixMultiply(MatrixMultiply(origin, rotation), scale), offset);
    if (s.backend) s.backend->SetModelview(s.modelview);
}

void EndMode2D() {
    auto& s = State();
    detail::FlushBatch();
    s.modelview = MatrixIdentity();
    if (s.backend) s.backend->SetModelview(s.modelview);
}

void BeginMode3D(Camera3D camera) {
    auto& s = State();
    detail::FlushBatch();
    s.lighting.eye=camera.position;

    const double aspect = static_cast<double>(std::max(1,s.targetWidth)) / std::max(1,s.targetHeight);
    if (camera.projection == CameraProjection::Perspective) {
        s.projection = MatrixPerspective(camera.fovy * DEG2RAD, aspect, 0.01, 1000.0);
    } else {
        const double top = camera.fovy / 2.0;
        const double right = top * aspect;
        s.projection = MatrixOrtho(-right, right, -top, top, 0.01, 1000.0);
    }
    s.modelview = MatrixLookAt(camera.position, camera.target, camera.up);

    if (s.backend) {
        s.backend->SetDepthTest(true);
        s.backend->SetProjection(s.projection);
        s.backend->SetModelview(s.modelview);
    }
    // Defer BLEND meshes drawn in this pass for a back-to-front flush at EndMode3D.
    detail::SetTransparentDeferral(true);
}

void EndMode3D() {
    auto& s = State();
    detail::FlushBatch();
    // Composite the sorted transparent queue (depth-test, no depth-write) before
    // leaving 3D mode.
    detail::FlushTransparentQueue();
    detail::SetTransparentDeferral(false);
    // Restore the default 2D orthographic projection (origin top-left).
    s.projection = MatrixOrtho(0, s.targetWidth, s.targetHeight, 0, -1.0, 1.0);
    s.modelview = MatrixIdentity();
    if (s.backend) {
        s.backend->SetDepthTest(false);
        s.backend->SetProjection(s.projection);
        s.backend->SetModelview(s.modelview);
    }
}

bool IsShadowMappingSupported() {
    auto& s = State();
    return s.backend && s.backend->SupportsShadows();
}

void BeginShadowMode(Camera3D lightCamera, int resolution) {
    auto& s = State();
    if (!s.backend || !s.backend->SupportsShadows()) return;
    detail::FlushBatch();
    // Build the light's view*projection (orthographic for directional light).
    const double aspect = 1.0; // shadow map is square
    Matrix proj;
    if (lightCamera.projection == CameraProjection::Perspective)
        proj = MatrixPerspective(lightCamera.fovy * DEG2RAD, aspect, 0.01, 1000.0);
    else {
        const double top = lightCamera.fovy / 2.0;
        proj = MatrixOrtho(-top, top, -top, top, 0.01, 1000.0);
    }
    Matrix view = MatrixLookAt(lightCamera.position, lightCamera.target, lightCamera.up);
    s.backend->BeginShadowPass(MatrixMultiply(view, proj), resolution);
}

void EndShadowMode() {
    auto& s = State();
    if (!s.backend) return;
    detail::FlushBatch();
    s.backend->EndShadowPass();
    // Restore the default 2D projection so subsequent 2D draws are unaffected.
    s.projection = MatrixOrtho(0, s.targetWidth, s.targetHeight, 0, -1.0, 1.0);
    s.modelview = MatrixIdentity();
    s.backend->SetProjection(s.projection);
    s.backend->SetModelview(s.modelview);
    s.backend->SetDepthTest(false);
}

void ClearShadowMap() {
    auto& s = State();
    if (s.backend) { detail::FlushBatch(); s.backend->ClearShadowMap(); }
}

// ---------------------------------------------------------------------------
// Cascaded directional shadow maps
// ---------------------------------------------------------------------------
bool IsCascadedShadowSupported() {
    auto& s = State();
    return s.backend && s.backend->SupportsCascadedShadows();
}

void BeginShadowCascades(Camera3D viewCamera, Vector3 lightDirection, int cascadeCount, int resolution) {
    auto& s = State();
    if (!s.backend || !s.backend->SupportsCascadedShadows()) return;
    detail::FlushBatch();
    cascadeCount = std::clamp(cascadeCount, 1, detail::EngineState::kMaxShadowCascades);
    s.cascadeCount = cascadeCount;

    const double aspect = static_cast<double>(std::max(1, s.targetWidth)) / std::max(1, s.targetHeight);
    const float nearPlane = 0.1f, farPlane = 100.0f;
    // Practical split scheme (blend of logarithmic and uniform).
    float splits[detail::EngineState::kMaxShadowCascades + 1];
    splits[0] = nearPlane;
    for (int i = 1; i <= cascadeCount; ++i) {
        const float p = static_cast<float>(i) / cascadeCount;
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        splits[i] = 0.5f * logSplit + 0.5f * uniformSplit;
    }

    Vector3 lightDir = Vector3Normalize(lightDirection);
    Matrix viewMat = MatrixLookAt(viewCamera.position, viewCamera.target, viewCamera.up);
    s.cascadeViewMatrix = viewMat;

    for (int c = 0; c < cascadeCount; ++c) {
        // Build the sub-frustum [splits[c], splits[c+1]] corners in world space.
        Matrix proj = MatrixPerspective(viewCamera.fovy * DEG2RAD, aspect, splits[c], splits[c + 1]);
        Matrix invVP = MatrixInvert(MatrixMultiply(viewMat, proj));
        Vector3 corners[8]; int idx = 0;
        Vector3 center{0, 0, 0};
        for (int x = 0; x < 2; ++x) for (int y = 0; y < 2; ++y) for (int z = 0; z < 2; ++z) {
            // NDC corner -> world (perspective divide).
            Vector4 ndc{x ? 1.0f : -1.0f, y ? 1.0f : -1.0f, z ? 1.0f : 0.0f, 1.0f};
            Vector3 p{invVP.m0*ndc.x + invVP.m4*ndc.y + invVP.m8*ndc.z + invVP.m12,
                      invVP.m1*ndc.x + invVP.m5*ndc.y + invVP.m9*ndc.z + invVP.m13,
                      invVP.m2*ndc.x + invVP.m6*ndc.y + invVP.m10*ndc.z + invVP.m14};
            float w = invVP.m3*ndc.x + invVP.m7*ndc.y + invVP.m11*ndc.z + invVP.m15;
            if (w != 0.0f) { p.x /= w; p.y /= w; p.z /= w; }
            corners[idx++] = p;
            center = Vector3Add(center, p);
        }
        center = Vector3Scale(center, 1.0f / 8.0f);
        // Radius of the frustum slice's bounding sphere (stable cascade size).
        float radius = 0.0f;
        for (auto& corner : corners) radius = std::max(radius, Vector3Length(Vector3Subtract(corner, center)));
        radius = std::ceil(radius * 16.0f) / 16.0f;
        // Light camera looks at the slice center from along -lightDir.
        Vector3 eye = Vector3Subtract(center, Vector3Scale(lightDir, radius));
        Vector3 up = std::fabs(lightDir.y) > 0.99f ? Vector3{0, 0, 1} : Vector3{0, 1, 0};
        Matrix lightView = MatrixLookAt(eye, center, up);
        Matrix lightProj = MatrixOrtho(-radius, radius, -radius, radius, 0.0, radius * 2.0 + 0.1);
        s.cascadeMatrices[c] = MatrixMultiply(lightView, lightProj);
        s.cascadeSplits[c] = splits[c + 1];
    }
    s.backend->BeginShadowCascades(cascadeCount, resolution);
}

void SetShadowCascade(int index) {
    auto& s = State();
    if (!s.backend || index < 0 || index >= s.cascadeCount) return;
    detail::FlushBatch();
    s.backend->BeginShadowCascade(index, s.cascadeMatrices[index]);
}

int GetShadowCascadeCount() { return State().cascadeCount; }

void EndShadowCascades() {
    auto& s = State();
    if (!s.backend) return;
    detail::FlushBatch();
    s.backend->EndShadowCascade();
    s.backend->EndShadowCascades(s.cascadeSplits, s.cascadeCount, s.cascadeViewMatrix);
    // Restore default 2D projection state.
    s.projection = MatrixOrtho(0, s.targetWidth, s.targetHeight, 0, -1.0, 1.0);
    s.modelview = MatrixIdentity();
    s.backend->SetProjection(s.projection);
    s.backend->SetModelview(s.modelview);
    s.backend->SetDepthTest(false);
}

void BeginBlendMode(int mode) {
    detail::FlushBatch();
    if (State().backend) State().backend->SetBlendMode(mode);
}
void EndBlendMode() {
    detail::FlushBatch();
    if (State().backend) State().backend->SetBlendMode(0);
}
void BeginScissorMode(int x, int y, int w, int h) {
    detail::FlushBatch();
    if (State().backend) State().backend->SetScissor(true, x, y, w, h);
}
void EndScissorMode() {
    detail::FlushBatch();
    if (State().backend) State().backend->SetScissor(false, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
void WaitTime(double seconds) {
    if (seconds <= 0.0) return;
    // Coarse sleep for the bulk, then spin the last ~2ms for accuracy.
    const double target = detail::ClockSeconds() + seconds;
    const double coarse = seconds - 0.002;
    if (coarse > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(coarse));
    while (detail::ClockSeconds() < target) { /* busy-wait tail */ }
}

void PollInputEvents() { detail::PlatformPollInput(); }
void EnableEventWaiting()  { State().eventWaiting = true; }
void DisableEventWaiting() { State().eventWaiting = false; }

void SetTargetFPS(int fps) {
    State().timing.targetFrameTime = fps > 0 ? 1.0 / fps : 0.0;
}
float GetFrameTime() { return static_cast<float>(State().timing.frameTime); }
double GetTime() { return detail::ClockSeconds(); }
int GetFPS() { return State().timing.fps; }

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
void SetConfigFlags(unsigned int flags) { State().configFlags = flags; }

void TakeScreenshot(const std::string& fileName) {
    // Defer the actual capture to EndDrawing, while the frame is still active.
    // Backends whose only readable color target is the in-flight drawable /
    // swapchain image (Metal, Vulkan) return empty data once EndFrame presents,
    // and a post-present read would also miss the ImGui overlay. Recording the
    // path here lets EndDrawing capture at the correct point in the frame.
    auto& s = State();
    if (!s.window) {
        std::fprintf(stderr, "[meowyrender] screenshot requested with no window: %s\n", fileName.c_str());
        return;
    }
    s.pendingScreenshotPath = fileName;
}

int GetRandomValue(int min, int max) {
    if (min > max) std::swap(min, max);
    return min + std::rand() % (max - min + 1);
}

namespace detail {
extern int g_traceLogLevel;
extern TraceLogCallback g_traceLogCallback;
}
void TraceLog(int logLevel, const std::string& text) {
    if (logLevel < detail::g_traceLogLevel) return; // below threshold: suppressed
    if (detail::g_traceLogCallback) {
        // raylib's callback takes a printf format + va_list; we pass the already
        // formatted text as the format with no args.
        detail::g_traceLogCallback(logLevel, text.c_str(), nullptr);
        return;
    }
    std::printf("[meowyrender][%d] %s\n", logLevel, text.c_str());
}

} // namespace meowyrender
