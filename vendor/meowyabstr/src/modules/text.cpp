// meowyrender - src/modules/text.cpp
// Text rendering:
//  - a built-in 5x7 bitmap default font (no external files needed)
//  - TrueType/OTF loading via stb_truetype, rasterized into a packed atlas
//  - UTF-8 codepoint decoding, per-glyph metrics, wrapping
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"
#include "modules/font_data.hpp"

#include "stb_truetype.h"

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace meowyrender {

using detail::PushVertex;
using detail::SetBatchState;
using backend::DrawMode;

// ===========================================================================
// UTF-8 codepoint utilities
// ===========================================================================
int GetCodepointNext(const char* text, int* codepointSize) {
    const auto* s = reinterpret_cast<const unsigned char*>(text);
    int cp = 0x3F; // '?'
    int size = 1;
    if(!text){if(codepointSize)*codepointSize=0;return 0;}
    const int expected=s[0]<0x80?1:s[0]>=0xc2&&s[0]<=0xdf?2:s[0]>=0xe0&&s[0]<=0xef?3:s[0]>=0xf0&&s[0]<=0xf4?4:0;
    for(int i=1;i<expected;++i)if(!s[i] || (s[i]&0xc0)!=0x80){if(codepointSize)*codepointSize=1;return cp;}
    if(!expected || (expected==3&&((s[0]==0xe0&&s[1]<0xa0)||(s[0]==0xed&&s[1]>=0xa0))) ||
       (expected==4&&((s[0]==0xf0&&s[1]<0x90)||(s[0]==0xf4&&s[1]>=0x90)))){if(codepointSize)*codepointSize=1;return cp;}
    if (s[0] < 0x80) { cp = s[0]; size = 1; }
    else if ((s[0] & 0xE0) == 0xC0) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); size = 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); size = 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
             ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); size = 4;
    }
    if (codepointSize) *codepointSize = size;
    return cp;
}

const char* CodepointToUTF8(int codepoint, int* utf8Size) {
    static thread_local char buf[5];
    if(codepoint<0 || codepoint>0x10ffff || (codepoint>=0xd800 && codepoint<=0xdfff))codepoint=0x3f;
    int size = 0;
    if (codepoint < 0x80) {
        buf[0] = static_cast<char>(codepoint); size = 1;
    } else if (codepoint < 0x800) {
        buf[0] = static_cast<char>(0xC0 | (codepoint >> 6));
        buf[1] = static_cast<char>(0x80 | (codepoint & 0x3F)); size = 2;
    } else if (codepoint < 0x10000) {
        buf[0] = static_cast<char>(0xE0 | (codepoint >> 12));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (codepoint & 0x3F)); size = 3;
    } else {
        buf[0] = static_cast<char>(0xF0 | (codepoint >> 18));
        buf[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = static_cast<char>(0x80 | (codepoint & 0x3F)); size = 4;
    }
    buf[size] = '\0';
    if (utf8Size) *utf8Size = size;
    return buf;
}

int GetCodepointCount(const std::string& text) {
    int count = 0;
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        int sz = 1;
        (void)GetCodepointNext(p, &sz);
        p += sz;
        ++count;
    }
    return count;
}

// ===========================================================================
// Built-in bitmap default font
// ===========================================================================
namespace detail {

Font BuildDefaultFont() {
    const auto& glyphs = FontGlyphs();
    const int cellW = kFontWidth + 1;
    const int cellH = kFontHeight + 1;
    const int atlasW = cellW * kFontGlyphCount;
    const int atlasH = cellH;

    std::vector<unsigned char> pixels(static_cast<std::size_t>(atlasW) * atlasH * 4, 0);
    for (int gi = 0; gi < kFontGlyphCount; ++gi) {
        const auto& glyph = glyphs[static_cast<std::size_t>(gi)];
        for (int col = 0; col < kFontWidth; ++col)
            for (int row = 0; row < kFontHeight; ++row)
                if (glyph[static_cast<std::size_t>(col)] & (1u << row)) {
                    const int px = gi * cellW + col;
                    const std::size_t idx =
                        (static_cast<std::size_t>(row) * atlasW + px) * 4;
                    pixels[idx + 0] = 255; pixels[idx + 1] = 255;
                    pixels[idx + 2] = 255; pixels[idx + 3] = 255;
                }
    }

    Font font;
    font.baseSize = kFontHeight;
    font.glyphCount = kFontGlyphCount;
    font.glyphPadding = 1;

    auto& s = State();
    if (s.backend)
        font.texture.id = s.backend->CreateTexture(
            pixels.data(), atlasW, atlasH, PixelFormat::Uncompressed_R8G8B8A8);
    font.texture.width = atlasW;
    font.texture.height = atlasH;

    font.recs = new Rectangle[kFontGlyphCount];
    font.glyphs = new GlyphInfo[kFontGlyphCount];
    for (int gi = 0; gi < kFontGlyphCount; ++gi) {
        font.recs[gi] = {static_cast<float>(gi * cellW), 0.0f,
                         static_cast<float>(kFontWidth), static_cast<float>(kFontHeight)};
        font.glyphs[gi].value = kFontFirstChar + gi;
        font.glyphs[gi].offsetX = 0;
        font.glyphs[gi].offsetY = 0;
        font.glyphs[gi].advanceX = kFontWidth + 1;
    }
    return font;
}

} // namespace detail

Font GetFontDefault() { return detail::State().defaultFont; }

// ===========================================================================
// TrueType font loading (stb_truetype)
// ===========================================================================
namespace {

// Build a font from in-memory TTF/OTF bytes, rasterizing the requested
// codepoints into a single atlas laid out in a simple shelf packer.
Font BuildFontFromTTF(const unsigned char* fileData, int /*dataSize*/,
                      int fontSize, const int* codepoints, int codepointCount) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, fileData, stbtt_GetFontOffsetForIndex(fileData, 0))) {
        std::fprintf(stderr, "[meowyrender] stbtt_InitFont failed\n");
        return GetFontDefault();
    }

    // Default codepoint set: printable ASCII + common Latin-1 if none given.
    std::vector<int> cps;
    if (codepoints && codepointCount > 0) {
        cps.assign(codepoints, codepoints + codepointCount);
    } else {
        for (int c = 32; c <= 126; ++c) cps.push_back(c);
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(fontSize));
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

    const int padding = 2;
    const int glyphCount = static_cast<int>(cps.size());

    // Rasterize each glyph to its own bitmap first.
    struct Raster { int w, h, xoff, yoff, advance; unsigned char* bmp; };
    std::vector<Raster> rasters(glyphCount);
    int maxH = 0;
    for (int i = 0; i < glyphCount; ++i) {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmap(
            &info, 0, scale, cps[static_cast<std::size_t>(i)], &w, &h, &xoff, &yoff);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, cps[static_cast<std::size_t>(i)], &adv, &lsb);
        rasters[static_cast<std::size_t>(i)] =
            {w, h, xoff, yoff, static_cast<int>(adv * scale), bmp};
        maxH = std::max(maxH, h);
    }

    // Shelf-pack into a square-ish atlas.
    const int atlasW = 512;
    int penX = padding, penY = padding, rowH = 0, atlasH = 0;
    std::vector<Rectangle> recs(glyphCount);
    for (int i = 0; i < glyphCount; ++i) {
        const Raster& r = rasters[static_cast<std::size_t>(i)];
        if (penX + r.w + padding > atlasW) {
            penX = padding;
            penY += rowH + padding;
            rowH = 0;
        }
        recs[static_cast<std::size_t>(i)] = {static_cast<float>(penX),
                                             static_cast<float>(penY),
                                             static_cast<float>(r.w),
                                             static_cast<float>(r.h)};
        penX += r.w + padding;
        rowH = std::max(rowH, r.h);
        atlasH = std::max(atlasH, penY + rowH + padding);
    }
    // Round atlas height up to a sane power-of-two-ish value.
    int texH = 1;
    while (texH < atlasH) texH <<= 1;

    // Blit glyph bitmaps into an RGBA atlas (white with alpha = coverage).
    std::vector<unsigned char> atlas(
        static_cast<std::size_t>(atlasW) * texH * 4, 0);
    for (int i = 0; i < glyphCount; ++i) {
        const Raster& r = rasters[static_cast<std::size_t>(i)];
        const Rectangle& rc = recs[static_cast<std::size_t>(i)];
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                const unsigned char cov = r.bmp[y * r.w + x];
                const int ax = static_cast<int>(rc.x) + x;
                const int ay = static_cast<int>(rc.y) + y;
                const std::size_t idx =
                    (static_cast<std::size_t>(ay) * atlasW + ax) * 4;
                atlas[idx + 0] = 255; atlas[idx + 1] = 255;
                atlas[idx + 2] = 255; atlas[idx + 3] = cov;
            }
        if (r.bmp) stbtt_FreeBitmap(r.bmp, nullptr);
    }

    Font font;
    font.baseSize = fontSize;
    font.glyphCount = glyphCount;
    font.glyphPadding = padding;
    auto& s = detail::State();
    if (s.backend)
        font.texture.id = s.backend->CreateTexture(
            atlas.data(), atlasW, texH, PixelFormat::Uncompressed_R8G8B8A8);
    font.texture.width = atlasW;
    font.texture.height = texH;

    font.recs = new Rectangle[glyphCount];
    font.glyphs = new GlyphInfo[glyphCount];
    const int baseline = static_cast<int>(ascent * scale);
    for (int i = 0; i < glyphCount; ++i) {
        font.recs[i] = recs[static_cast<std::size_t>(i)];
        font.glyphs[i].value = cps[static_cast<std::size_t>(i)];
        font.glyphs[i].offsetX = rasters[static_cast<std::size_t>(i)].xoff;
        font.glyphs[i].offsetY = baseline + rasters[static_cast<std::size_t>(i)].yoff;
        font.glyphs[i].advanceX = rasters[static_cast<std::size_t>(i)].advance;
    }
    return font;
}

} // namespace

Font LoadFontFromMemory(const std::string& /*fileType*/,
                        const unsigned char* fileData, int dataSize,
                        int fontSize, const int* codepoints, int codepointCount) {
    return BuildFontFromTTF(fileData, dataSize, fontSize, codepoints, codepointCount);
}

Font LoadFontEx(const std::string& fileName, int fontSize,
                const int* codepoints, int codepointCount) {
    std::FILE* f = std::fopen(fileName.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "[meowyrender] LoadFontEx: cannot open %s\n",
                     fileName.c_str());
        return GetFontDefault();
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    std::fread(data.data(), 1, static_cast<std::size_t>(size), f);
    std::fclose(f);
    return BuildFontFromTTF(data.data(), static_cast<int>(size), fontSize,
                            codepoints, codepointCount);
}

Font LoadFont(const std::string& fileName) {
    return LoadFontEx(fileName, 32, nullptr, 0);
}

bool IsFontValid(Font font) { return font.texture.id != 0 && font.glyphCount > 0; }

void UnloadFont(Font font) {
    // Never free the default font's arrays (shared).
    if (font.texture.id == detail::State().defaultFont.texture.id) return;
    delete[] font.recs;
    delete[] font.glyphs;
    auto& s = detail::State();
    if (s.backend && font.texture.id) s.backend->DestroyTexture(font.texture.id);
}

// ===========================================================================
// Glyph lookup
// ===========================================================================
int GetGlyphIndex(Font font, int codepoint) {
    for (int i = 0; i < font.glyphCount; ++i)
        if (font.glyphs[i].value == codepoint) return i;
    return 0; // fallback to first glyph
}

GlyphInfo GetGlyphInfo(Font font, int codepoint) {
    const int idx = GetGlyphIndex(font, codepoint);
    return font.glyphs ? font.glyphs[idx] : GlyphInfo{};
}

// ===========================================================================
// Drawing
// ===========================================================================
void DrawTextCodepoint(Font font, int codepoint, Vector2 position,
                       float fontSize, Color tint) {
    if (font.texture.id == 0) return;
    const int idx = GetGlyphIndex(font, codepoint);
    const float scale = fontSize / font.baseSize;
    const Rectangle src = font.recs[idx];
    const GlyphInfo& g = font.glyphs[idx];
    const Rectangle dst = {position.x + g.offsetX * scale,
                           position.y + g.offsetY * scale,
                           src.width * scale, src.height * scale};
    DrawTexturePro(font.texture, src, dst, {0, 0}, 0.0f, tint);
}

void DrawTextEx(Font font, const std::string& text, Vector2 position,
                float fontSize, float spacing, Color tint) {
    if (font.texture.id == 0) return;
    const float scale = fontSize / font.baseSize;
    const float lineHeight = fontSize;
    float x = position.x;
    float y = position.y;

    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        int sz = 1;
        const int cp = GetCodepointNext(p, &sz);
        p += sz;
        if (cp == '\n') { x = position.x; y += lineHeight; continue; }
        const int idx = GetGlyphIndex(font, cp);
        const GlyphInfo& g = font.glyphs[idx];
        if (cp != ' ') {
            const Rectangle src = font.recs[idx];
            const Rectangle dst = {x + g.offsetX * scale, y + g.offsetY * scale,
                                   src.width * scale, src.height * scale};
            DrawTexturePro(font.texture, src, dst, {0, 0}, 0.0f, tint);
        }
        const float adv = (g.advanceX > 0 ? g.advanceX : (int)font.baseSize) * scale;
        x += adv + spacing;
    }
}

void DrawTextPro(Font font, const std::string& text, Vector2 position,
                 Vector2 origin, float rotation, float fontSize,
                 float spacing, Color tint) {
    if (font.texture.id == 0) return;
    const float scale = fontSize / font.baseSize;
    const float lineHeight = fontSize;
    // Lay glyphs out in the text's local space (origin at the top-left of the
    // run), then rotate each glyph about `origin` by `rotation` degrees and
    // translate to `position`. This matches raylib's DrawTextPro semantics.
    const float rad = rotation * DEG2RAD;
    const float c = std::cos(rad), s = std::sin(rad);
    auto place = [&](float lx, float ly) -> Vector2 {
        const float dx = lx - origin.x, dy = ly - origin.y;
        return {position.x + dx * c - dy * s, position.y + dx * s + dy * c};
    };

    float x = 0.0f, y = 0.0f;
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        int sz = 1;
        const int cp = GetCodepointNext(p, &sz);
        p += sz;
        if (cp == '\n') { x = 0.0f; y += lineHeight; continue; }
        const int idx = GetGlyphIndex(font, cp);
        const GlyphInfo& g = font.glyphs[idx];
        if (cp != ' ') {
            const Rectangle src = font.recs[idx];
            // Rotate the glyph's local top-left about origin; draw the glyph
            // quad rotated by the same angle so the whole run stays rigid.
            const Vector2 gp = place(x + g.offsetX * scale, y + g.offsetY * scale);
            const Rectangle dst = {gp.x, gp.y, src.width * scale, src.height * scale};
            DrawTexturePro(font.texture, src, dst, {0, 0}, rotation, tint);
        }
        const float adv = (g.advanceX > 0 ? g.advanceX : (int)font.baseSize) * scale;
        x += adv + spacing;
    }
}

void DrawText(const std::string& text, int x, int y, int fontSize, Color color) {
    Font font = GetFontDefault();
    const float spacing = fontSize / 10.0f;
    DrawTextEx(font, text, {static_cast<float>(x), static_cast<float>(y)},
               static_cast<float>(fontSize), spacing, color);
}

Vector2 MeasureTextEx(Font font, const std::string& text, float fontSize, float spacing) {
    const float scale = fontSize / font.baseSize;
    float lineW = 0.0f, maxW = 0.0f, totalH = fontSize;
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        int sz = 1;
        const int cp = GetCodepointNext(p, &sz);
        p += sz;
        if (cp == '\n') {
            maxW = std::max(maxW, lineW);
            lineW = 0.0f;
            totalH += fontSize;
            continue;
        }
        const int idx = GetGlyphIndex(font, cp);
        const int adv = font.glyphs[idx].advanceX > 0 ? font.glyphs[idx].advanceX
                                                       : static_cast<int>(font.baseSize);
        lineW += adv * scale + spacing;
    }
    maxW = std::max(maxW, lineW);
    return {maxW, totalH};
}

int MeasureText(const std::string& text, int fontSize) {
    const float spacing = fontSize / 10.0f;
    return static_cast<int>(
        MeasureTextEx(GetFontDefault(), text, static_cast<float>(fontSize), spacing).x);
}

void DrawFPS(int x, int y) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d FPS", GetFPS());
    Color col = GetFPS() >= 30 ? LIME : (GetFPS() >= 15 ? ORANGE : RED);
    DrawText(buf, x, y, 20, col);
}

// ===========================================================================
// Codepoint-array drawing/measuring + glyph atlas rect
// ===========================================================================
Rectangle GetGlyphAtlasRec(Font font, int codepoint) {
    if (!font.recs || font.glyphCount <= 0) return {0, 0, 0, 0};
    return font.recs[GetGlyphIndex(font, codepoint)];
}
void DrawTextCodepoints(Font font, const int* codepoints, int count, Vector2 position,
                        float fontSize, float spacing, Color tint) {
    if (font.texture.id == 0 || !codepoints) return;
    const float scale = fontSize / font.baseSize;
    float x = position.x;
    for (int i = 0; i < count; ++i) {
        const int cp = codepoints[i];
        if (cp == '\n') { x = position.x; position.y += fontSize; continue; }
        const int idx = GetGlyphIndex(font, cp);
        const GlyphInfo& g = font.glyphs[idx];
        if (cp != ' ') {
            const Rectangle src = font.recs[idx];
            const Rectangle dst = {x + g.offsetX * scale, position.y + g.offsetY * scale,
                                   src.width * scale, src.height * scale};
            DrawTexturePro(font.texture, src, dst, {0, 0}, 0.0f, tint);
        }
        const int adv = g.advanceX > 0 ? g.advanceX : (int)font.baseSize;
        x += adv * scale + spacing;
    }
}
Vector2 MeasureTextCodepoints(Font font, const int* codepoints, int count,
                              float fontSize, float spacing) {
    if (!codepoints) return {0, fontSize};
    const float scale = fontSize / font.baseSize;
    float lineW = 0, maxW = 0, totalH = fontSize;
    for (int i = 0; i < count; ++i) {
        if (codepoints[i] == '\n') { maxW = std::max(maxW, lineW); lineW = 0; totalH += fontSize; continue; }
        const int idx = GetGlyphIndex(font, codepoints[i]);
        const int adv = font.glyphs[idx].advanceX > 0 ? font.glyphs[idx].advanceX : (int)font.baseSize;
        lineW += adv * scale + spacing;
    }
    maxW = std::max(maxW, lineW);
    return {maxW, totalH};
}

// ===========================================================================
// Font data / atlas generation
// ===========================================================================
GlyphInfo* LoadFontData(const unsigned char* fileData, int /*dataSize*/, int fontSize,
                        const int* codepoints, int codepointCount, int /*type*/) {
    if (!fileData || fontSize <= 0) return nullptr;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, fileData, stbtt_GetFontOffsetForIndex(fileData, 0))) return nullptr;
    std::vector<int> cps;
    if (codepoints && codepointCount > 0) cps.assign(codepoints, codepoints + codepointCount);
    else for (int c = 32; c <= 126; ++c) cps.push_back(c);
    const int count = static_cast<int>(cps.size());
    const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(fontSize));
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const int baseline = static_cast<int>(ascent * scale);
    auto* glyphs = new GlyphInfo[count];
    for (int i = 0; i < count; ++i) {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmap(&info, 0, scale, cps[i], &w, &h, &xoff, &yoff);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, cps[i], &adv, &lsb);
        glyphs[i].value = cps[i];
        glyphs[i].offsetX = xoff;
        glyphs[i].offsetY = baseline + yoff;
        glyphs[i].advanceX = static_cast<int>(adv * scale);
        // Store the rasterized coverage as a single-channel image on the glyph.
        glyphs[i].image.width = w;
        glyphs[i].image.height = h;
        glyphs[i].image.mipmaps = 1;
        glyphs[i].image.format = PixelFormat::Uncompressed_Grayscale;
        if (w > 0 && h > 0 && bmp) {
            glyphs[i].image.data = std::malloc(static_cast<std::size_t>(w) * h);
            std::memcpy(glyphs[i].image.data, bmp, static_cast<std::size_t>(w) * h);
        }
        if (bmp) stbtt_FreeBitmap(bmp, nullptr);
    }
    return glyphs;
}
void UnloadFontData(GlyphInfo* glyphs, int glyphCount) {
    if (!glyphs) return;
    for (int i = 0; i < glyphCount; ++i) std::free(glyphs[i].image.data);
    delete[] glyphs;
}
Image GenImageFontAtlas(const GlyphInfo* glyphs, Rectangle** glyphRecs, int glyphCount,
                        int fontSize, int padding, int /*packMethod*/) {
    Image atlas{};
    if (!glyphs || glyphCount <= 0) return atlas;
    const int atlasW = 512;
    int penX = padding, penY = padding, rowH = 0, atlasH = 0;
    std::vector<Rectangle> recs(glyphCount);
    for (int i = 0; i < glyphCount; ++i) {
        const int w = glyphs[i].image.width, h = glyphs[i].image.height;
        if (penX + w + padding > atlasW) { penX = padding; penY += rowH + padding; rowH = 0; }
        recs[i] = {(float)penX, (float)penY, (float)w, (float)h};
        penX += w + padding; rowH = std::max(rowH, h);
        atlasH = std::max(atlasH, penY + rowH + padding);
    }
    int texH = 1; while (texH < atlasH) texH <<= 1;
    atlas.width = atlasW; atlas.height = texH; atlas.mipmaps = 1;
    atlas.format = PixelFormat::Uncompressed_R8G8B8A8;
    atlas.data = std::calloc(static_cast<std::size_t>(atlasW) * texH * 4, 1);
    auto* px = static_cast<unsigned char*>(atlas.data);
    for (int i = 0; i < glyphCount; ++i) {
        const int w = glyphs[i].image.width, h = glyphs[i].image.height;
        const auto* src = static_cast<const unsigned char*>(glyphs[i].image.data);
        if (!src) continue;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const int ax = (int)recs[i].x + x, ay = (int)recs[i].y + y;
            const std::size_t idx = (static_cast<std::size_t>(ay) * atlasW + ax) * 4;
            px[idx + 0] = 255; px[idx + 1] = 255; px[idx + 2] = 255; px[idx + 3] = src[y * w + x];
        }
    }
    if (glyphRecs) {
        *glyphRecs = static_cast<Rectangle*>(std::malloc(sizeof(Rectangle) * glyphCount));
        std::memcpy(*glyphRecs, recs.data(), sizeof(Rectangle) * glyphCount);
    }
    (void)fontSize;
    return atlas;
}
Font LoadFontFromImage(Image image, Color key, int firstChar) {
    // Parse a fixed-cell bitmap font: detect glyph cells separated by the key
    // color along the top row (raylib's simple image-font convention).
    Font font{};
    if (!IsImageValid(image)) return GetFontDefault();
    Image rgba = ImageCopy(image);
    ImageFormat(&rgba, PixelFormat::Uncompressed_R8G8B8A8);
    // Scan the first row to find glyph column boundaries.
    std::vector<std::pair<int,int>> cells; // (x, width)
    int x = 0;
    auto isKey = [&](int px) {
        Color c = GetImageColor(rgba, px, 0);
        return ColorIsEqual(c, key);
    };
    while (x < rgba.width) {
        while (x < rgba.width && isKey(x)) ++x;
        if (x >= rgba.width) break;
        int start = x;
        while (x < rgba.width && !isKey(x)) ++x;
        cells.emplace_back(start, x - start);
    }
    const int count = static_cast<int>(cells.size());
    if (count == 0) { UnloadImage(rgba); return GetFontDefault(); }
    font.baseSize = rgba.height;
    font.glyphCount = count;
    font.glyphPadding = 0;
    font.recs = new Rectangle[count];
    font.glyphs = new GlyphInfo[count];
    for (int i = 0; i < count; ++i) {
        font.recs[i] = {(float)cells[i].first, 0.0f, (float)cells[i].second, (float)rgba.height};
        font.glyphs[i].value = firstChar + i;
        font.glyphs[i].offsetX = 0;
        font.glyphs[i].offsetY = 0;
        font.glyphs[i].advanceX = cells[i].second + 1;
    }
    auto& s = detail::State();
    if (s.backend)
        font.texture.id = s.backend->CreateTexture(rgba.data, rgba.width, rgba.height,
                                                   PixelFormat::Uncompressed_R8G8B8A8);
    font.texture.width = rgba.width;
    font.texture.height = rgba.height;
    UnloadImage(rgba);
    return font;
}
bool ExportFontAsCode(Font font, const std::string& fileName) {
    if (!IsFontValid(font)) return false;
    // Export glyph metrics as a C array (atlas pixels are not embedded here;
    // documented limitation vs raylib's full recreator).
    std::string code = "// MeowyRender ExportFontAsCode (glyph metrics)\n";
    code += "static const int FONT_GLYPH_COUNT = " + std::to_string(font.glyphCount) + ";\n";
    code += "static const int FONT_BASE_SIZE = " + std::to_string(font.baseSize) + ";\n";
    return SaveFileText(fileName, code);
}

} // namespace meowyrender
