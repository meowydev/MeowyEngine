// meowyrender - src/modules/ibl_precompute.cpp
// Precomputed split-sum image-based lighting: cosine-convolved diffuse
// irradiance cubemap, GGX roughness-prefiltered specular cubemap, and a BRDF
// integration LUT. All convolution runs on the CPU (deterministic and
// backend-agnostic) and the results upload through the existing texture/cubemap
// paths, so it works on any backend whose lit shader consumes them.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <array>
#include <cstdint>

namespace meowyrender {

namespace {
constexpr float kPI = 3.14159265358979323846f;

// Cube face direction for face `f` at face-space (u,v) in [-1,1].
Vector3 FaceDir(int f, float u, float v) {
    switch (f) {
        case 0: return { 1, -v, -u}; // +X
        case 1: return {-1, -v,  u}; // -X
        case 2: return { u,  1,  v}; // +Y
        case 3: return { u, -1, -v}; // -Y
        case 4: return { u, -v,  1}; // +Z
        default:return {-u, -v, -1}; // -Z
    }
}

// A CPU copy of a cubemap: 6 faces of `size`x`size` linear-space RGB.
struct CubeCPU {
    int size = 0;
    std::array<std::vector<Vector3>, 6> faces;
    Vector3 Sample(Vector3 dir) const {
        // Select the major axis -> face, then face-space UV.
        const float ax = std::fabs(dir.x), ay = std::fabs(dir.y), az = std::fabs(dir.z);
        int f; float u, v, ma;
        if (ax >= ay && ax >= az) { ma = ax; if (dir.x > 0) { f = 0; u = -dir.z; v = -dir.y; } else { f = 1; u = dir.z; v = -dir.y; } }
        else if (ay >= az)        { ma = ay; if (dir.y > 0) { f = 2; u = dir.x; v = dir.z; } else { f = 3; u = dir.x; v = -dir.z; } }
        else                      { ma = az; if (dir.z > 0) { f = 4; u = dir.x; v = -dir.y; } else { f = 5; u = -dir.x; v = -dir.y; } }
        float fu = (u / ma * 0.5f + 0.5f) * size - 0.5f;
        float fv = (v / ma * 0.5f + 0.5f) * size - 0.5f;
        int x = std::clamp((int)std::lround(fu), 0, size - 1);
        int y = std::clamp((int)std::lround(fv), 0, size - 1);
        return faces[f][(std::size_t)y * size + x];
    }
};

// Read a GPU cubemap back to a linear-space CPU copy.
CubeCPU ReadCube(TextureCubemap cube) {
    CubeCPU c;
    for (int f = 0; f < 6; ++f) {
        Image img = LoadImageFromCubemapFace(cube, f);
        if (!img.data) return {};
        c.size = img.width;
        c.faces[f].resize((std::size_t)img.width * img.height);
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x) {
                Color col = GetImageColor(img, x, y);
                // sRGB -> linear (matches the shader's pow(...,2.2) on sampling).
                c.faces[f][(std::size_t)y * img.width + x] =
                    { std::pow(col.r / 255.0f, 2.2f), std::pow(col.g / 255.0f, 2.2f), std::pow(col.b / 255.0f, 2.2f) };
            }
        UnloadImage(img);
    }
    return c;
}

unsigned char ToSrgbByte(float linear) {
    float s = std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f);
    return (unsigned char)std::lround(std::clamp(s, 0.0f, 1.0f) * 255.0f);
}

// Build a GPU cubemap from CPU face colors (linear -> sRGB8) via CreateCubemap.
TextureCubemap UploadCube(const std::array<std::vector<Vector3>, 6>& faces, int size, bool mips) {
    std::vector<unsigned char> rgba((std::size_t)6 * size * size * 4);
    for (int f = 0; f < 6; ++f)
        for (int i = 0; i < size * size; ++i) {
            const Vector3& c = faces[f][i];
            std::size_t o = ((std::size_t)f * size * size + i) * 4;
            rgba[o+0] = ToSrgbByte(c.x); rgba[o+1] = ToSrgbByte(c.y); rgba[o+2] = ToSrgbByte(c.z); rgba[o+3] = 255;
        }
    auto& s = detail::State();
    TextureCubemap cube{};
    cube.id = s.backend ? s.backend->CreateCubemap(rgba.data(), size) : 0;
    cube.width = cube.height = size;
    if (cube.id && mips && s.backend) { cube.mipmaps = s.backend->GenTextureMipmaps(cube.id); }
    else cube.mipmaps = 1;
    return cube;
}

// GGX importance-sample helpers for the specular prefilter and BRDF LUT.
Vector3 ImportanceGGX(float u1, float u2, Vector3 n, float rough) {
    float a = rough * rough;
    float phi = 2.0f * kPI * u1;
    float cosT = std::sqrt((1.0f - u2) / (1.0f + (a*a - 1.0f) * u2));
    float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT*cosT));
    Vector3 h{ std::cos(phi)*sinT, std::sin(phi)*sinT, cosT };
    Vector3 up = std::fabs(n.z) < 0.999f ? Vector3{0,0,1} : Vector3{1,0,0};
    Vector3 tx = Vector3Normalize(Vector3CrossProduct(up, n));
    Vector3 ty = Vector3CrossProduct(n, tx);
    return Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(tx, h.x), Vector3Scale(ty, h.y)), Vector3Scale(n, h.z)));
}
float RadicalInverse(unsigned int bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}
float GeometrySchlickIBL(float ndv, float rough) {
    float k = rough * rough / 2.0f;
    return ndv / (ndv * (1.0f - k) + k);
}
} // namespace

EnvironmentLight GenEnvironmentLightMaps(TextureCubemap cubemap, int irradianceSize, int prefilterSize, int brdfSize) {
    EnvironmentLight env{};
    env.source = cubemap;
    if (!cubemap.id) return env;
    detail::FlushBatch();
    CubeCPU src = ReadCube(cubemap);
    if (src.size == 0) return env;

    // --- 1. Diffuse irradiance: cosine-weighted hemisphere convolution. ---
    {
        std::array<std::vector<Vector3>, 6> out;
        for (int f = 0; f < 6; ++f) out[f].resize((std::size_t)irradianceSize * irradianceSize);
        const float step = 0.35f; // hemisphere sampling step (radians)
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < irradianceSize; ++y)
                for (int x = 0; x < irradianceSize; ++x) {
                    float u = (x + 0.5f) / irradianceSize * 2 - 1;
                    float v = (y + 0.5f) / irradianceSize * 2 - 1;
                    Vector3 n = Vector3Normalize(FaceDir(f, u, v));
                    Vector3 up = std::fabs(n.y) < 0.999f ? Vector3{0,1,0} : Vector3{1,0,0};
                    Vector3 tx = Vector3Normalize(Vector3CrossProduct(up, n));
                    Vector3 ty = Vector3CrossProduct(n, tx);
                    Vector3 sum{0,0,0}; float samples = 0;
                    for (float phi = 0; phi < 2*kPI; phi += step)
                        for (float theta = 0; theta < 0.5f*kPI; theta += step) {
                            Vector3 tangent{ std::sin(theta)*std::cos(phi), std::sin(theta)*std::sin(phi), std::cos(theta) };
                            Vector3 dir = Vector3Add(Vector3Add(Vector3Scale(tx, tangent.x), Vector3Scale(ty, tangent.y)), Vector3Scale(n, tangent.z));
                            Vector3 c = src.Sample(dir);
                            float w = std::cos(theta) * std::sin(theta); // cosine-weighted
                            sum = Vector3Add(sum, Vector3Scale(c, w));
                            samples += 1;
                        }
                    Vector3 irr = Vector3Scale(sum, kPI / std::max(1.0f, samples));
                    out[f][(std::size_t)y*irradianceSize + x] = irr;
                }
        env.irradiance = UploadCube(out, irradianceSize, false);
    }

    // --- 2. Prefiltered specular: GGX importance-sampled convolution at a
    // representative roughness; hardware mips extend the roughness range. ---
    {
        std::array<std::vector<Vector3>, 6> out;
        for (int f = 0; f < 6; ++f) out[f].resize((std::size_t)prefilterSize * prefilterSize);
        const float rough = 0.35f; const unsigned int N = 64;
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < prefilterSize; ++y)
                for (int x = 0; x < prefilterSize; ++x) {
                    float u = (x + 0.5f) / prefilterSize * 2 - 1;
                    float v = (y + 0.5f) / prefilterSize * 2 - 1;
                    Vector3 r = Vector3Normalize(FaceDir(f, u, v)); // N=V=R
                    Vector3 sum{0,0,0}; float wsum = 0;
                    for (unsigned int i = 0; i < N; ++i) {
                        Vector3 h = ImportanceGGX(RadicalInverse(i), (float)i / N, r, rough);
                        Vector3 l = Vector3Subtract(Vector3Scale(h, 2*Vector3DotProduct(r, h)), r);
                        float ndl = std::max(0.0f, Vector3DotProduct(r, l));
                        if (ndl > 0) { sum = Vector3Add(sum, Vector3Scale(src.Sample(l), ndl)); wsum += ndl; }
                    }
                    out[f][(std::size_t)y*prefilterSize + x] = wsum > 0 ? Vector3Scale(sum, 1.0f/wsum) : src.Sample(r);
                }
        env.prefilter = UploadCube(out, prefilterSize, true);
        env.prefilterMips = env.prefilter.mipmaps;
    }

    // --- 3. BRDF integration LUT (RG): the split-sum scale+bias terms. ---
    {
        std::vector<unsigned char> lut((std::size_t)brdfSize * brdfSize * 4);
        const unsigned int N = 128;
        for (int y = 0; y < brdfSize; ++y)
            for (int x = 0; x < brdfSize; ++x) {
                float ndv = (x + 0.5f) / brdfSize;
                float rough = (y + 0.5f) / brdfSize;
                Vector3 view{ std::sqrt(std::max(0.0f,1.0f-ndv*ndv)), 0, ndv };
                float a = 0, b = 0;
                Vector3 n{0,0,1};
                for (unsigned int i = 0; i < N; ++i) {
                    Vector3 h = ImportanceGGX(RadicalInverse(i), (float)i / N, n, rough);
                    Vector3 l = Vector3Subtract(Vector3Scale(h, 2*Vector3DotProduct(view, h)), view);
                    float ndl = std::max(0.0f, l.z), ndh = std::max(0.0f, h.z), vdh = std::max(0.0f, Vector3DotProduct(view, h));
                    if (ndl > 0) {
                        float g = GeometrySchlickIBL(std::max(0.0f,n.z*view.z>0?ndv:ndv), rough) * GeometrySchlickIBL(ndl, rough);
                        float gvis = (g * vdh) / std::max(1e-4f, ndh * ndv);
                        float fc = std::pow(1.0f - vdh, 5.0f);
                        a += (1.0f - fc) * gvis; b += fc * gvis;
                    }
                }
                a /= N; b /= N;
                std::size_t o = ((std::size_t)y * brdfSize + x) * 4;
                lut[o+0] = (unsigned char)std::lround(std::clamp(a,0.0f,1.0f)*255.0f);
                lut[o+1] = (unsigned char)std::lround(std::clamp(b,0.0f,1.0f)*255.0f);
                lut[o+2] = 0; lut[o+3] = 255;
            }
        auto& s = detail::State();
        env.brdfLut.id = s.backend ? s.backend->CreateTexture(lut.data(), brdfSize, brdfSize, PixelFormat::Uncompressed_R8G8B8A8) : 0;
        env.brdfLut.width = env.brdfLut.height = brdfSize;
        if (env.brdfLut.id && s.backend) s.backend->SetTextureFilter(env.brdfLut.id, 1); // bilinear
    }
    return env;
}

void SetEnvironmentLightPrecomputed(EnvironmentLight env, float intensity) {
    detail::FlushBatch();
    auto& light = detail::State().lighting;
    light.environment = env.source.id;
    light.environmentIntensity = std::max(0.0f, intensity);
    light.environmentMips = std::max(1, env.source.mipmaps);
    light.irradiance = env.irradiance.id;
    light.prefilter = env.prefilter.id;
    light.prefilterMips = std::max(1, env.prefilterMips);
    light.brdfLut = env.brdfLut.id;
}

void UnloadEnvironmentLight(EnvironmentLight env) {
    if (env.irradiance.id) UnloadTexture(env.irradiance);
    if (env.prefilter.id) UnloadTexture(env.prefilter);
    if (env.brdfLut.id) UnloadTexture(env.brdfLut);
}

} // namespace meowyrender