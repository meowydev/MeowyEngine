// meowyrender - meowyrender.hpp
// Public API. A raylib-style interface backed by Metal / OpenGL / Vulkan.
//
// Usage:
//   #include <meowyrender/meowyrender.hpp>
//   using namespace meowyrender;
//   InitWindow(800, 450, "hello");
//   while (!WindowShouldClose()) {
//       BeginDrawing();
//       ClearBackground(RAYWHITE);
//       DrawText("hi", 10, 10, 20, DARKGRAY);
//       EndDrawing();
//   }
//   CloseWindow();
#pragma once

#include <string>
#include <functional>
#include <cstdarg>

#include "meowyrender/mr_math.hpp"
#include "meowyrender/mr_types.hpp"
#include "meowyrender/mr_input.hpp"

namespace meowyrender {

// ===========================================================================
// Backend info
// ===========================================================================
// Rendering backend selector. All backends supported on the current platform
// are compiled into the library; the active one is chosen at InitWindow time.
// `Automatic` (the default) picks the best available backend for the platform.
enum class Backend { Automatic, Metal, Vulkan, OpenGL };

// Legacy alias kept for source compatibility (no Automatic member).
enum class BackendType { OpenGL, Metal, Vulkan };

// --- Backend selection (call before InitWindow) ----------------------------
// Express a preference for which backend InitWindow should try first. Precedence
// at InitWindow time is: this explicit choice > the MEOWY_BACKEND env var >
// automatic per-platform selection. If the chosen backend is unavailable or its
// initialization fails, InitWindow automatically falls back to the next
// supported backend. Calling this AFTER a window already exists is rejected and
// logged (GPU resources belong to the active backend; live switching is not
// supported) -- it does not throw.
void SetPreferredBackend(Backend backend);
[[nodiscard]] Backend GetPreferredBackend();

// --- Backend queries --------------------------------------------------------
// The backend actually initialized by InitWindow (Automatic before InitWindow).
[[nodiscard]] Backend GetActiveBackend();
// Human-readable name of the active backend (e.g. "Metal (MTLDevice)"), or
// "none" before InitWindow.
[[nodiscard]] const char* GetActiveBackendName();
// Whether `backend` is compiled into this build AND usable on this system right
// now (e.g. a Metal device exists / Vulkan loader present). Backend::Automatic
// returns true when any backend is available. Cheap; may probe the platform.
[[nodiscard]] bool IsBackendAvailable(Backend backend);
// Whether `backend` was compiled into this library at all (ignores runtime
// availability). Useful for reporting "compiled in" vs "currently available".
[[nodiscard]] bool IsBackendCompiled(Backend backend);
// Parse a backend name ("metal"/"vulkan"/"opengl"/"auto", case-insensitive).
// Returns Backend::Automatic for "auto"/empty; returns false for unrecognized
// strings so callers (e.g. a sample parsing --backend) can report an error.
[[nodiscard]] bool ParseBackend(const std::string& name, Backend& out);
// The short canonical name of a Backend enum value ("Metal"/"Vulkan"/"OpenGL"/
// "Automatic"), independent of whether a window is open.
[[nodiscard]] const char* GetBackendEnumName(Backend backend);

// Which backend is active at runtime (legacy; prefer GetActiveBackend()).
[[nodiscard]] BackendType GetBackendType();
[[nodiscard]] const char* GetBackendName();

// ===========================================================================
// Dear ImGui integration (optional)
// ===========================================================================
// Available when the library is built with MEOWY_WITH_IMGUI=ON (which fetches
// Dear ImGui and links its GLFW + active-backend renderer backends). When
// enabled, call InitImGui() ONCE after InitWindow(); then between BeginDrawing()
// and EndDrawing() call raw Dear ImGui functions directly (ImGui::Begin(...),
// ImGui::Text(...), ImGui::End(), ImGui::ShowDemoWindow(), ...). BeginDrawing
// starts the ImGui frame and EndDrawing renders it into the active backend's
// frame automatically -- no explicit per-frame ImGui begin/end call is needed.
//
//   InitWindow(800, 600, "app");
//   InitImGui();
//   while (!WindowShouldClose()) {
//       BeginDrawing();
//       ClearBackground(BLACK);
//       ImGui::Begin("Debug"); ImGui::Text("fps %d", GetFPS()); ImGui::End();
//       EndDrawing();
//   }
//   ShutdownImGui();
//
// This header intentionally does NOT include <imgui.h> or expose any ImGui /
// graphics-API types; include <imgui.h> yourself to call the widgets.

// Whether this build was compiled with ImGui support AND the active backend
// supports it. Safe to call before/without a window (returns false).
[[nodiscard]] bool IsImGuiAvailable();
// Set up Dear ImGui for the active backend. Must be called after InitWindow().
// Returns false (and does nothing) if the library was built without ImGui or
// the active backend does not support it (e.g. Vulkan for now). Idempotent.
bool InitImGui();
// Tear down Dear ImGui. Safe to call if InitImGui() was never called/failed;
// called automatically by CloseWindow() as well.
void ShutdownImGui();
[[nodiscard]] TextureCubemap LoadTextureCubemap(Image image,CubemapLayout layout=CubemapLayout::AutoDetect);
[[nodiscard]] Image LoadImageFromCubemapFace(TextureCubemap cubemap,int face);
// Draw inside BeginMode3D. Translation is ignored; ordinary scene depth is preserved.
void DrawSkybox(TextureCubemap cubemap,Color tint=WHITE);

// ===========================================================================
// Window & context (core)
// ===========================================================================
// Returns with a ready window, or throws std::invalid_argument/std::runtime_error.
// No IsWindowReady() guard is needed in the ordinary frame loop.
void InitWindow(int width, int height, const std::string& title);
void CloseWindow();
void RequestWindowClose();
// Portable application host. The frame callback runs between Begin/EndDrawing;
// initialization and cleanup run with a live graphics context. Required on visionOS.
void RunApplication(int width,int height,const std::string& title,std::function<void()> frame,
                    std::function<void()> initialize={},std::function<void()> cleanup={});
[[nodiscard]] bool WindowShouldClose();
[[nodiscard]] bool IsWindowReady();
[[nodiscard]] bool IsWindowResized();
[[nodiscard]] bool IsWindowFullscreen();
void ToggleFullscreen();
void SetWindowTitle(const std::string& title);
void SetWindowSize(int width, int height);
void SetWindowPosition(int x, int y);
[[nodiscard]] int GetScreenWidth();
[[nodiscard]] int GetScreenHeight();
// Framebuffer (pixel) dimensions; differ from screen dims under HighDPI.
[[nodiscard]] int GetRenderWidth();
[[nodiscard]] int GetRenderHeight();
[[nodiscard]] Vector2 GetWindowPosition();
[[nodiscard]] Vector2 GetWindowScaleDPI();
[[nodiscard]] void* GetWindowHandle();

// --- window state (raylib FLAG_* via SetConfigFlags values) ---
[[nodiscard]] bool IsWindowState(unsigned int flag);
void SetWindowState(unsigned int flags);
void ClearWindowState(unsigned int flags);
[[nodiscard]] bool IsWindowHidden();
[[nodiscard]] bool IsWindowMinimized();
[[nodiscard]] bool IsWindowMaximized();
[[nodiscard]] bool IsWindowFocused();
void MinimizeWindow();
void MaximizeWindow();
void RestoreWindow();
void SetWindowMinSize(int width, int height);
void SetWindowMaxSize(int width, int height);
void SetWindowOpacity(float opacity);
void SetWindowFocused();
void SetWindowIcon(Image image);
void SetWindowIcons(Image* images, int count);

// --- cursor ---
void ShowCursor();
void HideCursor();
[[nodiscard]] bool IsCursorHidden();
void EnableCursor();
void DisableCursor();
[[nodiscard]] bool IsCursorOnScreen();

// --- monitor physical size ---
[[nodiscard]] int GetMonitorPhysicalWidth(int monitor);
[[nodiscard]] int GetMonitorPhysicalHeight(int monitor);

// ===========================================================================
// Drawing / frame lifecycle
// ===========================================================================
void BeginDrawing();
void EndDrawing();
void ClearBackground(Color color);

void BeginMode2D(Camera2D camera);
void EndMode2D();
[[nodiscard]] VrStereoConfig LoadVrStereoConfig(VrDeviceInfo device = {});
void UnloadVrStereoConfig(VrStereoConfig config);
void BeginVrStereoMode(VrStereoConfig config);
void EndVrStereoMode();
void BeginBlendMode(int mode);
enum class BlendMode : int { Alpha=0, Additive=1, Multiplied=2 };
inline void BeginBlendMode(BlendMode mode) { BeginBlendMode(static_cast<int>(mode)); }
void EndBlendMode();
void BeginScissorMode(int x, int y, int width, int height);
void EndScissorMode();

// ===========================================================================
// Timing
// ===========================================================================
void SetTargetFPS(int fps);
[[nodiscard]] float GetFrameTime();
[[nodiscard]] double GetTime();
[[nodiscard]] int GetFPS();
// Halt the calling thread for `seconds` (busy-wait tail for precision).
void WaitTime(double seconds);
// Poll input events without presenting a frame (advanced frame control).
void PollInputEvents();
// Wait/enable-disable OS event waiting to reduce CPU when idle.
void EnableEventWaiting();
void DisableEventWaiting();

// ===========================================================================
// Input: keyboard
// ===========================================================================
[[nodiscard]] bool IsKeyPressed(KeyboardKey key);
[[nodiscard]] bool IsKeyDown(KeyboardKey key);
[[nodiscard]] bool IsKeyReleased(KeyboardKey key);
[[nodiscard]] bool IsKeyUp(KeyboardKey key);
[[nodiscard]] int GetKeyPressed();
[[nodiscard]] int GetCharPressed();

// ===========================================================================
// Input: mouse
// ===========================================================================
[[nodiscard]] bool IsMouseButtonPressed(MouseButton button);
[[nodiscard]] bool IsMouseButtonDown(MouseButton button);
[[nodiscard]] bool IsMouseButtonReleased(MouseButton button);
[[nodiscard]] bool IsMouseButtonUp(MouseButton button);
[[nodiscard]] int GetMouseX();
[[nodiscard]] int GetMouseY();
[[nodiscard]] Vector2 GetMousePosition();
[[nodiscard]] Vector2 GetMouseDelta();
[[nodiscard]] float GetMouseWheelMove();
[[nodiscard]] Vector2 GetMouseWheelMoveV();
void SetMousePosition(int x, int y);
void SetMouseOffset(int offsetX, int offsetY);
void SetMouseScale(float scaleX, float scaleY);
void SetMouseCursor(MouseCursor cursor);

// --- keyboard extras ---
[[nodiscard]] bool IsKeyPressedRepeat(KeyboardKey key);
[[nodiscard]] const char* GetKeyName(KeyboardKey key);
void SetExitKey(KeyboardKey key);

// ===========================================================================
// Input: touch (GetTouchPosition / GetTouchPointCount declared with gestures)
// ===========================================================================
[[nodiscard]] int GetTouchX();
[[nodiscard]] int GetTouchY();
[[nodiscard]] int GetTouchPointId(int index);

// ===========================================================================
// Input: gamepad
// ===========================================================================
[[nodiscard]] bool IsGamepadAvailable(int gamepad);
[[nodiscard]] bool IsGamepadButtonDown(int gamepad, GamepadButton button);
[[nodiscard]] bool IsGamepadButtonPressed(int gamepad, GamepadButton button);
[[nodiscard]] bool IsGamepadButtonReleased(int gamepad, GamepadButton button);
[[nodiscard]] bool IsGamepadButtonUp(int gamepad, GamepadButton button);
[[nodiscard]] int GetGamepadButtonPressed();
[[nodiscard]] int GetGamepadAxisCount(int gamepad);
[[nodiscard]] float GetGamepadAxisMovement(int gamepad, GamepadAxis axis);
[[nodiscard]] const char* GetGamepadName(int gamepad);
int SetGamepadMappings(const std::string& mappings);
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration);

// ===========================================================================
// Shapes (2D)
// ===========================================================================
void DrawPixel(int x, int y, Color color);
void DrawPixelV(Vector2 position, Color color);
void DrawLine(int startX, int startY, int endX, int endY, Color color);
void DrawLineV(Vector2 start, Vector2 end, Color color);
void DrawLineEx(Vector2 start, Vector2 end, float thick, Color color);

void DrawCircle(int centerX, int centerY, float radius, Color color);
void DrawCircleV(Vector2 center, float radius, Color color);
void DrawCircleLines(int centerX, int centerY, float radius, Color color);
void DrawCircleSector(Vector2 center, float radius, float startAngle,
                      float endAngle, int segments, Color color);
void DrawEllipse(int centerX, int centerY, float radiusH, float radiusV, Color color);

void DrawRectangle(int x, int y, int width, int height, Color color);
void DrawRectangleV(Vector2 position, Vector2 size, Color color);
void DrawRectangleRec(Rectangle rec, Color color);
void DrawRectanglePro(Rectangle rec, Vector2 origin, float rotation, Color color);
void DrawRectangleLines(int x, int y, int width, int height, Color color);
void DrawRectangleLinesEx(Rectangle rec, float lineThick, Color color);
void DrawRectangleGradientV(int x, int y, int width, int height, Color top, Color bottom);
void DrawRectangleGradientH(int x, int y, int width, int height, Color left, Color right);
void DrawRectangleRounded(Rectangle rec, float roundness, int segments, Color color);

void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color);
void DrawTriangleLines(Vector2 v1, Vector2 v2, Vector2 v3, Color color);
void DrawTriangleFan(const Vector2* points, int pointCount, Color color);
void DrawTriangleStrip(const Vector2* points, int pointCount, Color color);
void DrawPoly(Vector2 center, int sides, float radius, float rotation, Color color);
void DrawPolyLines(Vector2 center, int sides, float radius, float rotation, Color color);
void DrawPolyLinesEx(Vector2 center, int sides, float radius, float rotation, float lineThick, Color color);

// --- additional line / circle / ellipse / ring / rounded variants ---
void DrawLineStrip(const Vector2* points, int pointCount, Color color);
void DrawLineBezier(Vector2 startPos, Vector2 endPos, float thick, Color color);
void DrawLineDashed(Vector2 startPos, Vector2 endPos, int dashSize, int spaceSize, Color color);
void DrawCircleGradient(Vector2 center, float radius, Color inner, Color outer);
void DrawCircleLinesV(Vector2 center, float radius, Color color);
void DrawCircleSectorLines(Vector2 center, float radius, float startAngle, float endAngle, int segments, Color color);
void DrawEllipseV(Vector2 center, float radiusH, float radiusV, Color color);
void DrawEllipseLines(int centerX, int centerY, float radiusH, float radiusV, Color color);
void DrawEllipseLinesV(Vector2 center, float radiusH, float radiusV, Color color);
void DrawRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color);
void DrawRingLines(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color);
void DrawRectangleGradientEx(Rectangle rec, Color topLeft, Color bottomLeft, Color topRight, Color bottomRight);
void DrawRectangleRoundedLines(Rectangle rec, float roundness, int segments, Color color);
void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments, float lineThick, Color color);

// --- splines (drawers) ---
void DrawSplineLinear(const Vector2* points, int pointCount, float thick, Color color);
void DrawSplineBasis(const Vector2* points, int pointCount, float thick, Color color);
void DrawSplineCatmullRom(const Vector2* points, int pointCount, float thick, Color color);
void DrawSplineBezierQuadratic(const Vector2* points, int pointCount, float thick, Color color);
void DrawSplineBezierCubic(const Vector2* points, int pointCount, float thick, Color color);
void DrawSplineSegmentLinear(Vector2 p1, Vector2 p2, float thick, Color color);
void DrawSplineSegmentBasis(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float thick, Color color);
void DrawSplineSegmentCatmullRom(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float thick, Color color);
void DrawSplineSegmentBezierQuadratic(Vector2 p1, Vector2 c2, Vector2 p3, float thick, Color color);
void DrawSplineSegmentBezierCubic(Vector2 p1, Vector2 c2, Vector2 c3, Vector2 p4, float thick, Color color);

// --- spline point evaluators (parameter t in [0,1]) ---
[[nodiscard]] Vector2 GetSplinePointLinear(Vector2 startPos, Vector2 endPos, float t);
[[nodiscard]] Vector2 GetSplinePointBasis(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float t);
[[nodiscard]] Vector2 GetSplinePointCatmullRom(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float t);
[[nodiscard]] Vector2 GetSplinePointBezierQuad(Vector2 p1, Vector2 c2, Vector2 p3, float t);
[[nodiscard]] Vector2 GetSplinePointBezierCubic(Vector2 p1, Vector2 c2, Vector2 c3, Vector2 p4, float t);

// Collision helpers
[[nodiscard]] bool CheckCollisionRecs(Rectangle a, Rectangle b);
[[nodiscard]] bool CheckCollisionCircles(Vector2 c1, float r1, Vector2 c2, float r2);
[[nodiscard]] bool CheckCollisionCircleRec(Vector2 center, float radius, Rectangle rec);
[[nodiscard]] bool CheckCollisionCircleLine(Vector2 center, float radius, Vector2 p1, Vector2 p2);
[[nodiscard]] bool CheckCollisionPointRec(Vector2 point, Rectangle rec);
[[nodiscard]] bool CheckCollisionPointCircle(Vector2 point, Vector2 center, float radius);
[[nodiscard]] bool CheckCollisionPointTriangle(Vector2 point, Vector2 p1, Vector2 p2, Vector2 p3);
[[nodiscard]] bool CheckCollisionPointLine(Vector2 point, Vector2 p1, Vector2 p2, int threshold);
[[nodiscard]] bool CheckCollisionPointPoly(Vector2 point, const Vector2* points, int pointCount);
[[nodiscard]] bool CheckCollisionLines(Vector2 startPos1, Vector2 endPos1, Vector2 startPos2, Vector2 endPos2, Vector2* collisionPoint);
[[nodiscard]] Rectangle GetCollisionRec(Rectangle rec1, Rectangle rec2);

// Texture used as the source for solid-shape drawing (raylib batches shapes
// against a 1x1 white texel from the font atlas; MeowyRender uses a white texture).
void SetShapesTexture(Texture2D texture, Rectangle source);
[[nodiscard]] Texture2D GetShapesTexture();
[[nodiscard]] Rectangle GetShapesTextureRectangle();

// ===========================================================================
// 3D mode + camera
// ===========================================================================
void BeginMode3D(Camera3D camera);
void EndMode3D();

// Camera movement modes for UpdateCamera.
enum class CameraMode : int { Custom = 0, Free, Orbital, FirstPerson, ThirdPerson };
void UpdateCamera(Camera3D* camera, CameraMode mode);
[[nodiscard]] Matrix GetCameraMatrix(Camera3D camera);
[[nodiscard]] Ray GetMouseRay(Vector2 mousePosition, Camera3D camera);
[[nodiscard]] Vector2 GetWorldToScreen(Vector3 position, Camera3D camera);

// ===========================================================================
// 3D primitives
// ===========================================================================
void DrawLine3D(Vector3 start, Vector3 end, Color color);
void DrawPoint3D(Vector3 position, Color color);
void DrawTriangle3D(Vector3 v1, Vector3 v2, Vector3 v3, Color color);
void DrawCube(Vector3 position, float width, float height, float length, Color color);
void DrawCubeV(Vector3 position, Vector3 size, Color color);
void DrawCubeWires(Vector3 position, float width, float height, float length, Color color);
void DrawSphere(Vector3 center, float radius, Color color);
void DrawSphereEx(Vector3 center, float radius, int rings, int slices, Color color);
void DrawSphereWires(Vector3 center, float radius, int rings, int slices, Color color);
void DrawCylinder(Vector3 position, float radiusTop, float radiusBottom,
                  float height, int slices, Color color);
void DrawPlane(Vector3 center, Vector2 size, Color color);
void DrawGrid(int slices, float spacing);
void DrawRay(Ray ray, Color color);
void DrawCircle3D(Vector3 center, float radius, Vector3 rotationAxis, float rotationAngle, Color color);
void DrawCubeWiresV(Vector3 position, Vector3 size, Color color);
void DrawCylinderEx(Vector3 startPos, Vector3 endPos, float startRadius, float endRadius, int sides, Color color);
void DrawCylinderWires(Vector3 position, float radiusTop, float radiusBottom, float height, int slices, Color color);
void DrawCylinderWiresEx(Vector3 startPos, Vector3 endPos, float startRadius, float endRadius, int sides, Color color);
void DrawCapsule(Vector3 startPos, Vector3 endPos, float radius, int slices, int rings, Color color);
void DrawCapsuleWires(Vector3 startPos, Vector3 endPos, float radius, int slices, int rings, Color color);
void DrawTriangleStrip3D(const Vector3* points, int pointCount, Color color);

// 3D collision helpers
[[nodiscard]] bool CheckCollisionSpheres(Vector3 c1, float r1, Vector3 c2, float r2);
[[nodiscard]] bool CheckCollisionBoxes(BoundingBox a, BoundingBox b);
[[nodiscard]] bool CheckCollisionBoxSphere(BoundingBox box, Vector3 center, float radius);
[[nodiscard]] RayCollision GetRayCollisionSphere(Ray ray, Vector3 center, float radius);
[[nodiscard]] RayCollision GetRayCollisionBox(Ray ray, BoundingBox box);
[[nodiscard]] RayCollision GetRayCollisionMesh(Ray ray, Mesh mesh, Matrix transform);
[[nodiscard]] RayCollision GetRayCollisionTriangle(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3);
[[nodiscard]] RayCollision GetRayCollisionQuad(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 p4);

// ===========================================================================
// Meshes, materials & models
// ===========================================================================
// --- mesh generators ---
[[nodiscard]] Mesh GenMeshCube(float width, float height, float length);
[[nodiscard]] Mesh GenMeshPlane(float width, float length, int resX, int resZ);
[[nodiscard]] Mesh GenMeshSphere(float radius, int rings, int slices);
[[nodiscard]] Mesh GenMeshCylinder(float radius, float height, int slices);
[[nodiscard]] Mesh GenMeshTorus(float radius, float size, int radSeg, int sides);
[[nodiscard]] Mesh GenMeshCone(float radius, float height, int slices);
[[nodiscard]] Mesh GenMeshHemiSphere(float radius, int rings, int slices);
[[nodiscard]] Mesh GenMeshKnot(float radius, float size, int radSeg, int sides);
[[nodiscard]] Mesh GenMeshPoly(int sides, float radius);
[[nodiscard]] Mesh GenMeshHeightmap(Image heightmap, Vector3 size);
[[nodiscard]] Mesh GenMeshCubicmap(Image cubicmap, Vector3 cubeSize);
void GenMeshTangents(Mesh* mesh);
// glTF morph targets: set the per-target blend weights (length = target count).
void SetMeshMorphWeights(Mesh* mesh, const float* weights, int count);
[[nodiscard]] int GetMeshMorphTargetCount(Mesh mesh);
void UploadMesh(Mesh* mesh, bool dynamic);
void UpdateMeshBuffer(Mesh mesh, int index, const void* data, int dataSize, int offset);
void UnloadMesh(Mesh mesh);
[[nodiscard]] BoundingBox GetMeshBoundingBox(Mesh mesh);
[[nodiscard]] bool ExportMesh(Mesh mesh, const std::string& fileName);
[[nodiscard]] bool ExportMeshAsCode(Mesh mesh, const std::string& fileName);

// --- materials ---
[[nodiscard]] Material LoadMaterialDefault();
[[nodiscard]] bool IsMaterialValid(Material material);
[[nodiscard]] Material* LoadMaterials(const std::string& fileName, int* materialCount);
void SetModelMeshMaterial(Model* model, int meshId, int materialId);
void SetAmbientLight(Color color, float intensity = 0.1f);
void SetDirectionalLight(Vector3 direction, Color color = WHITE, float intensity = 3.0f);
// Image-based lighting: use an environment cubemap for ambient diffuse and
// specular reflection on lit materials. Pass an invalid/zero cubemap or call
// ClearEnvironmentLight() to return to the flat SetAmbientLight term. By default
// this is a runtime cubemap-sampled approximation; call GenEnvironmentLightMaps
// first for the precomputed split-sum pipeline.
void SetEnvironmentLight(TextureCubemap cubemap, float intensity = 1.0f);
void ClearEnvironmentLight();
// Precompute split-sum IBL maps from `cubemap`: a cosine-convolved diffuse
// irradiance cubemap, a roughness-prefiltered specular cubemap, and a BRDF
// integration LUT. Bind them with SetEnvironmentLightPrecomputed(); the lit
// shader then uses irradiance(N) for diffuse and prefilter(R)*(F0*brdf.r+brdf.g)
// for specular (Karis split-sum), instead of the runtime approximation.
struct EnvironmentLight {
    TextureCubemap irradiance;   // diffuse irradiance
    TextureCubemap prefilter;    // roughness-prefiltered specular
    int prefilterMips = 1;
    Texture2D brdfLut;           // BRDF integration LUT (RG)
    TextureCubemap source;       // the original environment cubemap
};
[[nodiscard]] EnvironmentLight GenEnvironmentLightMaps(TextureCubemap cubemap, int irradianceSize = 32,
                                                       int prefilterSize = 64, int brdfSize = 128);
void SetEnvironmentLightPrecomputed(EnvironmentLight env, float intensity = 1.0f);
void UnloadEnvironmentLight(EnvironmentLight env);
// Directional shadow mapping. Render shadow-casting geometry between
// BeginShadowMode/EndShadowMode using an orthographic light camera; subsequent
// lit draws sample the resulting shadow map. IsShadowMappingSupported() reports
// backend support (currently OpenGL). resolution is the shadow map size.
[[nodiscard]] bool IsShadowMappingSupported();
void BeginShadowMode(Camera3D lightCamera, int resolution = 2048);
void EndShadowMode();
void ClearShadowMap();
// Cascaded directional shadow maps: partition the view camera's frustum into
// `cascadeCount` depth ranges (1..4), each with its own light-space shadow map,
// giving higher resolution near the camera. Between BeginShadowCascades and
// EndShadowCascades, draw shadow-casting geometry ONCE per cascade inside the
// GetShadowCascade(i) loop (the light view-projection is set per cascade).
// IsCascadedShadowSupported() reports backend support (currently OpenGL).
[[nodiscard]] bool IsCascadedShadowSupported();
void BeginShadowCascades(Camera3D viewCamera, Vector3 lightDirection,
                         int cascadeCount = 3, int resolution = 2048);
// Select cascade `index` for the following depth draws (call inside the loop).
void SetShadowCascade(int index);
[[nodiscard]] int GetShadowCascadeCount();
void EndShadowCascades();
void UnloadMaterial(Material material);
void SetMaterialTexture(Material* material, MaterialMapIndex mapType, Texture2D texture);

// --- models ---
// Load a model from a file. Supported: .obj (custom parser), .gltf/.glb (cgltf).
[[nodiscard]] Model LoadModel(const std::string& fileName);
[[nodiscard]] Model LoadModelFromMesh(Mesh mesh);
[[nodiscard]] bool IsModelValid(Model model);
void UnloadModel(Model model);
[[nodiscard]] BoundingBox GetModelBoundingBox(Model model);
void SetModelMaterialTexture(Model* model, int materialIndex,
                             MaterialMapIndex mapType, Texture2D texture);

// --- mesh/model drawing ---
// NOTE: inside BeginMode3D/EndMode3D, meshes whose material alphaMode is BLEND
// are DEFERRED and flushed (back-to-front sorted) at EndMode3D. The mesh's
// vertex data and the material must therefore stay alive until EndMode3D
// returns; do not UnloadMesh/UnloadMaterial a transparent mesh before then.
void DrawMesh(Mesh mesh, Material material, Matrix transform);
// Query after InitWindow. Compressed-format availability depends on the GPU.
[[nodiscard]] bool IsTextureFormatSupported(PixelFormat format);
// GPU-instanced with the default shader on Metal/OpenGL. Custom shaders and
// other backends can use the compatible per-instance rendering fallback.
void DrawMeshInstanced(Mesh mesh, Material material, const Matrix* transforms, int instances);
void DrawModel(Model model, Vector3 position, float scale, Color tint);
void DrawModelEx(Model model, Vector3 position, Vector3 rotationAxis,
                 float rotationAngle, Vector3 scale, Color tint);
void DrawModelWires(Model model, Vector3 position, float scale, Color tint);
void DrawModelWiresEx(Model model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale, Color tint);
void DrawBoundingBox(BoundingBox box, Color color);

// --- model animation (glTF / IQM) ---
[[nodiscard]] ModelAnimation* LoadModelAnimations(const std::string& fileName,
                                                  int* animCount);
void UpdateModelAnimation(Model model, ModelAnimation anim, int frame);
void UpdateModelAnimationEx(Model model, ModelAnimation anim, int frame, bool updateBones);
void UnloadModelAnimations(ModelAnimation* animations, int animCount);
[[nodiscard]] bool IsModelAnimationValid(Model model, ModelAnimation anim);

// --- billboards ---
void DrawBillboard(Camera3D camera, Texture2D texture, Vector3 position,
                   float scale, Color tint);
void DrawBillboardRec(Camera3D camera, Texture2D texture, Rectangle source,
                      Vector3 position, Vector2 size, Color tint);
void DrawBillboardPro(Camera3D camera, Texture2D texture, Rectangle source, Vector3 position,
                      Vector3 up, Vector2 size, Vector2 origin, float rotation, Color tint);

// ===========================================================================
// Textures
// ===========================================================================
// --- Image loading (CPU) ---
[[nodiscard]] Image LoadImage(const std::string& fileName);
[[nodiscard]] Image LoadImageFromMemory(const std::string& fileType,
                                        const unsigned char* data, int dataSize);
[[nodiscard]] bool ExportImage(Image image, const std::string& fileName);
void UnloadImage(Image image);
[[nodiscard]] Image ImageCopy(Image image);

// --- Image generation ---
[[nodiscard]] Image GenImageColor(int width, int height, Color color);
[[nodiscard]] Image GenImageGradientV(int width, int height, Color top, Color bottom);
[[nodiscard]] Image GenImageGradientH(int width, int height, Color left, Color right);
[[nodiscard]] Image GenImageGradientRadial(int width, int height, float density,
                                           Color inner, Color outer);
[[nodiscard]] Image GenImageChecked(int width, int height, int checksX, int checksY,
                                    Color col1, Color col2);
[[nodiscard]] Image GenImageWhiteNoise(int width, int height, float factor);

// --- Image manipulation (operate in place unless noted) ---
void ImageFormat(Image* image, PixelFormat newFormat);
void ImageResize(Image* image, int newWidth, int newHeight);
void ImageResizeNN(Image* image, int newWidth, int newHeight);
void ImageCrop(Image* image, Rectangle crop);
void ImageFlipVertical(Image* image);
void ImageFlipHorizontal(Image* image);
void ImageColorTint(Image* image, Color color);
void ImageColorInvert(Image* image);
void ImageColorGrayscale(Image* image);
void ImageColorBrightness(Image* image, int brightness);
[[nodiscard]] Color GetImageColor(Image image, int x, int y);
void ImageDrawPixel(Image* image, int x, int y, Color color);
void ImageDrawRectangle(Image* image, int x, int y, int w, int h, Color color);

// --- additional image generation ---
[[nodiscard]] Image GenImageGradientLinear(int width, int height, int direction, Color start, Color end);
[[nodiscard]] Image GenImageGradientSquare(int width, int height, float density, Color inner, Color outer);
[[nodiscard]] Image GenImagePerlinNoise(int width, int height, int offsetX, int offsetY, float scale);
[[nodiscard]] Image GenImageCellular(int width, int height, int tileSize);
[[nodiscard]] Image GenImageText(int width, int height, const std::string& text);

// --- image validity / raw load / export ---
[[nodiscard]] bool IsImageValid(Image image);
[[nodiscard]] bool IsTextureValid(Texture2D texture);
[[nodiscard]] bool IsRenderTextureValid(RenderTexture2D target);
[[nodiscard]] Image LoadImageRaw(const std::string& fileName, int width, int height, int format, int headerSize);
[[nodiscard]] Image LoadImageAnim(const std::string& fileName, int* frames);
[[nodiscard]] Image LoadImageAnimFromMemory(const std::string& fileType, const unsigned char* fileData, int dataSize, int* frames);
[[nodiscard]] Image LoadImageFromScreen();
[[nodiscard]] bool ExportImageAsCode(Image image, const std::string& fileName);
[[nodiscard]] unsigned char* ExportImageToMemory(Image image, const std::string& fileType, int* fileSize);

// --- image color arrays / palettes ---
[[nodiscard]] Color* LoadImageColors(Image image);
[[nodiscard]] Color* LoadImagePalette(Image image, int maxPaletteSize, int* colorCount);
void UnloadImageColors(Color* colors);
void UnloadImagePalette(Color* colors);
[[nodiscard]] Rectangle GetImageAlphaBorder(Image image, float threshold);

// --- pixel data ---
[[nodiscard]] Color GetPixelColor(void* srcPtr, int format);
void SetPixelColor(void* dstPtr, Color color, int format);
[[nodiscard]] int GetPixelDataSize(int width, int height, int format);

// --- more image manipulation ---
[[nodiscard]] Image ImageFromImage(Image image, Rectangle rec);
[[nodiscard]] Image ImageFromChannel(Image image, int selectedChannel);
[[nodiscard]] Image ImageText(const std::string& text, int fontSize, Color color);
[[nodiscard]] Image ImageTextEx(Font font, const std::string& text, float fontSize, float spacing, Color tint);
void ImageToPOT(Image* image, Color fill);
void ImageAlphaClear(Image* image, Color color, float threshold);
void ImageAlphaCrop(Image* image, float threshold);
void ImageAlphaMask(Image* image, Image alphaMask);
void ImageAlphaPremultiply(Image* image);
void ImageBlurGaussian(Image* image, int blurSize);
void ImageKernelConvolution(Image* image, const float* kernel, int kernelSize);
void ImageResizeCanvas(Image* image, int newWidth, int newHeight, int offsetX, int offsetY, Color fill);
void ImageMipmaps(Image* image);
void ImageDither(Image* image, int rBpp, int gBpp, int bBpp, int aBpp);
void ImageRotate(Image* image, int degrees);
void ImageRotateCW(Image* image);
void ImageRotateCCW(Image* image);
void ImageColorContrast(Image* image, float contrast);
void ImageColorReplace(Image* image, Color color, Color replace);
void ImageClearBackground(Image* dst, Color color);

// --- image drawing ---
void ImageDrawPixelV(Image* dst, Vector2 position, Color color);
void ImageDrawLine(Image* dst, int startX, int startY, int endX, int endY, Color color);
void ImageDrawLineV(Image* dst, Vector2 start, Vector2 end, Color color);
void ImageDrawLineEx(Image* dst, Vector2 start, Vector2 end, int thick, Color color);
void ImageDrawCircle(Image* dst, int centerX, int centerY, int radius, Color color);
void ImageDrawCircleV(Image* dst, Vector2 center, int radius, Color color);
void ImageDrawCircleLines(Image* dst, int centerX, int centerY, int radius, Color color);
void ImageDrawCircleLinesV(Image* dst, Vector2 center, int radius, Color color);
void ImageDrawRectangleV(Image* dst, Vector2 position, Vector2 size, Color color);
void ImageDrawRectangleRec(Image* dst, Rectangle rec, Color color);
void ImageDrawRectangleLines(Image* dst, Rectangle rec, int thick, Color color);
void ImageDrawTriangle(Image* dst, Vector2 v1, Vector2 v2, Vector2 v3, Color color);
void ImageDrawTriangleEx(Image* dst, Vector2 v1, Vector2 v2, Vector2 v3, Color c1, Color c2, Color c3);
void ImageDrawTriangleLines(Image* dst, Vector2 v1, Vector2 v2, Vector2 v3, Color color);
void ImageDrawTriangleFan(Image* dst, const Vector2* points, int pointCount, Color color);
void ImageDrawTriangleStrip(Image* dst, const Vector2* points, int pointCount, Color color);
void ImageDraw(Image* dst, Image src, Rectangle srcRec, Rectangle dstRec, Color tint);
void ImageDrawText(Image* dst, const std::string& text, int posX, int posY, int fontSize, Color color);
void ImageDrawTextEx(Image* dst, Font font, const std::string& text, Vector2 position, float fontSize, float spacing, Color tint);

// --- texture update / npatch ---
void UpdateTextureRec(Texture2D texture, Rectangle rec, const void* pixels);
void DrawTextureNPatch(Texture2D texture, NPatchInfo nPatchInfo, Rectangle dest, Vector2 origin, float rotation, Color tint);

[[nodiscard]] Texture2D LoadTexture(const std::string& fileName);
[[nodiscard]] Texture2D LoadTextureFromImage(Image image);
void UnloadTexture(Texture2D texture);
void UpdateTexture(Texture2D texture, const void* pixels);
void GenTextureMipmaps(Texture2D* texture);
void SetTextureFilter(Texture2D texture, TextureFilter filter);
void SetTextureWrap(Texture2D texture, TextureWrap wrap);
[[nodiscard]] Image LoadImageFromTexture(Texture2D texture);

// --- render textures (offscreen framebuffers) ---
[[nodiscard]] RenderTexture2D LoadRenderTexture(int width, int height);
void UnloadRenderTexture(RenderTexture2D target);
void BeginTextureMode(RenderTexture2D target);
void EndTextureMode();

void DrawTexture(Texture2D texture, int x, int y, Color tint);
void DrawTextureV(Texture2D texture, Vector2 position, Color tint);
void DrawTextureEx(Texture2D texture, Vector2 position, float rotation,
                   float scale, Color tint);
void DrawTextureRec(Texture2D texture, Rectangle source, Vector2 position, Color tint);
void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest,
                    Vector2 origin, float rotation, Color tint);

// ===========================================================================
// Text
// ===========================================================================
[[nodiscard]] Font GetFontDefault();
[[nodiscard]] Font LoadFont(const std::string& fileName);
// Load a TrueType/OTF font at a specific pixel size. If codepoints is null,
// the default ASCII range (32..126) plus Latin-1 extras is baked.
[[nodiscard]] Font LoadFontEx(const std::string& fileName, int fontSize,
                              const int* codepoints, int codepointCount);
[[nodiscard]] Font LoadFontFromMemory(const std::string& fileType,
                                      const unsigned char* fileData, int dataSize,
                                      int fontSize, const int* codepoints,
                                      int codepointCount);
[[nodiscard]] bool IsFontValid(Font font);
void UnloadFont(Font font);

void DrawFPS(int x, int y);
void DrawText(const std::string& text, int x, int y, int fontSize, Color color);
void DrawTextEx(Font font, const std::string& text, Vector2 position,
                float fontSize, float spacing, Color tint);
// Draw text rotated by `rotation` degrees about `origin` (relative to the run's
// top-left), anchored at `position`. Newlines start a new line; there is no
// automatic word wrapping.
void DrawTextPro(Font font, const std::string& text, Vector2 position,
                 Vector2 origin, float rotation, float fontSize,
                 float spacing, Color tint);
void DrawTextCodepoint(Font font, int codepoint, Vector2 position,
                       float fontSize, Color tint);
[[nodiscard]] int MeasureText(const std::string& text, int fontSize);
[[nodiscard]] Vector2 MeasureTextEx(Font font, const std::string& text,
                                    float fontSize, float spacing);
[[nodiscard]] int GetGlyphIndex(Font font, int codepoint);
[[nodiscard]] GlyphInfo GetGlyphInfo(Font font, int codepoint);
[[nodiscard]] Rectangle GetGlyphAtlasRec(Font font, int codepoint);
void DrawTextCodepoints(Font font, const int* codepoints, int count, Vector2 position,
                        float fontSize, float spacing, Color tint);
[[nodiscard]] Vector2 MeasureTextCodepoints(Font font, const int* codepoints, int count,
                                            float fontSize, float spacing);
[[nodiscard]] bool IsFontValid(Font font);

// --- font data / atlas generation ---
[[nodiscard]] GlyphInfo* LoadFontData(const unsigned char* fileData, int dataSize, int fontSize,
                                      const int* codepoints, int codepointCount, int type);
void UnloadFontData(GlyphInfo* glyphs, int glyphCount);
[[nodiscard]] Image GenImageFontAtlas(const GlyphInfo* glyphs, Rectangle** glyphRecs,
                                      int glyphCount, int fontSize, int padding, int packMethod);
[[nodiscard]] Font LoadFontFromImage(Image image, Color key, int firstChar);
[[nodiscard]] bool ExportFontAsCode(Font font, const std::string& fileName);

// --- Unicode / codepoint utilities ---
// Decode the next UTF-8 codepoint from text; advances *bytesProcessed.
[[nodiscard]] int GetCodepointNext(const char* text, int* codepointSize);
// Encode a codepoint to UTF-8 into a static buffer; sets *utf8Size.
[[nodiscard]] const char* CodepointToUTF8(int codepoint, int* utf8Size);
[[nodiscard]] int GetCodepointCount(const std::string& text);

// ===========================================================================
// Shaders
// ===========================================================================
enum class ShaderUniformType : int { Float = 0, Vec2, Vec3, Vec4, Int, Mat4 };

[[nodiscard]] Shader LoadShader(const std::string& vsFileName, const std::string& fsFileName);
[[nodiscard]] Shader LoadShaderFromMemory(const std::string& vsCode, const std::string& fsCode);
[[nodiscard]] bool IsShaderValid(Shader shader);
[[nodiscard]] int GetShaderLocation(Shader shader, const std::string& uniformName);
void SetShaderValue(Shader shader, int locIndex, const void* value,
                    ShaderUniformType uniformType);
void SetShaderValueV(Shader shader, int locIndex, const void* value,
                     ShaderUniformType uniformType, int count);
// Convenient named overloads; the location-based API remains available for
// callers that cache locations or upload arrays.
inline void SetShaderValue(Shader shader, const std::string& name, float value) {
    SetShaderValue(shader, GetShaderLocation(shader, name), &value, ShaderUniformType::Float);
}
inline void SetShaderValue(Shader shader, const std::string& name, int value) {
    SetShaderValue(shader, GetShaderLocation(shader, name), &value, ShaderUniformType::Int);
}
inline void SetShaderValue(Shader shader, const std::string& name, Vector2 value) {
    SetShaderValue(shader, GetShaderLocation(shader, name), &value, ShaderUniformType::Vec2);
}
inline void SetShaderValue(Shader shader, const std::string& name, Vector3 value) {
    SetShaderValue(shader, GetShaderLocation(shader, name), &value, ShaderUniformType::Vec3);
}
inline void SetShaderValue(Shader shader, const std::string& name, Vector4 value) {
    SetShaderValue(shader, GetShaderLocation(shader, name), &value, ShaderUniformType::Vec4);
}
inline void SetShaderValueMatrix(Shader shader,int location,Matrix value) {
    SetShaderValue(shader,location,&value,ShaderUniformType::Mat4);
}
inline void SetShaderValue(Shader shader,const std::string& name,Matrix value) {
    SetShaderValueMatrix(shader,GetShaderLocation(shader,name),value);
}
void UnloadShader(Shader shader);
void BeginShaderMode(Shader shader);
void EndShaderMode();

// ===========================================================================
// Audio
// ===========================================================================
// --- device ---
void InitAudioDevice();
void CloseAudioDevice();
[[nodiscard]] bool IsAudioDeviceReady();
void SetMasterVolume(float volume);
[[nodiscard]] float GetMasterVolume();

// --- Wave (raw samples in memory) ---
[[nodiscard]] Wave LoadWave(const std::string& fileName);
[[nodiscard]] Wave LoadWaveFromMemory(const std::string& fileType,
                                      const unsigned char* data, int dataSize);
void UnloadWave(Wave wave);
[[nodiscard]] bool ExportWave(Wave wave, const std::string& fileName);

// --- Sound (short one-shot effects) ---
[[nodiscard]] Sound LoadSound(const std::string& fileName);
[[nodiscard]] Sound LoadSoundFromWave(Wave wave);
void UnloadSound(Sound sound);
void PlaySound(Sound sound);
void StopSound(Sound sound);
void PauseSound(Sound sound);
void ResumeSound(Sound sound);
[[nodiscard]] bool IsSoundPlaying(Sound sound);
void SetSoundVolume(Sound sound, float volume);
void SetSoundPitch(Sound sound, float pitch);
void SetSoundPan(Sound sound, float pan);

// --- Music (streamed) ---
[[nodiscard]] Music LoadMusicStream(const std::string& fileName);
void UnloadMusicStream(Music music);
void PlayMusicStream(Music music);
void StopMusicStream(Music music);
void PauseMusicStream(Music music);
void ResumeMusicStream(Music music);
void UpdateMusicStream(Music music);            // call each frame
[[nodiscard]] bool IsMusicStreamPlaying(Music music);
void SetMusicVolume(Music music, float volume);
void SetMusicPitch(Music music, float pitch);
[[nodiscard]] float GetMusicTimeLength(Music music);
[[nodiscard]] float GetMusicTimePlayed(Music music);
void SeekMusicStream(Music music, float position);
void SetMusicPan(Music music, float pan);

// --- validity checks ---
[[nodiscard]] bool IsWaveValid(Wave wave);
[[nodiscard]] bool IsSoundValid(Sound sound);
[[nodiscard]] bool IsMusicValid(Music music);
[[nodiscard]] bool IsAudioStreamValid(AudioStream stream);

// --- wave utilities ---
[[nodiscard]] Wave WaveCopy(Wave wave);
void WaveCrop(Wave* wave, int initFrame, int finalFrame);
void WaveFormat(Wave* wave, int sampleRate, int sampleSize, int channels);
[[nodiscard]] float* LoadWaveSamples(Wave wave);
void UnloadWaveSamples(float* samples);
[[nodiscard]] bool ExportWaveAsCode(Wave wave, const std::string& fileName);

// --- sound aliases + update ---
[[nodiscard]] Sound LoadSoundAlias(Sound source);
void UnloadSoundAlias(Sound alias);
void UpdateSound(Sound sound, const void* data, int sampleCount);
[[nodiscard]] Music LoadMusicStreamFromMemory(const std::string& fileType, const unsigned char* data, int dataSize);

// --- audio streams (procedural / custom audio) ---
using AudioCallback = void (*)(void* bufferData, unsigned int frames);
[[nodiscard]] AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels);
void UnloadAudioStream(AudioStream stream);
void UpdateAudioStream(AudioStream stream, const void* data, int frameCount);
[[nodiscard]] bool IsAudioStreamProcessed(AudioStream stream);
void PlayAudioStream(AudioStream stream);
void PauseAudioStream(AudioStream stream);
void ResumeAudioStream(AudioStream stream);
[[nodiscard]] bool IsAudioStreamPlaying(AudioStream stream);
void StopAudioStream(AudioStream stream);
void SetAudioStreamVolume(AudioStream stream, float volume);
void SetAudioStreamPitch(AudioStream stream, float pitch);
void SetAudioStreamPan(AudioStream stream, float pan);
void SetAudioStreamBufferSizeDefault(int size);
void SetAudioStreamCallback(AudioStream stream, AudioCallback callback);
void AttachAudioStreamProcessor(AudioStream stream, AudioCallback processor);
void DetachAudioStreamProcessor(AudioStream stream, AudioCallback processor);
void AttachAudioMixedProcessor(AudioCallback processor);
void DetachAudioMixedProcessor(AudioCallback processor);

// ===========================================================================
// Monitors
// ===========================================================================
[[nodiscard]] int GetMonitorCount();
[[nodiscard]] int GetCurrentMonitor();
[[nodiscard]] Vector2 GetMonitorPosition(int monitor);
[[nodiscard]] int GetMonitorWidth(int monitor);
[[nodiscard]] int GetMonitorHeight(int monitor);
[[nodiscard]] int GetMonitorRefreshRate(int monitor);
[[nodiscard]] const char* GetMonitorName(int monitor);
void SetWindowMonitor(int monitor);

// ===========================================================================
// Clipboard
// ===========================================================================
void SetClipboardText(const std::string& text);
[[nodiscard]] const char* GetClipboardText();

// ===========================================================================
// Gestures / touch
// ===========================================================================
enum class Gesture : int {
    None = 0, Tap = 1, DoubleTap = 2, Hold = 4, Drag = 8,
    SwipeRight = 16, SwipeLeft = 32, SwipeUp = 64, SwipeDown = 128,
    PinchIn = 256, PinchOut = 512,
};
void SetGesturesEnabled(unsigned int flags);
[[nodiscard]] bool IsGestureDetected(Gesture gesture);
[[nodiscard]] int GetGestureDetected();
[[nodiscard]] float GetGestureHoldDuration();
[[nodiscard]] Vector2 GetGestureDragVector();
[[nodiscard]] float GetGestureDragAngle();
[[nodiscard]] Vector2 GetGesturePinchVector();
[[nodiscard]] float GetGesturePinchAngle();
[[nodiscard]] int GetTouchPointCount();
[[nodiscard]] Vector2 GetTouchPosition(int index);

// ===========================================================================
// File system utilities
// ===========================================================================
[[nodiscard]] bool FileExists(const std::string& fileName);
[[nodiscard]] bool DirectoryExists(const std::string& dirPath);
[[nodiscard]] std::string GetFileExtension(const std::string& fileName);
[[nodiscard]] std::string GetFileName(const std::string& filePath);
[[nodiscard]] std::string GetFileNameWithoutExt(const std::string& filePath);
[[nodiscard]] std::string GetDirectoryPath(const std::string& filePath);
[[nodiscard]] std::string GetWorkingDirectory();
[[nodiscard]] bool ChangeDirectory(const std::string& dir);
[[nodiscard]] long GetFileModTime(const std::string& fileName);
// Load an entire file into memory; caller frees with UnloadFileData.
[[nodiscard]] unsigned char* LoadFileData(const std::string& fileName, int* bytesRead);
void UnloadFileData(unsigned char* data);
[[nodiscard]] bool SaveFileData(const std::string& fileName, const void* data, int bytesToWrite);
[[nodiscard]] char* LoadFileText(const std::string& fileName);
void UnloadFileText(char* text);
[[nodiscard]] bool SaveFileText(const std::string& fileName, const std::string& text);
// --- additional path/file helpers (raylib parity) ---
[[nodiscard]] int GetFileLength(const std::string& fileName);
[[nodiscard]] bool IsFileExtension(const std::string& fileName, const std::string& ext);
[[nodiscard]] bool IsPathFile(const std::string& path);
[[nodiscard]] bool IsFileNameValid(const std::string& fileName);
[[nodiscard]] std::string GetPrevDirectoryPath(const std::string& dirPath);
[[nodiscard]] std::string GetApplicationDirectory();
[[nodiscard]] bool MakeDirectory(const std::string& dirPath);
[[nodiscard]] int GetDirectoryFileCount(const std::string& dirPath);
// Directory listing; free the result with UnloadDirectoryFiles.
[[nodiscard]] FilePathList LoadDirectoryFiles(const std::string& dirPath);
[[nodiscard]] FilePathList LoadDirectoryFilesEx(const std::string& basePath, const std::string& filter, bool scanSubdirs);
void UnloadDirectoryFiles(FilePathList files);
// Export a byte array as a C array source file (raylib ExportDataAsCode).
[[nodiscard]] bool ExportDataAsCode(const unsigned char* data, int dataSize, const std::string& fileName);
// File operations (return true on success).
[[nodiscard]] bool FileCopy(const std::string& srcPath, const std::string& dstPath);
[[nodiscard]] bool FileMove(const std::string& srcPath, const std::string& dstPath);
[[nodiscard]] bool FileRemove(const std::string& fileName);
[[nodiscard]] bool FileRename(const std::string& srcPath, const std::string& dstPath);

// ===========================================================================
// Text / string utilities (raylib rtext string helpers)
// ===========================================================================
// Format like printf into a rotating set of static buffers (raylib TextFormat).
[[nodiscard]] const char* TextFormat(const char* text, ...);
[[nodiscard]] int TextLength(const char* text);
[[nodiscard]] bool TextIsEqual(const char* a, const char* b);
[[nodiscard]] const char* TextSubtext(const char* text, int position, int length);
// Returns a heap string (free with MemFree), replacing all `replace` with `by`.
[[nodiscard]] char* TextReplace(const char* text, const char* replace, const char* by);
[[nodiscard]] char* TextInsert(const char* text, const char* insert, int position);
[[nodiscard]] const char* TextJoin(const char** textList, int count, const char* delimiter);
[[nodiscard]] const char** TextSplit(const char* text, char delimiter, int* count);
void TextAppend(char* text, const char* append, int* position);
[[nodiscard]] int TextFindIndex(const char* text, const char* find);
[[nodiscard]] const char* TextToUpper(const char* text);
[[nodiscard]] const char* TextToLower(const char* text);
[[nodiscard]] const char* TextToPascal(const char* text);
[[nodiscard]] const char* TextToCamel(const char* text);
[[nodiscard]] const char* TextToSnake(const char* text);
[[nodiscard]] int TextToInteger(const char* text);
[[nodiscard]] float TextToFloat(const char* text);
int TextCopy(char* dst, const char* src);
// Heap-returning variants (free with MemFree).
[[nodiscard]] char* TextReplaceAlloc(const char* text, const char* replace, const char* by);
[[nodiscard]] char* TextInsertAlloc(const char* text, const char* insert, int position);
[[nodiscard]] char* TextReplaceBetween(const char* text, const char* start, const char* end, const char* by);
[[nodiscard]] char* TextReplaceBetweenAlloc(const char* text, const char* start, const char* end, const char* by);
[[nodiscard]] const char* TextGetBetween(const char* text, const char* start, const char* end);
[[nodiscard]] const char* GetTextBetween(const char* text, const char* begin, const char* end);
[[nodiscard]] const char* TextRemoveSpaces(const char* text);

// --- Unicode / codepoints ---
[[nodiscard]] int GetCodepoint(const char* text, int* codepointSize);
[[nodiscard]] int GetCodepointPrevious(const char* text, int* codepointSize);
[[nodiscard]] int* LoadCodepoints(const std::string& text, int* count);
void UnloadCodepoints(int* codepoints);
[[nodiscard]] char* LoadUTF8(const int* codepoints, int length);
void UnloadUTF8(char* text);

// --- text line splitting ---
[[nodiscard]] char** LoadTextLines(const std::string& text, int* count);
void UnloadTextLines(char** lines, int count);
void SetTextLineSpacing(int spacing);

// ===========================================================================
// Config flags (window creation hints)
// ===========================================================================
enum ConfigFlags : unsigned int {
    FLAG_VSYNC_HINT         = 0x00000040,
    FLAG_FULLSCREEN_MODE    = 0x00000002,
    FLAG_WINDOW_RESIZABLE   = 0x00000004,
    FLAG_WINDOW_UNDECORATED = 0x00000008,
    FLAG_WINDOW_HIDDEN      = 0x00000080,
    FLAG_WINDOW_MINIMIZED   = 0x00000200,
    FLAG_WINDOW_MAXIMIZED   = 0x00000400,
    FLAG_WINDOW_TOPMOST     = 0x00001000,
    FLAG_WINDOW_HIGHDPI     = 0x00002000,
    FLAG_MSAA_4X_HINT       = 0x00000020,
};

// ===========================================================================
// Misc
// ===========================================================================
void SetConfigFlags(unsigned int flags);
void TakeScreenshot(const std::string& fileName);
// raylib returns an int in [min,max] inclusive (was float in older MeowyRender).
[[nodiscard]] int GetRandomValue(int min, int max);
void SetRandomSeed(unsigned int seed);
// Load `count` unique random ints in [min,max]; free with UnloadRandomSequence.
[[nodiscard]] int* LoadRandomSequence(unsigned int count, int min, int max);
void UnloadRandomSequence(int* sequence);
void TraceLog(int logLevel, const std::string& text);
void OpenURL(const std::string& url);

// ===========================================================================
// rcore: data encoding, hashing and compression (CPU utilities)
// ===========================================================================
// All returned pointers are heap-allocated; free with MemFree (raylib parity).
[[nodiscard]] void* MemAlloc(unsigned int size);
[[nodiscard]] void* MemRealloc(void* ptr, unsigned int size);
void MemFree(void* ptr);
// Base64: EncodeDataBase64 returns a NUL-terminated string; *outputSize is the
// encoded length including the terminator (raylib semantics).
[[nodiscard]] char* EncodeDataBase64(const unsigned char* data, int dataSize, int* outputSize);
[[nodiscard]] unsigned char* DecodeDataBase64(const char* text, int* outputSize);
// DEFLATE compression (via miniz/tinfl-compatible path). Free with MemFree.
[[nodiscard]] unsigned char* CompressData(const unsigned char* data, int dataSize, int* compDataSize);
[[nodiscard]] unsigned char* DecompressData(const unsigned char* compData, int compDataSize, int* dataSize);
// Hashes. CRC32 returns the value; MD5/SHA1 return a pointer to a static buffer
// (raylib semantics: 4 ints / 5 ints). SHA256 is a MeowyRender extension.
[[nodiscard]] unsigned int ComputeCRC32(unsigned char* data, int dataSize);
[[nodiscard]] unsigned int* ComputeMD5(unsigned char* data, int dataSize);
[[nodiscard]] unsigned int* ComputeSHA1(unsigned char* data, int dataSize);
[[nodiscard]] unsigned int* ComputeSHA256(unsigned char* data, int dataSize);

// ===========================================================================
// rcore: screen/world space and camera matrices
// ===========================================================================
[[nodiscard]] Ray GetScreenToWorldRay(Vector2 position, Camera3D camera);
[[nodiscard]] Ray GetScreenToWorldRayEx(Vector2 position, Camera3D camera, int width, int height);
[[nodiscard]] Vector2 GetWorldToScreenEx(Vector3 position, Camera3D camera, int width, int height);
[[nodiscard]] Vector2 GetWorldToScreen2D(Vector2 position, Camera2D camera);
[[nodiscard]] Vector2 GetScreenToWorld2D(Vector2 position, Camera2D camera);
[[nodiscard]] Matrix GetCameraMatrix2D(Camera2D camera);
void UpdateCameraPro(Camera3D* camera, Vector3 movement, Vector3 rotation, float zoom);

// ===========================================================================
// rcore: logging, callbacks, misc window control
// ===========================================================================
void SetTraceLogLevel(int logLevel);
// Callback typedefs (raylib parity).
using TraceLogCallback = void (*)(int logLevel, const char* text, va_list args);
using LoadFileDataCallback = unsigned char* (*)(const char* fileName, int* dataSize);
using SaveFileDataCallback = bool (*)(const char* fileName, void* data, int dataSize);
using LoadFileTextCallback = char* (*)(const char* fileName);
using SaveFileTextCallback = bool (*)(const char* fileName, char* text);
void SetTraceLogCallback(TraceLogCallback callback);
void SetLoadFileDataCallback(LoadFileDataCallback callback);
void SetSaveFileDataCallback(SaveFileDataCallback callback);
void SetLoadFileTextCallback(LoadFileTextCallback callback);
void SetSaveFileTextCallback(SaveFileTextCallback callback);
void SwapScreenBuffer();
void ToggleBorderlessWindowed();
[[nodiscard]] Image GetClipboardImage();
[[nodiscard]] int GetShaderLocationAttrib(Shader shader, const std::string& attribName);
void SetShaderValueTexture(Shader shader, int locIndex, Texture2D texture);
[[nodiscard]] int GetDirectoryFileCountEx(const std::string& basePath, const std::string& filter, bool scanSubdirs);
[[nodiscard]] int FileTextFindIndex(const std::string& fileName, const std::string& find);
[[nodiscard]] bool FileTextReplace(const std::string& fileName, const std::string& find, const std::string& by);

// --- dropped files ---
[[nodiscard]] bool IsFileDropped();
[[nodiscard]] FilePathList LoadDroppedFiles();
void UnloadDroppedFiles(FilePathList files);

// ===========================================================================
// rcore: automation events
// ===========================================================================
[[nodiscard]] AutomationEventList LoadAutomationEventList(const std::string& fileName);
void UnloadAutomationEventList(AutomationEventList list);
[[nodiscard]] bool ExportAutomationEventList(AutomationEventList list, const std::string& fileName);
void SetAutomationEventList(AutomationEventList* list);
void SetAutomationEventBaseFrame(int frame);
void StartAutomationEventRecording();
void StopAutomationEventRecording();
void PlayAutomationEvent(AutomationEvent event);

} // namespace meowyrender
