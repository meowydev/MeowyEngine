# Using MeowyRender

The library is `meowyrender::meowyrender`; all public functions and types live
in the `meowyrender` C++ namespace. `namespace mr = meowyrender;` is optional.
Pick one backend when configuring CMake. Ordinary drawing code stays the same.

## Add it to your build

```cmake
set(MEOWY_BACKEND Metal CACHE STRING "") # OpenGL, Metal, or Vulkan
set(MEOWY_BUILD_SAMPLES OFF CACHE BOOL "")
add_subdirectory(path/to/meowyrender)
target_link_libraries(MyGame PRIVATE meowyrender::meowyrender)
```

The target supplies includes, C++26 requirements and link dependencies. You do
not need to manually list every framework or link the static archive yourself.
Building your executable builds the library dependency automatically.

## Minimal application

```cpp
#include <meowyrender/meowyrender.hpp>
namespace mr = meowyrender;

int main() {
    mr::RunApplication(800, 450, "Meow", [] {
        mr::ClearBackground(mr::RAYWHITE);
        mr::DrawCircle(400, 225, 60, mr::SKYBLUE);
        mr::DrawText("Hello :3", 340, 320, 24, mr::DARKGRAY);
    });
}
```

`RunApplication` creates the window and wraps each callback in a drawing frame.
Use `RequestWindowClose()` to stop it. The optional initialization and cleanup
callbacks run while the graphics context exists. If a desktop frame throws,
cleanup is attempted before the context closes. A failing initialization must
clean up any partially created user resources itself.

The familiar desktop `InitWindow` → `while (!WindowShouldClose())` →
`BeginDrawing`/`EndDrawing` → `CloseWindow` style still works.
`InitWindow` throws if setup fails: no mandatory `IsWindowReady()` guard.
Native visionOS uses `RunApplication` so UIKit owns the event loop.

## Textures and offscreen drawing

Load GPU resources after initialization and unload them before closing:

```cpp
auto texture = mr::LoadTexture("assets/cloud.png");
auto target = mr::LoadRenderTexture(512, 512);

// Inside a frame:
mr::BeginTextureMode(target);
mr::ClearBackground(mr::BLANK);
mr::DrawTexture(texture, 20, 20, mr::WHITE);
mr::EndTextureMode();
mr::DrawTexture(target.texture, 100, 100, mr::WHITE);

// During cleanup:
mr::UnloadRenderTexture(target);
mr::UnloadTexture(texture);
```

Offscreen textures use the same top-left orientation on all backends. No
negative-height workaround is required. Missing files return empty resources;
checking `texture.id` is useful for optional assets, unlike mandatory window
initialization. Call `TakeScreenshot` during a frame. Texture readback returns
an owned CPU `Image`; release it with `UnloadImage`.

## Lit models and animation

```cpp
auto model = mr::LoadModel("assets/chaser.glb");
for (int i = 0; i < model.materialCount; ++i)
    model.materials[i].lighting = true;
mr::SetDirectionalLight({-1, -1, -1}, mr::WHITE, 3.0f);

int clipCount = 0;
auto* clips = mr::LoadModelAnimations("assets/chaser.glb", &clipCount);
// Per frame, before drawing:
if (clipCount > 0)
    mr::UpdateModelAnimation(model, clips[0],
        static_cast<int>(mr::GetTime() * clips[0].frameRate));

mr::BeginMode3D(camera);
mr::DrawModel(model, {0, 0, 0}, 1.0f, mr::WHITE);
mr::EndMode3D();

// During cleanup:
mr::UnloadModelAnimations(clips, clipCount);
mr::UnloadModel(model);
```

The default material remains unlit for straightforward raylib-like drawing.
Lighting is direct metallic/roughness shading. Animation clamps to the four
strongest skeletal influences per vertex (multiple `JOINTS_n`/`WEIGHTS_n` sets
are merged and renormalized), and does not support morph targets.
`DrawMeshInstanced(mesh, material, transforms, count)` uses GPU instancing for
unlit and lit meshes; skinned and custom-shader cases fall back to individual
draws. `UploadMesh(&mesh, false)` optionally caches static meshes in a
GPU-resident buffer (OpenGL/Metal) so they are not re-streamed each frame;
plain streaming still works without it.

### Image-based lighting and shadows

```cpp
// IBL: sample an environment cubemap for ambient + reflection (all backends).
auto sky = mr::LoadTextureCubemap(mr::LoadImage("assets/sky.hdr"));
mr::SetEnvironmentLight(sky, 1.0f);   // lit materials now reflect the env
// ... draw lit models ...
mr::ClearEnvironmentLight();

// Directional shadow mapping (OpenGL/Metal/Vulkan; guard with the query).
if (mr::IsShadowMappingSupported()) {
    mr::Camera3D lightCam{{0,10,0}, {0,0,0}, {0,0,-1}, 8.0f,
                          mr::CameraProjection::Orthographic};
    mr::BeginShadowMode(lightCam, 2048);   // depth pass from the light
        mr::DrawModel(caster, {0,0,0}, 1.0f, mr::WHITE);
    mr::EndShadowMode();
    mr::BeginMode3D(camera);
        mr::DrawModel(floor, {0,0,0}, 1.0f, mr::WHITE); // receives shadow
    mr::EndMode3D();
    mr::ClearShadowMap();
}
```

Runtime IBL works on all three backends; the precomputed split-sum pipeline
(`GenEnvironmentLightMaps` + `SetEnvironmentLightPrecomputed`) is consumed by the
lit shader on OpenGL and Metal (on Vulkan the maps are generated but rendered via
the runtime path). Single directional shadow mapping works on OpenGL, Metal, and
Vulkan; still gate shadow-dependent rendering with `IsShadowMappingSupported()`.
Cascaded (multi-split) shadows are OpenGL + Metal only
(`IsCascadedShadowSupported()`).

## Custom shaders

```cpp
auto shader = mr::LoadShaderFromMemory(vertexSource, fragmentSource);
mr::BeginShaderMode(shader);
mr::SetShaderValue(shader, "uTime", static_cast<float>(mr::GetTime()));
mr::SetShaderValue(shader, "tint", mr::Vector4{1, 0.8f, 0.6f, 1});
mr::DrawRectangle(20, 20, 200, 100, mr::WHITE);
mr::EndShaderMode();
```

Source language is backend-native, not automatically translated. OpenGL uses
GLSL 330; Metal uses MSL `vs_main`/`fs_main`, transforms at vertex buffer 1 and
custom uniform structs at buffer 2; Vulkan uses the GLSL 450 descriptor ABI in
the README. Named values support float, int, Vector2/3/4 and Matrix. See
`samples/shader_value_checks.cpp` for matching complete sources on all backends.

## Ownership and limits

Resource structs are lightweight handles, not reference-counted owners. Copying
a `Model`, `Texture`, `Font`, `Image`, `Sound` or animation pointer does not make
a second independent resource. Unload each allocation once. `ImageCopy` creates
an independent CPU image. `LoadModelFromMesh` transfers mesh ownership to the
model; do not also unload that mesh. A loaded glTF model owns its imported
textures. Externally assigned material textures remain yours to manage.

Draw and window operations belong on the main thread. There is one active
window/context. Shader languages and driver-supported compressed formats remain
platform-specific; use `IsTextureFormatSupported` for compressed uploads.
Stereo mode is a side-by-side simulator, not a tracked VR session.

This implements the requested rendering roadmap, not every function or
extension in raylib. The README records verification coverage and remaining
limitations instead of treating declared types as finished features.
