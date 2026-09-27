// meowyrender sample - showcase
//
// A multi-scene demo of the meowyrender API. The same code runs on any backend
// (OpenGL / Metal / Vulkan) since it only touches the public API.
//
// Scenes (switch with LEFT/RIGHT arrows or 1-5):
//   1. 2D shapes    - rectangles, circles, lines, gradients, polygons
//   2. Textures/Text- procedural textures + bitmap/TTF text + palette
//   3. 3D models    - camera, meshes, models, grid, depth
//   4. Shaders      - render-to-texture + a live custom fragment shader
//   5. Advanced     - cubemap sky, skeletal animation, instancing and stereo
#include <meowyrender/meowyrender.hpp>

#include <cmath>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>

using namespace meowyrender;

namespace {

constexpr int kScreenW = 960;
constexpr int kScreenH = 540;
constexpr int kSceneCount = 6;

// A custom fragment shader that tints + waves over time. Shader source
// languages are backend-native: GLSL on OpenGL and MSL on Metal.
// Backend-native wave shader in all three dialects; the correct one is chosen
// from the ACTIVE backend at runtime (WaveVS()/WaveFS()), so the same binary
// works whichever backend InitWindow selects.
const char* kWaveVS_Metal = R"(
#include <metal_stdlib>
using namespace metal;
struct VSIn { float3 position [[attribute(0)]]; float2 texcoord [[attribute(1)]]; float4 color [[attribute(2)]]; };
struct VSOut { float4 position [[position]]; float2 texcoord; float4 color; };
struct Uniforms { float4x4 projection; float4x4 modelview; };
struct CustomUniforms { float uTime; };
vertex VSOut vs_main(VSIn in [[stage_in]], constant Uniforms& u [[buffer(1)]]) {
    VSOut out;
    float4 clip = u.projection * u.modelview * float4(in.position, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;
    out.position = clip; out.texcoord = in.texcoord; out.color = in.color;
    return out;
}
)";
const char* kWaveFS_Metal = R"(
fragment float4 fs_main(VSOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                        sampler samp [[sampler(0)]],
                        constant CustomUniforms& custom [[buffer(2)]]) {
    float w = 0.5 + 0.5 * sin(custom.uTime * 2.0 + in.texcoord.x * 10.0);
    return tex.sample(samp, in.texcoord) * in.color * float4(w, 0.6, 1.0-w, 1.0);
}
)";
const char* kWaveVS_Vulkan = R"(#version 450
layout(location=0) in vec3 aPos;layout(location=1) in vec2 aTex;layout(location=2) in vec4 aColor;
layout(location=0) out vec2 vTex;layout(location=1) out vec4 vColor;
layout(push_constant) uniform Transforms {mat4 projection;mat4 modelview;} transforms;
void main(){gl_Position=transforms.projection*transforms.modelview*vec4(aPos,1);gl_Position.y=-gl_Position.y;gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;vTex=aTex;vColor=aColor;}
)";
const char* kWaveFS_Vulkan = R"(#version 450
layout(location=0) in vec2 vTex;layout(location=1) in vec4 vColor;layout(location=0) out vec4 FragColor;
layout(set=0,binding=0) uniform sampler2D uTexture;
layout(set=0,binding=1,std140) uniform Parameters {float uTime;} parameters;
void main(){float w=0.5+0.5*sin(parameters.uTime*2+vTex.x*10);FragColor=texture(uTexture,vTex)*vColor*vec4(w,0.6,1-w,1);}
)";
const char* kWaveVS_GL = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aTex;
layout(location=2) in vec4 aColor;
uniform mat4 uProjection; uniform mat4 uModelview;
out vec2 vTex; out vec4 vColor;
void main(){ gl_Position=uProjection*uModelview*vec4(aPos,1.0); vTex=aTex; vColor=aColor; }
)";
const char* kWaveFS_GL = R"(#version 330 core
in vec2 vTex; in vec4 vColor; uniform sampler2D uTexture; uniform float uTime;
out vec4 FragColor;
void main(){
    float w = 0.5 + 0.5 * sin(uTime * 2.0 + vTex.x * 10.0);
    FragColor = texture(uTexture, vTex) * vColor * vec4(w, 0.6, 1.0 - w, 1.0);
}
)";
const char* WaveVS() {
    switch (GetActiveBackend()) {
        case Backend::Metal:  return kWaveVS_Metal;
        case Backend::Vulkan: return kWaveVS_Vulkan;
        default:              return kWaveVS_GL;
    }
}
const char* WaveFS() {
    switch (GetActiveBackend()) {
        case Backend::Metal:  return kWaveFS_Metal;
        case Backend::Vulkan: return kWaveFS_Vulkan;
        default:              return kWaveFS_GL;
    }
}

void SceneShapes(float t) {
    DrawText("Scene 1: 2D Shapes", 20, 60, 24, DARKBLUE);

    DrawRectangleGradientV(40, 120, 200, 120, SKYBLUE, DARKBLUE);
    DrawRectangleRounded({280, 120, 200, 120}, 0.4f, 12, ORANGE);
    DrawRectangleLinesEx({520, 120, 200, 120}, 4, MAROON);

    DrawCircle(140, 340, 60, RED);
    DrawCircleLines(140, 340, 70, DARKGRAY);
    DrawPoly({380, 340}, 6, 60, t * 40.0f, VIOLET);
    DrawTriangle({620, 290}, {680, 390}, {560, 390}, LIME);

    for (int i = 0; i < 12; ++i) {
        const float a = t + i * (PI / 6.0f);
        DrawLineEx({820, 340},
                   {820 + std::cos(a) * 70, 340 + std::sin(a) * 70}, 3, GOLD);
    }
}

void SceneTexturesText(Texture2D grad, Font ttf, float t) {
    DrawText("Scene 2: Textures & Text", 20, 60, 24, DARKGREEN);

    DrawTextureEx(grad, {40, 110}, t * 20.0f, 1.2f, WHITE);
    DrawTexture(grad, 240, 110, ColorAlpha(WHITE, 0.7f));

    DrawText("Built-in bitmap font: The quick brown fox 0123456789", 40, 300, 18, DARKGRAY);
    if (IsFontValid(ttf))
        DrawTextEx(ttf, "TrueType font via stb_truetype!", {40, 340}, 32, 2, MAROON);
    DrawText("Unicode-aware DrawTextEx / MeasureText supported", 40, 390, 16, GRAY);

    const Color palette[] = {LIGHTGRAY, GRAY, YELLOW, GOLD, ORANGE, PINK, RED,
                             MAROON, GREEN, LIME, SKYBLUE, BLUE, PURPLE, VIOLET};
    const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    for (int i = 0; i < n; ++i)
        DrawRectangle(40 + i * 40, 440, 36, 40, palette[i]);
}

void Scene3D(Camera3D& cam, Model cube, Model sphere, float t) {
    DrawText("Scene 3: 3D Models & Meshes", 20, 60, 24, DARKPURPLE);

    UpdateCamera(&cam, CameraMode::Orbital);
    BeginMode3D(cam);
        DrawGrid(20, 1.0f);
        DrawModelEx(cube, {-2.5f, 1, 0}, {0, 1, 0}, t * 50.0f, {1, 1, 1}, RED);
        DrawModelWires(cube, {-2.5f, 1, 0}, 1.0f, MAROON);
        DrawModel(sphere, {2.5f, 1, 0}, 1.0f, BLUE);
        DrawCube({0, 0.5f, 2.5f}, 1, 1, 1, GOLD);
        DrawSphereWires({0, 1.2f, -2.5f}, 1.0f, 8, 8, DARKGREEN);
        DrawCylinder({0, 0, 0}, 0.3f, 0.5f, 1.5f, 12, ORANGE);
        Matrix instances[16];
        for(int i=0;i<16;++i) {
            instances[i]=MatrixMultiply(MatrixScale(0.15f,0.15f,0.15f),
                MatrixTranslate((i%4-1.5f)*0.8f,0.2f,(i/4)*0.8f-5.0f));
        }
        DrawMeshInstanced(cube.meshes[0],cube.materials[0],instances,16);
    EndMode3D();

    DrawText("Orbital camera - solid + wireframe meshes with depth", 20, kScreenH - 40, 16, GRAY);
}

void SceneShaders(RenderTexture2D target, Shader wave, float t) {
    DrawText("Scene 4: Render Textures & Shaders", 20, 60, 24, DARKBLUE);

    // Draw a small 2D scene into the offscreen render texture.
    BeginTextureMode(target);
        ClearBackground(DARKGRAY);
        DrawCircle(100, 100, 60, RED);
        DrawRectangle(120, 40, 80, 120, GOLD);
        DrawText("offscreen", 30, 170, 20, RAYWHITE);
    EndTextureMode();

    // Draw the render texture normally.
    DrawTexture(target.texture, 60, 120, WHITE);

    // Draw the same texture through the backend-native custom wave shader.
    if (IsShaderValid(wave)) {
        BeginShaderMode(wave);
            SetShaderValue(wave, "uTime", t);
            DrawTexture(target.texture, 520, 120, WHITE);
        EndShaderMode();
        DrawText("<- plain          custom shader ->", 300, 200, 16, DARKGRAY);
    } else {
        DrawText("Custom shader failed to compile", 520, 200, 14, MAROON);
    }
}

// Scene 6: advanced features added to MeowyRender - cascaded directional
// shadows, transparent (BLEND) depth-sorted geometry, image-based lighting, and
// a morph-target mesh. Falls back gracefully where a backend lacks a feature.
void SceneAdvanced(TextureCubemap sky, Model floor, Model box, Mesh morphMesh,
                   Material morphMat, float t) {
    Camera3D cam{{6, 5, 8}, {0, 1, 0}, {0, 1, 0}, 45, CameraProjection::Perspective};
    Vector3 lightDir{-0.4f, -1.0f, -0.3f};
    SetAmbientLight(WHITE, 0.12f);
    SetDirectionalLight(lightDir, WHITE, 3.0f);
    SetEnvironmentLight(sky, 1.0f);

    const bool cascades = IsCascadedShadowSupported();
    if (cascades) {
        BeginShadowCascades(cam, lightDir, 3, 1024);
        for (int c = 0; c < GetShadowCascadeCount(); ++c) {
            SetShadowCascade(c);
            DrawModel(box, {-2.0f, 1.0f, 0.0f}, 1.0f, WHITE);
            DrawModel(box, {2.5f, 1.0f, -3.0f}, 1.0f, WHITE);
        }
        EndShadowCascades();
    } else if (IsShadowMappingSupported()) {
        Camera3D lightCam{{6, 12, 5}, {0, 0, 0}, {0, 1, 0}, 24.0f, CameraProjection::Orthographic};
        BeginShadowMode(lightCam, 2048);
            DrawModel(box, {-2.0f, 1.0f, 0.0f}, 1.0f, WHITE);
            DrawModel(box, {2.5f, 1.0f, -3.0f}, 1.0f, WHITE);
        EndShadowMode();
    }

    BeginMode3D(cam);
        DrawModel(floor, {0, 0, 0}, 1.0f, WHITE);
        DrawModel(box, {-2.0f, 1.0f, 0.0f}, 1.0f, {220, 180, 60, 255});
        DrawModel(box, {2.5f, 1.0f, -3.0f}, 1.0f, {80, 160, 220, 255});
        // Morph mesh pulsing over time.
        float w = 0.5f + 0.5f * std::sin(t * 1.5f);
        SetMeshMorphWeights(&morphMesh, &w, 1);
        DrawMesh(morphMesh, morphMat, MatrixTranslate(0, 1.2f, 3.0f));
        // Transparent panels (BLEND) are deferred and back-to-front sorted by
        // the renderer, flushed at EndMode3D. Because BLEND draws are deferred,
        // the mesh/material must stay alive until EndMode3D returns -- so these
        // panels are created once (function-static) rather than per-frame.
        static Mesh panel = GenMeshPlane(3, 3, 1, 1);
        static Material greenGlass = []{ Material m = LoadMaterialDefault(); m.lighting=false; m.alphaMode=MaterialAlphaMode::Blend; m.maps[0].color={80,220,120,120}; return m; }();
        static Material pinkGlass  = []{ Material m = LoadMaterialDefault(); m.lighting=false; m.alphaMode=MaterialAlphaMode::Blend; m.maps[0].color={220,80,160,120}; return m; }();
        DrawMesh(panel, greenGlass, MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixTranslate(0, 1.5f, 1.0f)));
        DrawMesh(panel, pinkGlass,  MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixTranslate(0.6f, 1.5f, 2.0f)));
    EndMode3D();
    ClearShadowMap();
    ClearEnvironmentLight();

    DrawRectangle(10, 65, 900, 72, ColorAlpha(BLACK, 0.65f));
    DrawText("Scene 6: cascaded shadows + IBL + morph + transparent sort", 20, 75, 16, WHITE);
    DrawText(cascades ? "Cascaded shadows: ON" :
             IsShadowMappingSupported() ? "Single-map shadows (cascades N/A on this backend)"
                                        : "Shadows unsupported on this backend", 20, 105, 16, LIME);
}

// Compose a compact "compiled: X available: Y" string for the HUD.
std::string BackendInventory() {
    auto tag = [](Backend b) { return GetBackendEnumName(b); };
    std::string compiled, available;
    for (Backend b : {Backend::Metal, Backend::Vulkan, Backend::OpenGL}) {
        if (IsBackendCompiled(b)) { if (!compiled.empty()) compiled += ","; compiled += tag(b); }
        if (IsBackendAvailable(b)) { if (!available.empty()) available += ","; available += tag(b); }
    }
    if (compiled.empty()) compiled = "none";
    if (available.empty()) available = "none";
    return "compiled: " + compiled + "   available: " + available;
}

} // namespace

int main(int argc, char** argv) {
    // Optional CLI backend override: `showcase --backend metal|vulkan|opengl|auto`.
    // The library provides the string->Backend parse; the sample owns argv.
    Backend requested = Backend::Automatic;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            Backend parsed;
            if (ParseBackend(argv[++i], parsed)) requested = parsed;
            else std::fprintf(stderr, "showcase: unknown --backend '%s' (use metal/vulkan/opengl/auto)\n", argv[i]);
        }
    }
    if (requested != Backend::Automatic) SetPreferredBackend(requested);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(kScreenW, kScreenH, "meowyrender showcase");
    SetTargetFPS(60);
    // Record whether a fallback occurred (requested != active) for the HUD.
    const bool fellBack = requested != Backend::Automatic && GetActiveBackend() != requested;
    if(!std::getenv("MEOWY_SHOWCASE_NO_AUDIO")) InitAudioDevice();

    // Assets shared across scenes.
    Image gradImg = GenImageGradientV(160, 160, SKYBLUE, DARKBLUE);
    Texture2D gradTex = LoadTextureFromImage(gradImg);
    UnloadImage(gradImg);
    GenTextureMipmaps(&gradTex);
    SetTextureFilter(gradTex, TextureFilter::Trilinear);
    SetTextureWrap(gradTex, TextureWrap::MirrorRepeat);

    Font ttf = GetFontDefault(); // LoadFontEx("font.ttf", 32, ...) for a real TTF

    Model cube = LoadModelFromMesh(GenMeshCube(2, 2, 2));
    Model sphere = LoadModelFromMesh(GenMeshSphere(1.2f, 16, 16));
    {
        cube.materials[0].lighting=true;
        sphere.materials[0].lighting=true;
        sphere.materials[0].maps[static_cast<int>(MaterialMapIndex::Metalness)].value=0.65f;
        sphere.materials[0].maps[static_cast<int>(MaterialMapIndex::Roughness)].value=0.25f;
    }

    // Scene 6 assets: a lit floor, a lit box (for shadow casters), and a morph
    // mesh (a cube with one blend-shape target that stretches it upward).
    Model floorModel = LoadModelFromMesh(GenMeshPlane(24, 24, 1, 1));
    floorModel.materials[0].lighting = true;
    floorModel.materials[0].maps[static_cast<int>(MaterialMapIndex::Roughness)].value = 1.0f;
    Model boxModel = LoadModelFromMesh(GenMeshCube(1.6f, 2.0f, 1.6f));
    boxModel.materials[0].lighting = true;
    Mesh morphMesh = GenMeshSphere(0.8f, 12, 12);
    {
        // Build one morph target that displaces every vertex outward along +Y.
        morphMesh.morphTargetCount = 1;
        morphMesh.morphPositions = static_cast<float*>(std::calloc(static_cast<std::size_t>(morphMesh.vertexCount) * 3, sizeof(float)));
        morphMesh.morphNormals = static_cast<float*>(std::calloc(static_cast<std::size_t>(morphMesh.vertexCount) * 3, sizeof(float)));
        morphMesh.morphWeights = static_cast<float*>(std::calloc(1, sizeof(float)));
        for (int i = 0; i < morphMesh.vertexCount; ++i) morphMesh.morphPositions[i * 3 + 1] = 1.2f; // +Y stretch
    }
    Material morphMat = LoadMaterialDefault();
    morphMat.lighting = true;
    morphMat.maps[static_cast<int>(MaterialMapIndex::Albedo)].color = {200, 120, 220, 255};
    morphMat.maps[static_cast<int>(MaterialMapIndex::Metalness)].value = 0.3f;
    morphMat.maps[static_cast<int>(MaterialMapIndex::Roughness)].value = 0.4f;

    RenderTexture2D target = LoadRenderTexture(240, 220);
    Shader wave = LoadShaderFromMemory(WaveVS(), WaveFS());
    Image faces=GenImageColor(96,16,SKYBLUE);
    const Color skyColors[]={SKYBLUE,BLUE,{80,140,210,255},DARKBLUE,{110,165,220,255},{70,125,190,255}};
    for(int face=0;face<6;++face)ImageDrawRectangle(&faces,face*16,0,16,16,skyColors[face]);
    auto sky=LoadTextureCubemap(faces,CubemapLayout::LineHorizontal);UnloadImage(faces);
    Model animated=LoadModel(MEOWY_SAMPLE_ASSETS "/skinned_triangle.gltf");
    int animationCount=0;auto* animations=LoadModelAnimations(MEOWY_SAMPLE_ASSETS "/skinned_triangle.gltf",&animationCount);
    auto stereo=LoadVrStereoConfig({});bool useStereo=false;

    Camera3D cam{};
    cam.position = {6, 5, 6};
    cam.target = {0, 1, 0};
    cam.up = {0, 1, 0};
    cam.fovy = 45.0f;
    cam.projection = CameraProjection::Perspective;

    int scene = 0;
    if (const char* selected = std::getenv("MEOWY_SHOWCASE_SCENE")) scene = std::clamp(std::atoi(selected), 0, kSceneCount-1);
    int frameLimit = 0;
    if (const char* limit = std::getenv("MEOWY_SHOWCASE_FRAMES")) frameLimit = std::atoi(limit);
    int frames = 0;

    while (!WindowShouldClose()) {
        const float t = static_cast<float>(GetTime());

        // Scene switching.
        if (IsKeyPressed(KeyboardKey::Right)) scene = (scene + 1) % kSceneCount;
        if (IsKeyPressed(KeyboardKey::Left)) scene = (scene + kSceneCount - 1) % kSceneCount;
        if (IsKeyPressed(KeyboardKey::One)) scene = 0;
        if (IsKeyPressed(KeyboardKey::Two)) scene = 1;
        if (IsKeyPressed(KeyboardKey::Three)) scene = 2;
        if (IsKeyPressed(KeyboardKey::Four)) scene = 3;
        if (IsKeyPressed(KeyboardKey::Five)) scene = 4;
        if (IsKeyPressed(KeyboardKey::Six)) scene = 5;
        if (IsKeyPressed(KeyboardKey::S)) useStereo=!useStereo;

        BeginDrawing();
        ClearBackground(RAYWHITE);

        switch (scene) {
            case 0: SceneShapes(t); break;
            case 1: SceneTexturesText(gradTex, ttf, t); break;
            case 2: Scene3D(cam, cube, sphere, t); break;
            case 3: SceneShaders(target, wave, t); break;
            case 4: {
                if(animationCount)UpdateModelAnimation(animated,animations[0],static_cast<int>(t*60));
                BeginMode3D({{5,4,7},{0,1,0},{0,1,0},45,CameraProjection::Perspective});
                if(useStereo)BeginVrStereoMode(stereo);
                DrawSkybox(sky);DrawGrid(20,1);
                DrawModel(animated,{-1,0,0},0.035f,GREEN);
                Matrix instances[36];
                for(int i=0;i<36;++i)instances[i]=MatrixMultiply(MatrixScale(0.15f,0.15f,0.15f),MatrixTranslate((i%6-2.5f)*0.7f,0.3f,(i/6)*0.7f-4));
                auto material=cube.materials[0];material.lighting=false;material.maps[0].color=ORANGE;
                DrawMeshInstanced(cube.meshes[0],material,instances,36);
                if(useStereo)EndVrStereoMode();EndMode3D();
                DrawRectangle(10,65,800,72,ColorAlpha(BLACK,0.65f));
                DrawText("Scene 5: cubemap / animated glTF / 36 GPU instances",20,75,16,WHITE);
                DrawText(useStereo?"S: stereo ON (untracked simulation)":"S: enable side-by-side stereo simulation",20,105,16,LIME);
                break;
            }
            case 5: SceneAdvanced(sky, floorModel, boxModel, morphMesh, morphMat, t); break;
            default: break;
        }

        // Persistent HUD.
        DrawRectangle(0, 0, kScreenW, 58, ColorAlpha(BLACK, 0.75f));
        DrawText("meowyrender", 20, 10, 18, RAYWHITE);
        // Active backend (+ fallback note), then the compiled/available inventory.
        std::string active = std::string("Active: ") + GetActiveBackendName();
        if (fellBack) active += " (fell back from " + std::string(GetBackendEnumName(requested)) + ")";
        DrawText(active, 220, 8, 14, fellBack ? GOLD : LIME);
        DrawText(BackendInventory(), 220, 26, 11, LIGHTGRAY);
        DrawText("Left/Right or 1-5: switch scene   (run with --backend metal|vulkan|opengl)", 20, 42, 11, LIGHTGRAY);
        DrawFPS(kScreenW - 150, 12);

        if (frameLimit > 0 && frames + 1 >= frameLimit) TakeScreenshot("showcase-capture.png");
        EndDrawing();
        if (frameLimit > 0 && ++frames >= frameLimit) break;
    }

    UnloadTexture(sky);UnloadModelAnimations(animations,animationCount);UnloadModel(animated);UnloadVrStereoConfig(stereo);
    UnloadShader(wave);
    UnloadRenderTexture(target);
    UnloadModel(cube);
    UnloadModel(sphere);
    UnloadModel(floorModel);
    UnloadModel(boxModel);
    UnloadMesh(morphMesh);
    UnloadMaterial(morphMat);
    UnloadTexture(gradTex);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
