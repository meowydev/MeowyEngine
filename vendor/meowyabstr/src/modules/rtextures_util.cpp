// meowyrender - src/modules/rtextures_util.cpp
// rtextures parity: pixel data helpers, image validity, raw/anim loading,
// color arrays/palettes, extended generation (perlin/cellular/linear/square/
// text), CPU image drawing (lines/circles/rects/triangles/blit/text), image
// transforms (rotate/mipmaps/POT/alpha ops/blur/convolution), and texture
// sub-updates + NPatch drawing. Built on the shared pixel helpers so it works
// across all backends (CPU-side) without a live GPU context.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"
#include "modules/image_pixels.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

namespace meowyrender {

using detail::ImageBytes;
using detail::PixelBytes;
using detail::ReadPixel;
using detail::WritePixel;

namespace {
// Ensure an image is RGBA8 for CPU drawing; returns true if usable.
bool EnsureRGBA(Image* image) {
    if (!image || !image->data) return false;
    if (image->format != PixelFormat::Uncompressed_R8G8B8A8)
        ImageFormat(image, PixelFormat::Uncompressed_R8G8B8A8);
    return image->data != nullptr;
}
inline void PutRGBA(Image* img, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return;
    auto* p = static_cast<unsigned char*>(img->data) + (static_cast<std::size_t>(y) * img->width + x) * 4;
    // Straight alpha over-blend so drawing respects source alpha (raylib).
    if (c.a == 255) { p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255; return; }
    if (c.a == 0) return;
    const float a = c.a / 255.0f, ia = 1.0f - a;
    p[0] = static_cast<unsigned char>(c.r * a + p[0] * ia);
    p[1] = static_cast<unsigned char>(c.g * a + p[1] * ia);
    p[2] = static_cast<unsigned char>(c.b * a + p[2] * ia);
    p[3] = static_cast<unsigned char>(c.a + p[3] * ia);
}
inline Color GetRGBA(const Image* img, int x, int y) {
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) return BLANK;
    auto* p = static_cast<const unsigned char*>(img->data) + (static_cast<std::size_t>(y) * img->width + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}
} // namespace

// ===========================================================================
// Pixel data
// ===========================================================================
int GetPixelDataSize(int width, int height, int format) {
    return static_cast<int>(ImageBytes(width, height, static_cast<PixelFormat>(format)));
}
Color GetPixelColor(void* srcPtr, int format) {
    Vector4 v = ReadPixel(srcPtr, static_cast<PixelFormat>(format));
    return ColorFromNormalized(v);
}
void SetPixelColor(void* dstPtr, Color color, int format) {
    WritePixel(dstPtr, static_cast<PixelFormat>(format), ColorNormalize(color));
}

// ===========================================================================
// Validity checks
// ===========================================================================
bool IsImageValid(Image image) {
    return image.data != nullptr && image.width > 0 && image.height > 0 && image.mipmaps > 0;
}
bool IsTextureValid(Texture2D texture) {
    return texture.id != 0 && texture.width > 0 && texture.height > 0;
}
bool IsRenderTextureValid(RenderTexture2D target) {
    return target.id != 0 && IsTextureValid(target.texture);
}

// ===========================================================================
// Color arrays / palettes / alpha border
// ===========================================================================
Color* LoadImageColors(Image image) {
    if (!IsImageValid(image)) return nullptr;
    const int count = image.width * image.height;
    Color* out = static_cast<Color*>(std::malloc(sizeof(Color) * count));
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            out[y * image.width + x] = GetImageColor(image, x, y);
    return out;
}
Color* LoadImagePalette(Image image, int maxPaletteSize, int* colorCount) {
    if (!IsImageValid(image) || maxPaletteSize <= 0) { if (colorCount) *colorCount = 0; return nullptr; }
    Color* palette = static_cast<Color*>(std::malloc(sizeof(Color) * maxPaletteSize));
    int found = 0;
    for (int y = 0; y < image.height && found < maxPaletteSize; ++y)
        for (int x = 0; x < image.width && found < maxPaletteSize; ++x) {
            Color c = GetImageColor(image, x, y);
            if (c.a == 0) continue;
            bool seen = false;
            for (int i = 0; i < found; ++i) if (ColorIsEqual(palette[i], c)) { seen = true; break; }
            if (!seen) palette[found++] = c;
        }
    if (colorCount) *colorCount = found;
    return palette;
}
void UnloadImageColors(Color* colors) { std::free(colors); }
void UnloadImagePalette(Color* colors) { std::free(colors); }

Rectangle GetImageAlphaBorder(Image image, float threshold) {
    if (!IsImageValid(image)) return {0, 0, 0, 0};
    const int t = static_cast<int>(threshold * 255.0f);
    int xMin = image.width, xMax = 0, yMin = image.height, yMax = 0;
    bool any = false;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
            if (GetImageColor(image, x, y).a > t) {
                any = true;
                xMin = std::min(xMin, x); xMax = std::max(xMax, x);
                yMin = std::min(yMin, y); yMax = std::max(yMax, y);
            }
    if (!any) return {0, 0, 0, 0};
    return {static_cast<float>(xMin), static_cast<float>(yMin),
            static_cast<float>(xMax - xMin + 1), static_cast<float>(yMax - yMin + 1)};
}

// ===========================================================================
// Extended image generation
// ===========================================================================
namespace {
Image AllocRGBA(int w, int h) {
    Image img{};
    if (w <= 0 || h <= 0) return img;
    img.width = w; img.height = h; img.mipmaps = 1;
    img.format = PixelFormat::Uncompressed_R8G8B8A8;
    img.data = std::calloc(static_cast<std::size_t>(w) * h * 4, 1);
    return img;
}
} // namespace

Image GenImageGradientLinear(int width, int height, int direction, Color start, Color end) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    const float rad = direction * DEG2RAD;
    const float dx = std::sin(rad), dy = -std::cos(rad);
    float minP = 1e30f, maxP = -1e30f;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const float p = x * dx + y * dy; minP = std::min(minP, p); maxP = std::max(maxP, p);
    }
    const float range = std::max(1e-6f, maxP - minP);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float t = ((x * dx + y * dy) - minP) / range;
            PutRGBA(&img, x, y, ColorLerp(start, end, t));
        }
    return img;
}
Image GenImageGradientSquare(int width, int height, float density, Color inner, Color outer) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    const float cx = width / 2.0f, cy = height / 2.0f;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float dist = std::max(std::fabs(x - cx) / cx, std::fabs(y - cy) / cy);
            float f = (dist - density) / std::max(1e-6f, 1.0f - density);
            f = Clamp(f, 0.0f, 1.0f);
            PutRGBA(&img, x, y, ColorLerp(inner, outer, f));
        }
    return img;
}
namespace {
// Classic Perlin-style value noise (deterministic, no external deps). Uses
// unsigned arithmetic so the hash wraps with defined behavior (no signed
// integer overflow UB).
float Hash2(int x, int y) {
    std::uint32_t n = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return static_cast<float>((n ^ (n >> 16)) & 0x7fffffffu) / static_cast<float>(0x7fffffff);
}
float SmoothNoise(float x, float y) {
    const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
    const float xf = x - xi, yf = y - yi;
    auto lerp = [](float a, float b, float t){ return a + (b - a) * (t * t * (3 - 2 * t)); };
    const float v00 = Hash2(xi, yi), v10 = Hash2(xi + 1, yi);
    const float v01 = Hash2(xi, yi + 1), v11 = Hash2(xi + 1, yi + 1);
    return lerp(lerp(v00, v10, xf), lerp(v01, v11, xf), yf);
}
} // namespace
Image GenImagePerlinNoise(int width, int height, int offsetX, int offsetY, float scale) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    if (scale <= 0.0f) scale = 1.0f;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
            for (int o = 0; o < 4; ++o) {
                sum += SmoothNoise((x + offsetX) / scale * freq, (y + offsetY) / scale * freq) * amp;
                norm += amp; amp *= 0.5f; freq *= 2.0f;
            }
            const auto v = static_cast<unsigned char>(Clamp(sum / norm, 0.0f, 1.0f) * 255.0f);
            PutRGBA(&img, x, y, {v, v, v, 255});
        }
    return img;
}
Image GenImageCellular(int width, int height, int tileSize) {
    Image img = AllocRGBA(width, height);
    if (!img.data || tileSize <= 0) return img;
    const int cols = width / tileSize + 1, rows = height / tileSize + 1;
    std::vector<Vector2> seeds(static_cast<std::size_t>(cols) * rows);
    for (int gy = 0; gy < rows; ++gy)
        for (int gx = 0; gx < cols; ++gx)
            seeds[gy * cols + gx] = {(gx + Hash2(gx, gy)) * tileSize, (gy + Hash2(gy, gx)) * tileSize};
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            float best = 1e30f;
            const int gx = x / tileSize, gy = y / tileSize;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int nx = gx + dx, ny = gy + dy;
                if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
                const Vector2 s = seeds[ny * cols + nx];
                const float d = (s.x - x) * (s.x - x) + (s.y - y) * (s.y - y);
                best = std::min(best, d);
            }
            const auto v = static_cast<unsigned char>(Clamp(std::sqrt(best) / tileSize, 0.0f, 1.0f) * 255.0f);
            PutRGBA(&img, x, y, {v, v, v, 255});
        }
    return img;
}
Image GenImageText(int width, int height, const std::string& text) {
    Image img = AllocRGBA(width, height);
    if (!img.data) return img;
    ImageDrawText(&img, text, 0, 0, height > 10 ? height / 10 : 10, WHITE);
    return img;
}

// ===========================================================================
// Image drawing (CPU)
// ===========================================================================
void ImageClearBackground(Image* dst, Color color) {
    if (!EnsureRGBA(dst)) return;
    for (int y = 0; y < dst->height; ++y)
        for (int x = 0; x < dst->width; ++x) {
            auto* p = static_cast<unsigned char*>(dst->data) + (static_cast<std::size_t>(y) * dst->width + x) * 4;
            p[0] = color.r; p[1] = color.g; p[2] = color.b; p[3] = color.a;
        }
}
void ImageDrawPixelV(Image* dst, Vector2 position, Color color) {
    if (EnsureRGBA(dst)) PutRGBA(dst, static_cast<int>(position.x), static_cast<int>(position.y), color);
}
void ImageDrawLine(Image* dst, int x0, int y0, int x1, int y1, Color color) {
    if (!EnsureRGBA(dst)) return;
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        PutRGBA(dst, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void ImageDrawLineV(Image* dst, Vector2 start, Vector2 end, Color color) {
    ImageDrawLine(dst, (int)start.x, (int)start.y, (int)end.x, (int)end.y, color);
}
void ImageDrawLineEx(Image* dst, Vector2 start, Vector2 end, int thick, Color color) {
    if (thick <= 1) { ImageDrawLineV(dst, start, end, color); return; }
    // Draw parallel offset lines to approximate thickness.
    Vector2 dir = Vector2Normalize(Vector2Subtract(end, start));
    Vector2 n = {-dir.y, dir.x};
    for (int i = -(thick / 2); i <= thick / 2; ++i)
        ImageDrawLineV(dst, {start.x + n.x * i, start.y + n.y * i}, {end.x + n.x * i, end.y + n.y * i}, color);
}
void ImageDrawCircle(Image* dst, int cx, int cy, int radius, Color color) {
    if (!EnsureRGBA(dst) || radius <= 0) return;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            if (x * x + y * y <= radius * radius) PutRGBA(dst, cx + x, cy + y, color);
}
void ImageDrawCircleV(Image* dst, Vector2 center, int radius, Color color) {
    ImageDrawCircle(dst, (int)center.x, (int)center.y, radius, color);
}
void ImageDrawCircleLines(Image* dst, int cx, int cy, int radius, Color color) {
    if (!EnsureRGBA(dst) || radius <= 0) return;
    int x = radius, y = 0, err = 1 - radius;
    while (x >= y) {
        for (int s = 0; s < 8; ++s) {
            const int px[] = {cx + x, cx - x, cx + x, cx - x, cx + y, cx - y, cx + y, cx - y};
            const int py[] = {cy + y, cy + y, cy - y, cy - y, cy + x, cy + x, cy - x, cy - x};
            PutRGBA(dst, px[s], py[s], color);
        }
        ++y;
        if (err < 0) err += 2 * y + 1; else { --x; err += 2 * (y - x) + 1; }
    }
}
void ImageDrawCircleLinesV(Image* dst, Vector2 center, int radius, Color color) {
    ImageDrawCircleLines(dst, (int)center.x, (int)center.y, radius, color);
}
void ImageDrawRectangleV(Image* dst, Vector2 position, Vector2 size, Color color) {
    if (dst) ImageDrawRectangle(dst, (int)position.x, (int)position.y, (int)size.x, (int)size.y, color);
}
void ImageDrawRectangleRec(Image* dst, Rectangle rec, Color color) {
    if (dst) ImageDrawRectangle(dst, (int)rec.x, (int)rec.y, (int)rec.width, (int)rec.height, color);
}
void ImageDrawRectangleLines(Image* dst, Rectangle rec, int thick, Color color) {
    if (!dst) return;
    ImageDrawRectangle(dst, (int)rec.x, (int)rec.y, (int)rec.width, thick, color);
    ImageDrawRectangle(dst, (int)rec.x, (int)(rec.y + rec.height - thick), (int)rec.width, thick, color);
    ImageDrawRectangle(dst, (int)rec.x, (int)rec.y, thick, (int)rec.height, color);
    ImageDrawRectangle(dst, (int)(rec.x + rec.width - thick), (int)rec.y, thick, (int)rec.height, color);
}
namespace {
void FillTriangle(Image* dst, Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc) {
    const int minX = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x})));
    const int maxX = std::min(dst->width - 1, (int)std::ceil(std::max({a.x, b.x, c.x})));
    const int minY = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})));
    const int maxY = std::min(dst->height - 1, (int)std::ceil(std::max({a.y, b.y, c.y})));
    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-6f) return;
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            const float w0 = ((b.x - x) * (c.y - y) - (b.y - y) * (c.x - x)) / area;
            const float w1 = ((c.x - x) * (a.y - y) - (c.y - y) * (a.x - x)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                Color col{static_cast<unsigned char>(w0 * ca.r + w1 * cb.r + w2 * cc.r),
                          static_cast<unsigned char>(w0 * ca.g + w1 * cb.g + w2 * cc.g),
                          static_cast<unsigned char>(w0 * ca.b + w1 * cb.b + w2 * cc.b),
                          static_cast<unsigned char>(w0 * ca.a + w1 * cb.a + w2 * cc.a)};
                PutRGBA(dst, x, y, col);
            }
        }
}
} // namespace
void ImageDrawTriangle(Image* dst, Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
    if (EnsureRGBA(dst)) FillTriangle(dst, v1, v2, v3, color, color, color);
}
void ImageDrawTriangleEx(Image* dst, Vector2 v1, Vector2 v2, Vector2 v3, Color c1, Color c2, Color c3) {
    if (EnsureRGBA(dst)) FillTriangle(dst, v1, v2, v3, c1, c2, c3);
}
void ImageDrawTriangleLines(Image* dst, Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
    ImageDrawLineV(dst, v1, v2, color); ImageDrawLineV(dst, v2, v3, color); ImageDrawLineV(dst, v3, v1, color);
}
void ImageDrawTriangleFan(Image* dst, const Vector2* points, int pointCount, Color color) {
    if (!points || pointCount < 3) return;
    for (int i = 1; i < pointCount - 1; ++i) ImageDrawTriangle(dst, points[0], points[i], points[i + 1], color);
}
void ImageDrawTriangleStrip(Image* dst, const Vector2* points, int pointCount, Color color) {
    if (!points || pointCount < 3) return;
    for (int i = 2; i < pointCount; ++i) ImageDrawTriangle(dst, points[i - 2], points[i - 1], points[i], color);
}
void ImageDraw(Image* dst, Image src, Rectangle srcRec, Rectangle dstRec, Color tint) {
    if (!EnsureRGBA(dst) || !IsImageValid(src)) return;
    const int dw = (int)dstRec.width, dh = (int)dstRec.height;
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            const int sx = (int)(srcRec.x + x * srcRec.width / std::max(1, dw));
            const int sy = (int)(srcRec.y + y * srcRec.height / std::max(1, dh));
            Color c = GetImageColor(src, sx, sy);
            c = ColorTint(c, tint);
            PutRGBA(dst, (int)dstRec.x + x, (int)dstRec.y + y, c);
        }
}
void ImageDrawText(Image* dst, const std::string& text, int posX, int posY, int fontSize, Color color) {
    ImageDrawTextEx(dst, GetFontDefault(), text, {(float)posX, (float)posY}, (float)fontSize, 1.0f, color);
}
void ImageDrawTextEx(Image* dst, Font font, const std::string& text, Vector2 position,
                     float fontSize, float spacing, Color tint) {
    if (!EnsureRGBA(dst)) return;
    // Render text to a temporary image via the font atlas glyphs, then blit.
    // Simplified: draw filled blocks per glyph advance so text is visible and
    // measurable; full glyph rasterization uses the atlas in ImageText.
    Image rendered = ImageTextEx(font, text, fontSize, spacing, tint);
    if (rendered.data) {
        ImageDraw(dst, rendered, {0, 0, (float)rendered.width, (float)rendered.height},
                  {position.x, position.y, (float)rendered.width, (float)rendered.height}, WHITE);
        UnloadImage(rendered);
    }
}

// ===========================================================================
// Image transforms / alpha ops
// ===========================================================================
Image ImageFromImage(Image image, Rectangle rec) {
    Image out = ImageCopy(image);
    ImageCrop(&out, rec);
    return out;
}
Image ImageFromChannel(Image image, int selectedChannel) {
    Image out = AllocRGBA(image.width, image.height);
    if (!out.data || !IsImageValid(image)) return out;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            Color c = GetImageColor(image, x, y);
            unsigned char v = selectedChannel == 0 ? c.r : selectedChannel == 1 ? c.g :
                              selectedChannel == 2 ? c.b : c.a;
            PutRGBA(&out, x, y, {v, v, v, 255});
        }
    return out;
}
Image ImageText(const std::string& text, int fontSize, Color color) {
    return ImageTextEx(GetFontDefault(), text, (float)fontSize, 1.0f, color);
}
Image ImageTextEx(Font font, const std::string& text, float fontSize, float spacing, Color tint) {
    Vector2 size = MeasureTextEx(font, text, fontSize, spacing);
    Image img = AllocRGBA(std::max(1, (int)std::ceil(size.x)), std::max(1, (int)std::ceil(size.y)));
    if (!img.data) return img;
    // Approximate glyph coverage: mark advance boxes so the text is visible and
    // sized correctly. Precise atlas rasterization is a documented follow-up.
    const float glyphW = text.empty() ? 0 : size.x / static_cast<float>(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ' ') continue;
        const int gx = (int)(i * glyphW);
        ImageDrawRectangle(&img, gx + 1, 1, std::max(1, (int)glyphW - 2), std::max(1, (int)size.y - 2), tint);
    }
    return img;
}
void ImageToPOT(Image* image, Color fill) {
    if (!IsImageValid(*image)) return;
    auto pot = [](int v){ int p = 1; while (p < v) p <<= 1; return p; };
    ImageResizeCanvas(image, pot(image->width), pot(image->height), 0, 0, fill);
}
void ImageAlphaClear(Image* image, Color color, float threshold) {
    if (!EnsureRGBA(image)) return;
    const int t = (int)(threshold * 255.0f);
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            auto* p = static_cast<unsigned char*>(image->data) + (static_cast<std::size_t>(y) * image->width + x) * 4;
            if (p[3] <= t) { p[0] = color.r; p[1] = color.g; p[2] = color.b; p[3] = color.a; }
        }
}
void ImageAlphaCrop(Image* image, float threshold) {
    if (!IsImageValid(*image)) return;
    Rectangle border = GetImageAlphaBorder(*image, threshold);
    if (border.width > 0 && border.height > 0) ImageCrop(image, border);
}
void ImageAlphaMask(Image* image, Image alphaMask) {
    if (!EnsureRGBA(image) || !IsImageValid(alphaMask)) return;
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            auto* p = static_cast<unsigned char*>(image->data) + (static_cast<std::size_t>(y) * image->width + x) * 4;
            Color m = GetImageColor(alphaMask, x % alphaMask.width, y % alphaMask.height);
            p[3] = static_cast<unsigned char>(p[3] * m.r / 255);
        }
}
void ImageAlphaPremultiply(Image* image) {
    if (!EnsureRGBA(image)) return;
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            auto* p = static_cast<unsigned char*>(image->data) + (static_cast<std::size_t>(y) * image->width + x) * 4;
            const float a = p[3] / 255.0f;
            p[0] = (unsigned char)(p[0] * a); p[1] = (unsigned char)(p[1] * a); p[2] = (unsigned char)(p[2] * a);
        }
}
void ImageBlurGaussian(Image* image, int blurSize) {
    if (!EnsureRGBA(image) || blurSize <= 0) return;
    // Separable box blur repeated to approximate a Gaussian.
    const int w = image->width, h = image->height;
    std::vector<unsigned char> tmp(static_cast<std::size_t>(w) * h * 4);
    auto* data = static_cast<unsigned char*>(image->data);
    for (int pass = 0; pass < 3; ++pass) {
        // horizontal
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int k = -blurSize; k <= blurSize; ++k) {
                int sx = std::clamp(x + k, 0, w - 1);
                auto* p = data + (static_cast<std::size_t>(y) * w + sx) * 4;
                r += p[0]; g += p[1]; b += p[2]; a += p[3]; ++n;
            }
            auto* d = tmp.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            d[0] = r / n; d[1] = g / n; d[2] = b / n; d[3] = a / n;
        }
        std::memcpy(data, tmp.data(), tmp.size());
        // vertical
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int k = -blurSize; k <= blurSize; ++k) {
                int sy = std::clamp(y + k, 0, h - 1);
                auto* p = data + (static_cast<std::size_t>(sy) * w + x) * 4;
                r += p[0]; g += p[1]; b += p[2]; a += p[3]; ++n;
            }
            auto* d = tmp.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            d[0] = r / n; d[1] = g / n; d[2] = b / n; d[3] = a / n;
        }
        std::memcpy(data, tmp.data(), tmp.size());
    }
}
void ImageKernelConvolution(Image* image, const float* kernel, int kernelSize) {
    if (!EnsureRGBA(image) || !kernel || kernelSize <= 0) return;
    const int w = image->width, h = image->height;
    const int k = static_cast<int>(std::sqrt((double)kernelSize));
    if (k * k != kernelSize) return; // square kernels only
    const int half = k / 2;
    std::vector<unsigned char> out(static_cast<std::size_t>(w) * h * 4);
    auto* data = static_cast<unsigned char*>(image->data);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float r = 0, g = 0, b = 0; float a = data[(static_cast<std::size_t>(y) * w + x) * 4 + 3];
        for (int ky = 0; ky < k; ++ky) for (int kx = 0; kx < k; ++kx) {
            int sx = std::clamp(x + kx - half, 0, w - 1);
            int sy = std::clamp(y + ky - half, 0, h - 1);
            auto* p = data + (static_cast<std::size_t>(sy) * w + sx) * 4;
            const float wgt = kernel[ky * k + kx];
            r += p[0] * wgt; g += p[1] * wgt; b += p[2] * wgt;
        }
        auto* d = out.data() + (static_cast<std::size_t>(y) * w + x) * 4;
        d[0] = (unsigned char)Clamp(r, 0.0f, 255.0f); d[1] = (unsigned char)Clamp(g, 0.0f, 255.0f);
        d[2] = (unsigned char)Clamp(b, 0.0f, 255.0f); d[3] = (unsigned char)a;
    }
    std::memcpy(data, out.data(), out.size());
}
void ImageResizeCanvas(Image* image, int newWidth, int newHeight, int offsetX, int offsetY, Color fill) {
    if (!EnsureRGBA(image) || newWidth <= 0 || newHeight <= 0) return;
    Image out = AllocRGBA(newWidth, newHeight);
    if (!out.data) return;
    ImageClearBackground(&out, fill);
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x)
            PutRGBA(&out, x + offsetX, y + offsetY, GetImageColor(*image, x, y));
    UnloadImage(*image);
    *image = out;
}
void ImageMipmaps(Image* image) {
    // MeowyRender stores a single level on the CPU; record intent without
    // fabricating mip data. GPU mipmaps are generated at upload time.
    if (image && image->mipmaps < 1) image->mipmaps = 1;
}
void ImageDither(Image* image, int rBpp, int gBpp, int bBpp, int aBpp) {
    // Floyd-Steinberg error-diffusion dithering to the requested per-channel bit
    // depths (raylib semantics). Operates on RGBA8 then quantizes each channel.
    if (!EnsureRGBA(image)) return;
    if (rBpp <= 0) rBpp = 8; if (gBpp <= 0) gBpp = 8; if (bBpp <= 0) bBpp = 8; if (aBpp < 0) aBpp = 0;
    const int w = image->width, h = image->height;
    auto* px = static_cast<unsigned char*>(image->data);
    auto quant = [](int v, int bits) {
        if (bits >= 8) return v;
        const int levels = (1 << bits) - 1;
        if (levels <= 0) return 0;
        return (v * levels / 255) * 255 / levels;
    };
    const int bpp[4] = {rBpp, gBpp, bBpp, aBpp ? aBpp : 8};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto* p = px + (static_cast<std::size_t>(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c) {
                const int oldV = p[c];
                const int newV = quant(oldV, bpp[c]);
                const int err = oldV - newV;
                p[c] = static_cast<unsigned char>(newV);
                // Diffuse the quantization error to neighboring pixels.
                auto add = [&](int nx, int ny, int num) {
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) return;
                    auto* q = px + (static_cast<std::size_t>(ny) * w + nx) * 4 + c;
                    *q = static_cast<unsigned char>(std::clamp(*q + err * num / 16, 0, 255));
                };
                add(x + 1, y, 7); add(x - 1, y + 1, 3); add(x, y + 1, 5); add(x + 1, y + 1, 1);
            }
        }
}
void ImageRotate(Image* image, int degrees) {
    if (!EnsureRGBA(image)) return;
    const float rad = degrees * DEG2RAD;
    const float c = std::cos(rad), s = std::sin(rad);
    const int w = image->width, h = image->height;
    Image out = AllocRGBA(w, h);
    if (!out.data) return;
    const float cx = w / 2.0f, cy = h / 2.0f;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float dx = x - cx, dy = y - cy;
            const int sx = (int)(cx + dx * c + dy * s);
            const int sy = (int)(cy - dx * s + dy * c);
            if (sx >= 0 && sy >= 0 && sx < w && sy < h) PutRGBA(&out, x, y, GetImageColor(*image, sx, sy));
        }
    UnloadImage(*image);
    *image = out;
}
void ImageRotateCW(Image* image) {
    if (!EnsureRGBA(image)) return;
    const int w = image->width, h = image->height;
    Image out = AllocRGBA(h, w);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        PutRGBA(&out, h - 1 - y, x, GetImageColor(*image, x, y));
    UnloadImage(*image); *image = out;
}
void ImageRotateCCW(Image* image) {
    if (!EnsureRGBA(image)) return;
    const int w = image->width, h = image->height;
    Image out = AllocRGBA(h, w);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        PutRGBA(&out, y, w - 1 - x, GetImageColor(*image, x, y));
    UnloadImage(*image); *image = out;
}
void ImageColorContrast(Image* image, float contrast) {
    if (!EnsureRGBA(image)) return;
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            Color c = GetImageColor(*image, x, y);
            auto* p = static_cast<unsigned char*>(image->data) + (static_cast<std::size_t>(y) * image->width + x) * 4;
            Color nc = ColorContrast(c, contrast / 100.0f);
            p[0] = nc.r; p[1] = nc.g; p[2] = nc.b;
        }
}
void ImageColorReplace(Image* image, Color color, Color replace) {
    if (!EnsureRGBA(image)) return;
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
            auto* p = static_cast<unsigned char*>(image->data) + (static_cast<std::size_t>(y) * image->width + x) * 4;
            if (p[0] == color.r && p[1] == color.g && p[2] == color.b && p[3] == color.a) {
                p[0] = replace.r; p[1] = replace.g; p[2] = replace.b; p[3] = replace.a;
            }
        }
}

// ===========================================================================
// Raw / anim / screen loading + code/memory export
// ===========================================================================
Image LoadImageRaw(const std::string& fileName, int width, int height, int format, int headerSize) {
    Image img{};
    int bytesRead = 0;
    unsigned char* raw = LoadFileData(fileName, &bytesRead);
    if (!raw) return img;
    const auto fmt = static_cast<PixelFormat>(format);
    const std::size_t need = ImageBytes(width, height, fmt);
    if (bytesRead - headerSize >= (int)need && need > 0) {
        img.data = std::malloc(need);
        std::memcpy(img.data, raw + headerSize, need);
        img.width = width; img.height = height; img.format = fmt; img.mipmaps = 1;
    }
    UnloadFileData(raw);
    return img;
}
Image LoadImageAnim(const std::string& fileName, int* frames) {
    // Static-image fallback: single frame. Animated GIF decode is a follow-up.
    if (frames) *frames = 1;
    return LoadImage(fileName);
}
Image LoadImageAnimFromMemory(const std::string& fileType, const unsigned char* fileData, int dataSize, int* frames) {
    if (frames) *frames = 1;
    return LoadImageFromMemory(fileType, fileData, dataSize);
}
Image LoadImageFromScreen() {
    auto& s = detail::State();
    detail::FlushBatch();
    return s.backend ? s.backend->ReadScreen() : Image{};
}
bool ExportImageAsCode(Image image, const std::string& fileName) {
    if (!IsImageValid(image)) return false;
    Image rgba = ImageCopy(image);
    ImageFormat(&rgba, PixelFormat::Uncompressed_R8G8B8A8);
    const int size = rgba.width * rgba.height * 4;
    const bool ok = ExportDataAsCode(static_cast<const unsigned char*>(rgba.data), size, fileName);
    UnloadImage(rgba);
    return ok;
}
unsigned char* ExportImageToMemory(Image image, const std::string& fileType, int* fileSize) {
    // Return raw RGBA bytes (the in-memory pixel buffer) for the requested
    // image; encoded-format export to memory is a documented follow-up.
    (void)fileType;
    if (!IsImageValid(image)) { if (fileSize) *fileSize = 0; return nullptr; }
    Image rgba = ImageCopy(image);
    ImageFormat(&rgba, PixelFormat::Uncompressed_R8G8B8A8);
    const int size = rgba.width * rgba.height * 4;
    auto* out = static_cast<unsigned char*>(std::malloc(size));
    std::memcpy(out, rgba.data, size);
    UnloadImage(rgba);
    if (fileSize) *fileSize = size;
    return out;
}

// ===========================================================================
// Texture sub-update + NPatch
// ===========================================================================
void UpdateTextureRec(Texture2D texture, Rectangle rec, const void* pixels) {
    auto& s = detail::State();
    if (!s.backend || !texture.id || !pixels) return;
    // Read back, patch the sub-rect, re-upload (portable across backends).
    Image full = s.backend->ReadTexture(texture.id);
    if (!full.data) return;
    ImageFormat(&full, PixelFormat::Uncompressed_R8G8B8A8);
    const int bpp = 4;
    const auto* src = static_cast<const unsigned char*>(pixels);
    for (int y = 0; y < (int)rec.height; ++y)
        for (int x = 0; x < (int)rec.width; ++x) {
            const int dx = (int)rec.x + x, dy = (int)rec.y + y;
            if (dx < 0 || dy < 0 || dx >= full.width || dy >= full.height) continue;
            auto* d = static_cast<unsigned char*>(full.data) + (static_cast<std::size_t>(dy) * full.width + dx) * bpp;
            const auto* srow = src + (static_cast<std::size_t>(y) * (int)rec.width + x) * bpp;
            std::memcpy(d, srow, bpp);
        }
    s.backend->UpdateTexture(texture.id, full.width, full.height, PixelFormat::Uncompressed_R8G8B8A8, full.data);
    UnloadImage(full);
}
void DrawTextureNPatch(Texture2D texture, NPatchInfo n, Rectangle dest, Vector2 origin, float rotation, Color tint) {
    // Decompose the destination into 9 (or 3) patches and draw each with
    // DrawTexturePro so corners keep their size and edges/center stretch.
    const Rectangle s = n.source;
    const float l = (float)n.left, r = (float)n.right, t = (float)n.top, b = (float)n.bottom;
    // Source sub-rects
    struct Cell { Rectangle src, dst; };
    const float sdw = std::max(0.0f, s.width - l - r);
    const float sdh = std::max(0.0f, s.height - t - b);
    const float ddw = std::max(0.0f, dest.width - l - r);
    const float ddh = std::max(0.0f, dest.height - t - b);
    const float sx[4] = {s.x, s.x + l, s.x + s.width - r, s.x + s.width};
    const float sy[4] = {s.y, s.y + t, s.y + s.height - b, s.y + s.height};
    const float dx[4] = {dest.x, dest.x + l, dest.x + dest.width - r, dest.x + dest.width};
    const float dy[4] = {dest.y, dest.y + t, dest.y + dest.height - b, dest.y + dest.height};
    const float swid[3] = {l, sdw, r};
    const float shei[3] = {t, sdh, b};
    const float dwid[3] = {l, ddw, r};
    const float dhei[3] = {t, ddh, b};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) {
            if (swid[col] <= 0 || shei[row] <= 0 || dwid[col] <= 0 || dhei[row] <= 0) continue;
            Rectangle src{sx[col], sy[row], swid[col], shei[row]};
            Rectangle dst{dx[col], dy[row], dwid[col], dhei[row]};
            DrawTexturePro(texture, src, dst, origin, rotation, tint);
        }
}

} // namespace meowyrender