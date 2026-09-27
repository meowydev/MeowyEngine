# meowyrender

A raylib-style 2D + 3D rendering library written in **C++26**, built as a
**static library**, running the same API over three graphics backends:

- **OpenGL** 3.3 core - implemented core rendering (2D + 3D, textures, text, render
  textures, custom shaders)
- **Metal** for macOS and native windowed visionOS - 2D + 3D, depth,
  render textures, custom MSL shaders and multisampling.
- **Vulkan** (with **MoltenVK** on Apple) - swapchain, batched geometry,
  depth, custom GLSL, materials, instancing/skinning, texture uploads,
  mipmaps, multisampling, and GPU readback.

The public API mirrors raylib's feel (`InitWindow`, `BeginDrawing`, `DrawText`,
`DrawModel`, `LoadTexture`, the color palette, the math helpers, input queries)
but everything lives in the `meowyrender` namespace and is modern C++.

```cpp
#include <meowyrender/meowyrender.hpp>
using namespace meowyrender;

int main() {
    InitWindow(800, 450, "hello meowyrender");
    SetTargetFPS(60);
    Model cube = LoadModelFromMesh(GenMeshCube(2, 2, 2));
    Camera3D cam{{4,4,4}, {0,0,0}, {0,1,0}, 45.0f, CameraProjection::Perspective};

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(cam);
            DrawModel(cube, {0,0,0}, 1.0f, RED);
            DrawGrid(10, 1.0f);
        EndMode3D();
        DrawText("It runs on OpenGL, Metal, or Vulkan!", 20, 20, 20, DARKGRAY);
        EndDrawing();
    }
    UnloadModel(cube);
    CloseWindow();
}
```

## Feature coverage

**Core / window / input**
- Window lifecycle, timing, target FPS, FPS counter, config flags
- Keyboard / mouse / gamepad (pressed / down / released edges)
- Multi-monitor queries, clipboard, gestures/touch, file-system utilities

**2D**
- Shapes: pixels, lines (thick), rectangles (rounded/gradient/rotated/outlined),
  circles, sectors, ellipses, triangles, polygons
- Collision helpers (rec/rec, circle/circle, point tests)
- 2D camera, blend modes, scissor clipping

**Textures & images**
- Real image loading via stb_image (PNG/JPG/BMP/TGA/GIF/HDR/PSD)
- All 13 uncompressed pixel formats: packed integers, float32 and float16
- Procedural generators; resize/crop/flip/tint/invert/grayscale/brightness
- HDR-preserving load, float conversion and resize; PNG/BMP/TGA/JPG/HDR export
- Mipmaps, filter/wrap modes, `DrawTexturePro` family

**Text**
- Built-in bitmap font (zero external files) + TrueType/OTF via stb_truetype
- Packed glyph atlas, UTF-8 codepoints, `MeasureText`, `DrawTextEx`

**3D**
- `BeginMode3D`, perspective/ortho cameras, depth buffer
- 3D primitives (cube, sphere, cylinder, plane, line, grid), billboards
- Meshes + materials, mesh generators (cube/plane/sphere/cylinder/torus)
- Model loading: **OBJ** (custom parser) and **glTF 2.0** (cgltf)
- Bounding boxes, 3D collision, ray casting

**Audio**
- miniaudio device, WAV/MP3/FLAC/OGG, `Sound` one-shots + streamed `Music`,
  volume / pitch / pan, master volume

**GPU**
- Render textures (offscreen framebuffers), custom shaders
  (`LoadShader` / `SetShaderValue`) on all three backends. Shader source
  is backend-native: GLSL 330 for OpenGL, GLSL 450 for Vulkan and MSL for Metal.

## Architecture

The public API is a thin dispatch layer over an immediate-mode vertex batch
that is handed to the backend selected at runtime during `InitWindow` (all
platform-supported backends are compiled in). Backends implement one
interface (`meowyrender::backend::RenderBackend`) and never appear in the
public headers.

```
include/meowyrender/     public API (namespace meowyrender)
src/backend/             RenderBackend interface + OpenGL / Metal / Vulkan
src/core/                window, input, timing, batch renderer, state
src/modules/             shapes, textures, text, audio, 3D, models,
                         model loading, shaders, system utilities
third_party/             vendored single-header libs (see Attribution)
samples/showcase/        multi-scene demo (2D / textures+text / 3D / shaders)
```

3D works through the same batch: `Vertex` carries a Z coordinate and the
backend applies `projection * modelview`, so `BeginMode3D` just sets a
perspective projection + look-at view and enables depth testing.

## Building

Requirements: CMake 3.25+ with support for your C++26 compiler, Ninja recommended (the Make
generator breaks on toolchain paths containing spaces). GLFW is fetched
automatically.

```sh
# Multi-backend desktop build (default). Every backend supported on the
# platform is compiled in; the active one is chosen at runtime by InitWindow.
# On macOS: Metal (auto), plus Vulkan/MoltenVK and OpenGL for explicit use.
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build build
./build/bin/showcase                 # auto-selects the best backend
./build/bin/showcase --backend opengl  # or force one

# Smallest binary: legacy single-backend build (only OpenGL compiled).
cmake -S . -B build-gl -G Ninja -DMEOWY_BACKEND=OpenGL
cmake --build build-gl

# Disable an individual backend (e.g. drop the Vulkan dependency):
cmake -S . -B build-novk -G Ninja -DMEOWY_ENABLE_VULKAN=OFF
```

### Runtime backend selection

`InitWindow` picks the best available backend for the platform automatically and
falls back to the next one if initialization fails. Override the choice from
code, an env var, or (via the sample's own parsing) the command line:

```cpp
meowyrender::SetPreferredBackend(meowyrender::Backend::Vulkan);
meowyrender::InitWindow(800, 600, "app");     // tries Vulkan, falls back if needed
meowyrender::GetActiveBackend();              // Backend actually initialized
meowyrender::GetActiveBackendName();          // e.g. "Metal (MTLDevice)"
meowyrender::IsBackendAvailable(meowyrender::Backend::Metal);
```

- Precedence: **explicit `SetPreferredBackend` > `MEOWY_BACKEND` env var >
  automatic**. If the chosen backend fails, the next supported backend is tried;
  every attempt is logged.
- Automatic order: macOS = Metal -> Vulkan(MoltenVK) -> OpenGL; visionOS = Metal
  only; Windows/Linux = Vulkan -> OpenGL.
- `SetPreferredBackend` after a window exists is rejected (logged) -- GPU
  resources belong to the active backend; live switching is not supported.
- `MEOWY_BACKEND=metal|vulkan|opengl` env var also selects a backend at startup.

### Vulkan / MoltenVK on macOS

Install the loader + MoltenVK, then configure with the prefix path above:

```sh
brew install molten-vk vulkan-headers vulkan-loader glslang
```

meowyrender calls `glfwInitVulkanLoader(vkGetInstanceProcAddr)` before
`glfwInit()`, so the app finds the Homebrew loader without needing
`VK_ICD_FILENAMES` / `DYLD_LIBRARY_PATH` env vars.

### CMake options

| Option | Default | Meaning |
| --- | --- | --- |
| `MEOWY_ENABLE_OPENGL` | `ON` | Compile the OpenGL backend (if supported) |
| `MEOWY_ENABLE_METAL` | `ON` | Compile the Metal backend (Apple only) |
| `MEOWY_ENABLE_VULKAN` | `ON` | Compile the Vulkan backend (auto-disabled if Vulkan/glslang missing) |
| `MEOWY_BACKEND` | *(empty)* | Legacy single-backend build: `OpenGL`/`Metal`/`Vulkan` compiles only that one. Empty = multi-backend. |
| `MEOWY_VULKAN_USE_MOLTENVK` | `ON` | Use MoltenVK for Vulkan on Apple |
| `MEOWY_WITH_IMGUI` | `OFF` | Fetch and integrate Dear ImGui (see below). Works on desktop OpenGL/Metal/Vulkan and native visionOS Metal. |
| `MEOWY_BUILD_SAMPLES` | `ON` | Build the `showcase` sample |

By default all platform-supported backends are compiled into the static library
and selected at runtime. Set `MEOWY_BACKEND=X` (or turn off the other
`MEOWY_ENABLE_*`) for a smaller single-backend binary. If Vulkan's dependencies
are missing, Vulkan is disabled cleanly and Metal/OpenGL are retained (unless
`MEOWY_BACKEND=Vulkan` was explicitly requested, which then errors).

## Using it in your project

```cmake
add_subdirectory(meowyrender)
target_link_libraries(mygame PRIVATE meowyrender::meowyrender)
```

## Dear ImGui integration (optional)

Build with `-DMEOWY_WITH_IMGUI=ON` to fetch Dear ImGui (v1.91.5-docking) and
compile the renderer backend(s) for whichever MeowyRender backends are enabled.
No ImGui or graphics-API types leak into the public headers.

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/homebrew -DMEOWY_WITH_IMGUI=ON
cmake --build build
```

The frame is driven for you: `BeginDrawing` starts the ImGui frame and
`EndDrawing` renders it just before the backend presents. You call
`InitImGui()` once, then raw `ImGui::` widgets between `BeginDrawing` and
`EndDrawing`:

```cpp
mr::InitWindow(900, 600, "app");
if (mr::InitImGui()) {                 // false if the build/backend lacks ImGui
    while (!mr::WindowShouldClose()) {
        mr::BeginDrawing();
        mr::ClearBackground(mr::Color{30, 30, 40, 255});
        mr::DrawCircle(200, 300, 60, mr::SKYBLUE);   // scene draws normally

        ImGui::Begin("Debug");                        // raw Dear ImGui
        ImGui::Text("Backend: %s", mr::GetActiveBackendName());
        ImGui::End();

        mr::EndDrawing();
    }
    mr::ShutdownImGui();
}
mr::CloseWindow();
```

Public API (declared in `meowyrender.hpp`): `IsImGuiAvailable()`, `InitImGui()`,
`ShutdownImGui()`. When the library is built without ImGui, `IsImGuiAvailable()`
returns `false` and `InitImGui()` is a no-op returning `false`, so the same
source compiles either way.

Platform layer per target:
- **Desktop** (OpenGL / Metal / Vulkan over GLFW): `imgui_impl_glfw` supplies input.
- **visionOS** (Metal only, no GLFW): a custom platform layer feeds ImGui IO
  from the UIKit view (display size + scale) and the engine's input, rendering
  via `imgui_impl_metal` into the view's `CAMetalLayer`.

`MEOWY_WITH_IMGUI` requires either a GLFW desktop backend or visionOS; it is a
no-op on other configurations.

## Backend status

See [the verification record](docs/VERIFICATION.md) for exact test results and
unverified configurations, and [the usage guide](docs/USAGE.md) for examples.

| Capability | OpenGL | Metal | Vulkan |
| --- | --- | --- | --- |
| Desktop window, 2D/3D, depth | Implemented | Implemented | Implemented |
| Render textures, screen/texture readback | Implemented | Implemented | Implemented |
| Custom shaders | GLSL 330 | MSL | GLSL 450 / runtime SPIR-V |
| GPU instancing (unlit + lit) | Implemented | Implemented | Implemented |
| Skeletal skinning | Implemented | Implemented | Implemented |
| Persistent GPU mesh upload | Implemented | Implemented | Streams (API no-op) |
| Lit metallic/roughness materials | Implemented | Implemented | Implemented |
| Image-based lighting (env cubemap) | Implemented | Implemented | Implemented (runtime) |
| Precomputed split-sum IBL | Implemented | Implemented | Maps generated; rendered via runtime path |
| Directional shadow mapping | Implemented | Implemented | Implemented |
| Cascaded shadow mapping | Implemented | Implemented | Not supported |
| glTF alpha mask / cutoff | Implemented | Implemented | Implemented |
| Alpha, additive, multiply blending | Implemented | Implemented | Implemented |
| 13 uncompressed texture upload formats | Implemented | Implemented | Implemented |
| Compressed textures and decoded readback | Driver-dependent | Driver-dependent | Driver-dependent |
| Native cubemaps, skyboxes, stereo simulator | Implemented | Implemented | Implemented |
| 4x MSAA hint | Window | Window + offscreen | Window + offscreen |
| Native visionOS application hosting | Not applicable | Windowed UIKit host | Not applicable |
| Dear ImGui integration (`MEOWY_WITH_IMGUI`) | Implemented (glfw) | Implemented (glfw desktop + visionOS custom) | Implemented (glfw) |

This is **not complete raylib parity**. Public types and familiar function names
do not imply every raylib feature exists. Native visionOS is a UIKit windowed host, not an immersive compositor.
Desktop runtime tests have only run on Apple-silicon macOS.
Windows/Linux need independent builds and runtime verification.

### Models and materials

glTF skinning retains node hierarchy, inverse bind matrices and normalized
four-joint vertex influences. Animation loading samples LINEAR, STEP and
CUBICSPLINE TRS channels at 60 Hz, with quaternion interpolation and bounded
sample memory. `UpdateModelAnimation` updates the GPU palette; bounds and wire
drawing use the animated positions. Morph targets and more than four influences
per vertex are not supported.

Set `material.lighting = true` to enable direct-light Cook-Torrance GGX shading.
Metallic/roughness, normal, occlusion and emission maps are supported, along
with `SetDirectionalLight` and `SetAmbientLight`. Imported materials remain
unlit until explicitly enabled. The active glTF scene is respected. OBJ
supports geometry, not MTL materials.

**Instancing and persistent meshes.** `DrawMeshInstanced` uses GPU instancing
for unlit and lit meshes (the shader applies each per-instance transform to
position and normal). Skinned meshes fall back to individual draws (each
instance would need a distinct pose). `UploadMesh` stores a mesh in a
GPU-resident buffer so it is not re-streamed every frame; the persistent path
is used for static (non-skinned) meshes drawn with an opaque-white albedo tint,
and is implemented on OpenGL and Metal (Vulkan streams via the same API). Tinted,
skinned, alpha-masked or secondary-UV/transformed materials use the streaming
path.

**glTF materials.** Alpha modes are parsed: `MASK` applies an alpha-test cutoff
in the lit shader (all three backends); `BLEND` currently renders with ordinary
alpha blending (no separate opaque/transparent sort). `KHR_texture_transform`
(offset/scale/rotation) and a secondary UV set (`TEXCOORD_1`) are parsed and
applied to the sampled albedo coordinate on the CPU during tessellation; this is
not a full per-map multi-UV shader path. More than four joint influences per
vertex (multiple `JOINTS_n`/`WEIGHTS_n` sets) are clamped to the four strongest
and renormalized rather than rejected. Morph targets are not supported.

**Shadows and image-based lighting.** `SetEnvironmentLight(cubemap, intensity)`
enables image-based lighting: the environment cubemap is sampled for ambient
diffuse (along the normal) and specular reflection (along the reflection vector,
blurred by roughness). Runtime IBL works on **OpenGL, Metal, and Vulkan**.
`GenEnvironmentLightMaps` + `SetEnvironmentLightPrecomputed` add the precomputed
split-sum pipeline (cosine irradiance + roughness-prefiltered specular + BRDF
LUT) consumed by the lit shader on **OpenGL and Metal** (on Vulkan the maps are
generated but rendered via the runtime path). `BeginShadowMode(lightCamera)/
EndShadowMode()` provide directional shadow mapping (depth pass from the light,
3x3 PCF in the lit shader, attenuating the direct light only), implemented on
**OpenGL, Metal, and Vulkan**; `IsShadowMappingSupported()` reports availability.
Cascaded shadows are OpenGL + Metal (`IsCascadedShadowSupported()`). There is no
contact hardening beyond the 3x3 filter.

### Textures, cubemaps and stereo

Packed integer upload formats expand to RGBA8; float/half-float inputs expand
to RGBA32F. Readback returns RGBA8 with HDR values clamped. This does not imply
lossless HDR behavior for every color operation: tint/invert/brightness convert
to RGBA8. Crop, flips and nearest resize retain the source format. Filtered
resize retains HDR using RGBA32F for floating-point sources. Compressed images
can be copied/uploaded, but CPU pixel editing rejects them. Failed image loads
return an empty image, not a fabricated placeholder.
Compressed formats are native GPU formats: query
`IsTextureFormatSupported(format)` after initialization. Unsupported formats
are not silently described as working. Compressed uploads currently have one
mip level.

Render textures have a consistent **top-left origin**. Use ordinary
`DrawTexture(target.texture, ...)`, without a negative source height.
`LoadTextureCubemap` accepts six-face strips, cross layouts and 2:1 panoramas.
Face order is +X, -X, +Y, -Y, +Z, -Z; cubemap uploads currently produce RGBA8.
`DrawSkybox` handles its internal backend-native shader, ignores camera
translation and preserves foreground depth. Use `UnloadTexture` for cubemaps.

`LoadVrStereoConfig`, `BeginVrStereoMode` and `EndVrStereoMode` provide
side-by-side stereo simulation. They do not create a tracked headset session
or apply lens-distortion post-processing automatically. The device/config
API follows the [raylib stereo API](https://github.com/raysan5/raylib/blob/master/src/raylib.h).

### Vulkan shader interface

Vulkan runtime compilation links glslang (CMake package required). Custom GLSL
450 shaders use vertex locations 0=position, 1=UV, 2=color; a vertex push block
containing projection then modelview matrices; set 0/binding 0 for sampler2D
or samplerCube; and set 0/binding 1 for a std140 uniform block. Named float,
vec2/3/4, int and mat4 values and arrays are reflected and snapshotted per draw.
Unsupported descriptor layouts produce an explicit compilation error.
See `samples/vulkan_shader_checks.cpp` for a complete example.

Vulkan currently serializes submission/presentation for correctness; it is not
a performance-tuned multi-frame renderer.

### Simpler usage and regression checks

`InitWindow` either creates a ready window or throws an exception. Ordinary
examples need no `IsWindowReady()` guard; the query remains for diagnostics.
Input is polled at the end of each frame so update-before-draw loops work.
For a smaller entry point, `RunApplication(width, height, title, frame, init,
cleanup)` owns the window and frame lifecycle. The frame callback is already
inside `BeginDrawing`/`EndDrawing`. This host works on desktop and native
visionOS; a blocking desktop `while` loop is not a UIKit application host.
See [quickstart.cpp](samples/quickstart.cpp) and [the usage guide](docs/USAGE.md).
Named shader values avoid manual location/type setup:

```cpp
SetShaderValue(shader, "uTime", static_cast<float>(GetTime()));
SetShaderValue(shader, "resolution", Vector2{800, 450});
```

Shader source still uses the backend's language: GLSL for OpenGL/Vulkan and MSL for
Metal. These overloads do not translate shader source.

`bin/backend_checks` checks texture readback, draw ordering across texture
updates and screenshots, indexed geometry, persistent-mesh uploads, repeated
clears, and window reinitialization. It passes on OpenGL, Metal (with GPU
validation), and Vulkan/MoltenVK. Additional suites cover materials (including
lit GPU instancing, image-based lighting, precomputed split-sum IBL on GL/Metal,
and directional shadows on GL/Metal/Vulkan), animation, offscreen rendering,
cubemaps, stereo, Vulkan custom shaders,
and a `gltf_checks` suite that validates glTF alpha modes, `KHR_texture_transform`,
secondary UV sets and >4-joint clamping. `bin/render_checks` runs format,
shader, blend, clipping and rotated-text (`DrawTextPro`) pixel tests on all
three backends.

Run `ctest --test-dir build --output-on-failure` (or use `build-metal` /
`build-vk`). Tests open short-lived windows and need an active graphical
session. Metal tests automatically enable API and GPU validation. The expanded
render tests cover upload formats, compressed decoding, shader values,
blending and clipping across readback. Unsupported compressed formats print
`SKIP`; they are not reported as verified support.

For graphics-only showcase testing, set `MEOWY_SHOWCASE_NO_AUDIO=1`.
The five showcase scenes cover shapes, textures/text, lit models, custom
shaders/offscreen rendering, and cubemaps/animation/instancing/stereo. Select
with arrows or 1–5; scene 5 uses S to toggle stereo.

Audio file tests cover WAV export and WAV/MP3/FLAC/Ogg decoding from files and
memory. A separate muted device smoke test successfully initialized CoreAudio,
created a sound and exercised volume controls. An earlier CoreAudio hang did
not reproduce in that test; this is not evidence that every audio-device
configuration is reliable. Unload sounds/music before closing the audio device.
Waves loaded by this library contain float32 samples. `LoadSoundFromWave`
copies its source PCM; pan uses 0=left, 0.5=center, 1=right.

### Native visionOS build

Requires an Xcode version with the visionOS SDK, and CMake with visionOS
support (3.28+). Use the native host, not GLFW:

```sh
cmake -S . -B build-visionos-sim -G Ninja \
  -DCMAKE_SYSTEM_NAME=visionOS -DCMAKE_OSX_SYSROOT=xrsimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=2.0 \
  -DMEOWY_BACKEND=Metal -DBUILD_TESTING=OFF
cmake --build build-visionos-sim
```

The app bundle is `build-visionos-sim/samples/visionos_showcase.app`.
For an unsigned device build, use another build folder and
`-DCMAKE_OSX_SYSROOT=xros`. Physical installation needs your signing/team
configuration.

The native simulator smoke test renders a lit cube, text and a clipped
rectangle, then checks a 1800×1200 GPU screenshot. It passed with Metal API
validation. A device-target static library and unsigned app also build;
physical Vision Pro execution has **not** been verified. Full Metal shader
validation on the simulator rejected a pipeline with a buffer-index error;
the same desktop suites pass GPU validation. That simulator instrumentation
issue remains unresolved, not silently counted as a passing validation run.

The windowed host supports UIKit interaction, basic keyboard/controller input,
clipboard and native presentation. It does not add immersive spaces, tracked
stereo views or CompositorServices. Physical input devices still need hands-on
testing.

## Attribution

Vendored third-party single-header libraries under `third_party/`:

- [stb](https://github.com/nothings/stb) (stb_image, stb_image_write,
  stb_image_resize2, stb_truetype, stb_vorbis) - public domain / MIT dual license,
  by Sean Barrett. `stb_vorbis.c` is pinned to commit
  `2c980bb59875b0d32144a71867fbdebb2f77cd20`.
- [miniaudio](https://github.com/mackron/miniaudio) - public domain / MIT-0,
  by David Reid
- [cgltf](https://github.com/jkuhlmann/cgltf) - MIT, by Johannes Kuhlmann

GLFW is fetched at version 3.4 (zlib/libpng license). Vulkan custom shader
compilation links glslang (BSD-style license; see its distribution), in addition
to the Vulkan loader and optional MoltenVK. These dependencies retain their
own licenses when distributing a statically linked application.

All other source in this repository is original to meowyrender.
