// meowyrender - mr_types.hpp
// Core data types mirroring raylib's structs, in the meowyrender namespace.
#pragma once

#include <cstdint>
#include "meowyrender/mr_math.hpp"

namespace meowyrender {

// ---------------------------------------------------------------------------
// Color
// ---------------------------------------------------------------------------
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

// Standard raylib color palette (same names, meowyrender namespace).
inline constexpr Color LIGHTGRAY = {200, 200, 200, 255};
inline constexpr Color GRAY      = {130, 130, 130, 255};
inline constexpr Color DARKGRAY  = {80, 80, 80, 255};
inline constexpr Color YELLOW    = {253, 249, 0, 255};
inline constexpr Color GOLD      = {255, 203, 0, 255};
inline constexpr Color ORANGE    = {255, 161, 0, 255};
inline constexpr Color PINK      = {255, 109, 194, 255};
inline constexpr Color RED       = {230, 41, 55, 255};
inline constexpr Color MAROON    = {190, 33, 55, 255};
inline constexpr Color GREEN     = {0, 228, 48, 255};
inline constexpr Color LIME      = {0, 158, 47, 255};
inline constexpr Color DARKGREEN = {0, 117, 44, 255};
inline constexpr Color SKYBLUE   = {102, 191, 255, 255};
inline constexpr Color BLUE      = {0, 121, 241, 255};
inline constexpr Color DARKBLUE  = {0, 82, 172, 255};
inline constexpr Color PURPLE    = {200, 122, 255, 255};
inline constexpr Color VIOLET    = {135, 60, 190, 255};
inline constexpr Color DARKPURPLE= {112, 31, 126, 255};
inline constexpr Color BEIGE     = {211, 176, 131, 255};
inline constexpr Color BROWN     = {127, 106, 79, 255};
inline constexpr Color DARKBROWN = {76, 63, 47, 255};
inline constexpr Color WHITE     = {255, 255, 255, 255};
inline constexpr Color BLACK     = {0, 0, 0, 255};
inline constexpr Color BLANK     = {0, 0, 0, 0};
inline constexpr Color MAGENTA   = {255, 0, 255, 255};
inline constexpr Color RAYWHITE  = {245, 245, 245, 255};

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
struct Rectangle {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// ---------------------------------------------------------------------------
// Pixel formats (full raylib set)
// ---------------------------------------------------------------------------
enum class PixelFormat : int {
    Uncompressed_Grayscale = 1,   // 8 bit per pixel (no alpha)
    Uncompressed_GrayAlpha,       // 8*2 bpp (2 channels)
    Uncompressed_R5G6B5,          // 16 bpp
    Uncompressed_R8G8B8,          // 24 bpp
    Uncompressed_R5G5B5A1,        // 16 bpp (1 bit alpha)
    Uncompressed_R4G4B4A4,        // 16 bpp (4 bit alpha)
    Uncompressed_R8G8B8A8,        // 32 bpp
    Uncompressed_R32,             // 32 bpp (1 channel - float)
    Uncompressed_R32G32B32,       // 32*3 bpp (3 channels - float)
    Uncompressed_R32G32B32A32,    // 32*4 bpp (4 channels - float)
    Uncompressed_R16,             // 16 bpp (1 channel - half float)
    Uncompressed_R16G16B16,       // 16*3 bpp (3 channels - half float)
    Uncompressed_R16G16B16A16,    // 16*4 bpp (4 channels - half float)
    Compressed_DXT1_RGB,
    Compressed_DXT1_RGBA,
    Compressed_DXT3_RGBA,
    Compressed_DXT5_RGBA,
    Compressed_ETC1_RGB,
    Compressed_ETC2_RGB,
    Compressed_ETC2_EAC_RGBA,
    Compressed_PVRT_RGB,
    Compressed_PVRT_RGBA,
    Compressed_ASTC_4x4_RGBA,
    Compressed_ASTC_8x8_RGBA,
};

// Texture sampling filters.
enum class TextureFilter : int {
    Point = 0,        // nearest
    Bilinear,         // linear
    Trilinear,        // linear with mipmaps
    Anisotropic4x,
    Anisotropic8x,
    Anisotropic16x,
};

// Texture wrap modes.
enum class TextureWrap : int {
    Repeat = 0,
    Clamp,
    MirrorRepeat,
    MirrorClamp,
};

// Face order for line layouts: +X, -X, +Y, -Y, +Z, -Z.
enum class CubemapLayout : int {
    AutoDetect = 0,
    LineVertical,
    LineHorizontal,
    CrossThreeByFour,
    CrossFourByThree,
    Panorama,
};

// ---------------------------------------------------------------------------
// Image (CPU-side pixel data)
// ---------------------------------------------------------------------------
struct Image {
    void* data = nullptr;
    int width = 0;
    int height = 0;
    int mipmaps = 1;
    PixelFormat format = PixelFormat::Uncompressed_R8G8B8A8;
};

// ---------------------------------------------------------------------------
// Texture (GPU-side handle). id is backend-defined (GL name, Metal index...).
// ---------------------------------------------------------------------------
struct Texture2D {
    unsigned int id = 0;
    int width = 0;
    int height = 0;
    int mipmaps = 1;
    PixelFormat format = PixelFormat::Uncompressed_R8G8B8A8;
};
using Texture = Texture2D;
using TextureCubemap = Texture2D;

struct RenderTexture2D {
    unsigned int id = 0;
    Texture2D texture;
    Texture2D depth;
};
using RenderTexture = RenderTexture2D;

// ---------------------------------------------------------------------------
// Font glyph data
// ---------------------------------------------------------------------------
struct GlyphInfo {
    int value = 0;       // codepoint
    int offsetX = 0;
    int offsetY = 0;
    int advanceX = 0;
    Image image;
};

struct Font {
    int baseSize = 0;
    int glyphCount = 0;
    int glyphPadding = 0;
    Texture2D texture;      // atlas
    Rectangle* recs = nullptr;   // glyph rects in atlas
    GlyphInfo* glyphs = nullptr;
};

// ---------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------
struct Camera2D {
    Vector2 offset;
    Vector2 target;
    float rotation = 0.0f;
    float zoom = 1.0f;
};

enum class CameraProjection : int { Perspective = 0, Orthographic };

struct VrDeviceInfo {
    int hResolution=2160,vResolution=1200;
    float hScreenSize=0.133f,vScreenSize=0.074f;
    float eyeToScreenDistance=0.041f,lensSeparationDistance=0.064f,interpupillaryDistance=0.064f;
    float lensDistortionValues[4]{1,0,0,0};
    float chromaAbCorrection[4]{1,0,1,0};
};
// Side-by-side stereo simulator configuration, not a headset session.
struct VrStereoConfig {
    Matrix projection[2]{},viewOffset[2]{};
    float leftLensCenter[2]{},rightLensCenter[2]{};
    float leftScreenCenter[2]{0.25f,0.5f},rightScreenCenter[2]{0.75f,0.5f};
    float scale[2]{},scaleIn[2]{};
};

struct Camera3D {
    Vector3 position;
    Vector3 target;
    Vector3 up;
    float fovy = 45.0f;
    CameraProjection projection = CameraProjection::Perspective;
};
using Camera = Camera3D;

// ---------------------------------------------------------------------------
// File path list (raylib parity, e.g. LoadDirectoryFiles / dropped files)
// ---------------------------------------------------------------------------
struct FilePathList {
    unsigned int capacity = 0;  // filepaths max entries
    unsigned int count = 0;     // filepaths entries count
    char** paths = nullptr;     // filepaths entries (heap; free with UnloadDirectoryFiles)
};

// Automation events (input recording/playback).
struct AutomationEvent {
    unsigned int frame = 0;   // event frame
    unsigned int type = 0;    // event type (INPUT_KEY_UP, etc.)
    int params[4] = {0, 0, 0, 0};
};
struct AutomationEventList {
    unsigned int capacity = 0;
    unsigned int count = 0;
    AutomationEvent* events = nullptr;
};

// N-patch scaling layout (raylib parity).
enum class NPatchLayout : int { NinePatch = 0, ThreePatchVertical, ThreePatchHorizontal };
struct NPatchInfo {
    Rectangle source{};   // texture source rectangle
    int left = 0;         // left border offset
    int top = 0;          // top border offset
    int right = 0;        // right border offset
    int bottom = 0;       // bottom border offset
    int layout = 0;       // layout (see NPatchLayout)
};

// ---------------------------------------------------------------------------
// 3D geometry helpers
// ---------------------------------------------------------------------------
struct Ray {
    Vector3 position;   // origin
    Vector3 direction;  // normalized
};

struct RayCollision {
    bool hit = false;
    float distance = 0.0f;
    Vector3 point;
    Vector3 normal;
};

struct BoundingBox {
    Vector3 min;
    Vector3 max;
};

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------
struct Shader {
    unsigned int id = 0;
    int* locs = nullptr;
};

// ---------------------------------------------------------------------------
// Mesh / Material / Model (3D)
// ---------------------------------------------------------------------------
// CPU-side mesh data. Arrays are owned by the mesh unless it was created as a
// view. vertexCount counts vertices; triangleCount counts faces.
struct Mesh {
    int vertexCount = 0;
    int triangleCount = 0;

    float* vertices = nullptr;   // 3 floats per vertex (x,y,z)
    float* texcoords = nullptr;  // 2 floats per vertex (u,v) = TEXCOORD_0
    float* texcoords2 = nullptr; // 2 floats per vertex = TEXCOORD_1 (optional)
    float* normals = nullptr;    // 3 floats per vertex
    float* tangents = nullptr;   // 4 floats per vertex (xyz + w handedness, optional)
    unsigned char* colors = nullptr; // 4 bytes per vertex (optional)
    unsigned short* indices = nullptr; // optional index buffer
    unsigned short* boneIds = nullptr; // four influences per vertex
    float* boneWeights = nullptr;      // four normalized weights per vertex
    int boneCount = 0;
    int* boneNodes = nullptr;          // indices into Model::bones
    Matrix* inverseBindMatrices = nullptr;
    Matrix* boneMatrices = nullptr;    // current model-space skin palette

    // glTF morph targets (blend shapes). morphTargetCount target sets, each with
    // vertexCount*3 position deltas (and optional normal deltas). morphWeights
    // holds the current per-target weight (default pose weights on load; updated
    // by SetMeshMorphWeights or morph-weight animation). Deltas are applied on
    // the CPU during tessellation: p' = p + sum(weight_i * delta_i).
    int morphTargetCount = 0;
    float* morphPositions = nullptr;  // [target][vertex*3], target-major
    float* morphNormals = nullptr;    // [target][vertex*3], target-major (may be null)
    float* morphWeights = nullptr;    // [morphTargetCount]

    // Backend upload handle (0 = not uploaded; drawn immediate otherwise).
    unsigned int vaoId = 0;
};

// Material map slot indices (matches raylib's MATERIAL_MAP_*).
enum class MaterialMapIndex : int {
    Albedo = 0, Metalness, Normal, Roughness, Occlusion,
    Emission, Height, Cubemap, Irradiance, Prefilter, Brdf,
};
inline constexpr int MaxMaterialMaps = 12;

struct MaterialMap {
    Texture2D texture;
    Color color = {255, 255, 255, 255};
    float value = 0.0f;
    // KHR_texture_transform (parsed from glTF; applied to sampled UVs). Offset
    // and scale operate in UV space; rotation is radians about the origin.
    Vector2 uvOffset = {0, 0};
    Vector2 uvScale = {1, 1};
    float uvRotation = 0.0f;
    // Which vertex UV set feeds this map: 0 = TEXCOORD_0, 1 = TEXCOORD_1.
    int uvSet = 0;
};

// glTF alpha handling. Opaque ignores the albedo alpha channel; Mask discards
// fragments below alphaCutoff; Blend uses standard alpha blending.
enum class MaterialAlphaMode : int { Opaque = 0, Mask, Blend };

struct Material {
    Shader shader;
    MaterialMap* maps = nullptr;   // array of MaxMaterialMaps
    float params[4] = {0, 0, 0, 0};
    bool lighting = false; // opt in to built-in metallic/roughness PBR
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;      // used when alphaMode == Mask
};

struct BoneInfo { char name[64] = {}; int parent = -1; };
struct Transform { Vector3 translation{}; Quaternion rotation{0,0,0,1}; Vector3 scale{1,1,1}; };

struct Model {
    Matrix transform;              // local transform
    int meshCount = 0;
    int materialCount = 0;
    Mesh* meshes = nullptr;
    Material* materials = nullptr;
    int* meshMaterial = nullptr;   // material index per mesh

    // Animation (optional)
    int boneCount = 0;
    BoneInfo* bones = nullptr;
    Transform* bindPose = nullptr;
    Matrix* bindMatrices = nullptr; // exact local matrices, including static matrix nodes
    Texture2D* ownedTextures = nullptr; // imported textures; material maps may share them
    int ownedTextureCount = 0;
};

struct ModelAnimation {
    int boneCount = 0;
    int frameCount = 0;
    BoneInfo* bones = nullptr;
    Transform** framePoses = nullptr;
    Matrix** frameMatrices = nullptr;
    // Morph-target weight animation (optional). morphTargetCount weights per
    // frame; UpdateModelAnimation writes these into the model's mesh weights.
    int morphTargetCount = 0;
    float** frameWeights = nullptr;   // [frame][morphTargetCount], or null
    float frameRate = 60.0f;
    float duration = 0.0f;
    char name[32] = {};
};

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
// Raw audio sample buffer loaded fully into memory.
struct Wave {
    unsigned int frameCount = 0;
    unsigned int sampleRate = 0;
    unsigned int sampleSize = 0;   // bits per sample (8/16/32)
    unsigned int channels = 0;
    void* data = nullptr;
};

// A playable, fully-decoded sound. The opaque handle points to backend voice
// state managed by the audio module.
struct Sound {
    unsigned int frameCount = 0;
    void* stream = nullptr;        // internal ma_sound* (owned)
};

// Streamed music (decoded on the fly). Handle is an internal ma_sound* set up
// for streaming from disk.
struct Music {
    unsigned int frameCount = 0;
    bool looping = true;
    void* stream = nullptr;        // internal ma_sound* (owned)
};

// A user-provided audio stream for procedural / custom audio.
struct AudioStream {
    void* buffer = nullptr;
    unsigned int sampleRate = 0;
    unsigned int sampleSize = 0;
    unsigned int channels = 0;
};

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------
[[nodiscard]] inline Color ColorAlpha(Color c, float alpha) {
    c.a = static_cast<std::uint8_t>(Clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return c;
}

[[nodiscard]] inline Color Fade(Color c, float alpha) { return ColorAlpha(c, alpha); }

[[nodiscard]] inline Vector4 ColorNormalize(Color c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

[[nodiscard]] inline Color ColorFromNormalized(Vector4 v) {
    return {static_cast<std::uint8_t>(Clamp(v.x, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(Clamp(v.y, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(Clamp(v.z, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(Clamp(v.w, 0.0f, 1.0f) * 255.0f)};
}

[[nodiscard]] inline Color ColorLerp(Color a, Color b, float t) {
    return {static_cast<std::uint8_t>(Lerp(a.r, b.r, t)),
            static_cast<std::uint8_t>(Lerp(a.g, b.g, t)),
            static_cast<std::uint8_t>(Lerp(a.b, b.b, t)),
            static_cast<std::uint8_t>(Lerp(a.a, b.a, t))};
}

// Packed 0xRRGGBBAA <-> Color (raylib ColorToInt / GetColor).
[[nodiscard]] inline int ColorToInt(Color c) {
    return (static_cast<int>(c.r) << 24) | (static_cast<int>(c.g) << 16) |
           (static_cast<int>(c.b) << 8) | static_cast<int>(c.a);
}
[[nodiscard]] inline Color GetColor(unsigned int hexValue) {
    return {static_cast<std::uint8_t>((hexValue >> 24) & 0xFF),
            static_cast<std::uint8_t>((hexValue >> 16) & 0xFF),
            static_cast<std::uint8_t>((hexValue >> 8) & 0xFF),
            static_cast<std::uint8_t>(hexValue & 0xFF)};
}
[[nodiscard]] inline bool ColorIsEqual(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
// Multiply-tint (raylib ColorTint): componentwise a*b/255.
[[nodiscard]] inline Color ColorTint(Color c, Color tint) {
    return {static_cast<std::uint8_t>(c.r * tint.r / 255),
            static_cast<std::uint8_t>(c.g * tint.g / 255),
            static_cast<std::uint8_t>(c.b * tint.b / 255),
            static_cast<std::uint8_t>(c.a * tint.a / 255)};
}
// Brightness in [-1,1]; contrast in [-1,1] (raylib semantics).
[[nodiscard]] inline Color ColorBrightness(Color c, float factor) {
    factor = Clamp(factor, -1.0f, 1.0f);
    auto adj = [&](std::uint8_t v) {
        float f = v;
        if (factor < 0.0f) f *= (1.0f + factor);
        else f += (255.0f - f) * factor;
        return static_cast<std::uint8_t>(Clamp(f, 0.0f, 255.0f));
    };
    return {adj(c.r), adj(c.g), adj(c.b), c.a};
}
[[nodiscard]] inline Color ColorContrast(Color c, float contrast) {
    contrast = Clamp(contrast, -1.0f, 1.0f) * 255.0f;
    const float f = (259.0f * (contrast + 255.0f)) / (255.0f * (259.0f - contrast));
    auto adj = [&](std::uint8_t v) {
        float nv = f * (static_cast<float>(v) - 128.0f) + 128.0f;
        return static_cast<std::uint8_t>(Clamp(nv, 0.0f, 255.0f));
    };
    return {adj(c.r), adj(c.g), adj(c.b), c.a};
}
// Alpha-over blend of `src` onto `dst`, modulated by `tint`.
[[nodiscard]] inline Color ColorAlphaBlend(Color dst, Color src, Color tint) {
    Vector4 fdst = ColorNormalize(dst), fsrc = ColorNormalize(src), ftint = ColorNormalize(tint);
    fsrc.x *= ftint.x; fsrc.y *= ftint.y; fsrc.z *= ftint.z; fsrc.w *= ftint.w;
    if (fsrc.w <= 0.0f) return dst;
    if (fsrc.w >= 1.0f) return ColorFromNormalized(fsrc);
    Vector4 out;
    out.w = fsrc.w + fdst.w * (1.0f - fsrc.w);
    const float inv = out.w > 0.0f ? 1.0f / out.w : 0.0f;
    out.x = (fsrc.x * fsrc.w + fdst.x * fdst.w * (1.0f - fsrc.w)) * inv;
    out.y = (fsrc.y * fsrc.w + fdst.y * fdst.w * (1.0f - fsrc.w)) * inv;
    out.z = (fsrc.z * fsrc.w + fdst.z * fdst.w * (1.0f - fsrc.w)) * inv;
    return ColorFromNormalized(out);
}
// HSV: hue in [0,360], sat/val in [0,1].
[[nodiscard]] inline Vector3 ColorToHSV(Color c) {
    const float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    const float mx = (r > g ? (r > b ? r : b) : (g > b ? g : b));
    const float mn = (r < g ? (r < b ? r : b) : (g < b ? g : b));
    const float delta = mx - mn;
    Vector3 hsv{0.0f, 0.0f, mx};
    if (mx > 0.0f) hsv.y = delta / mx;
    if (delta > 0.0f) {
        if (mx == r) hsv.x = 60.0f * (((g - b) / delta) - std::floor((g - b) / delta / 6.0f) * 6.0f);
        else if (mx == g) hsv.x = 60.0f * (((b - r) / delta) + 2.0f);
        else hsv.x = 60.0f * (((r - g) / delta) + 4.0f);
        if (hsv.x < 0.0f) hsv.x += 360.0f;
    }
    return hsv;
}
[[nodiscard]] inline Color ColorFromHSV(float hue, float saturation, float value) {
    const float c = value * saturation;
    const float x = c * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float m = value - c;
    float r = 0, g = 0, b = 0;
    if (hue < 60)      { r = c; g = x; }
    else if (hue < 120){ r = x; g = c; }
    else if (hue < 180){ g = c; b = x; }
    else if (hue < 240){ g = x; b = c; }
    else if (hue < 300){ r = x; b = c; }
    else               { r = c; b = x; }
    return {static_cast<std::uint8_t>((r + m) * 255.0f),
            static_cast<std::uint8_t>((g + m) * 255.0f),
            static_cast<std::uint8_t>((b + m) * 255.0f), 255};
}

} // namespace meowyrender
