// meowyrender - src/modules/shapes.cpp
// 2D shape drawing built on the immediate-mode batch (backend-agnostic).
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <cmath>

namespace meowyrender {

using detail::PushVertex;
using detail::SetBatchState;
using backend::DrawMode;

namespace {

unsigned int WhiteTex() {
    auto& s = detail::State();
    // Prefer a user-set shapes texture (raylib parity) when provided.
    if (s.shapesTextureSet && s.shapesTexture.id) return s.shapesTexture.id;
    return s.backend ? s.backend->WhiteTexture() : 0;
}

// Push a solid-color triangle (untextured -> white texture, uv at 0).
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    SetBatchState(DrawMode::Triangles, WhiteTex());
    PushVertex(a.x, a.y, 0.0f, 0.0f, col);
    PushVertex(b.x, b.y, 0.0f, 0.0f, col);
    PushVertex(c.x, c.y, 0.0f, 0.0f, col);
}

// Push a filled quad as two triangles.
void Quad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color col) {
    Tri(tl, bl, br, col);
    Tri(tl, br, tr, col);
}

} // namespace

// ---------------------------------------------------------------------------
// Pixels & lines
// ---------------------------------------------------------------------------
void DrawPixel(int x, int y, Color color) {
    DrawRectangle(x, y, 1, 1, color);
}
void DrawPixelV(Vector2 position, Color color) {
    DrawRectangle(static_cast<int>(position.x), static_cast<int>(position.y),
                  1, 1, color);
}

void DrawLineV(Vector2 start, Vector2 end, Color color) {
    SetBatchState(DrawMode::Lines, WhiteTex());
    PushVertex(start.x, start.y, 0, 0, color);
    PushVertex(end.x, end.y, 0, 0, color);
}
void DrawLine(int sx, int sy, int ex, int ey, Color color) {
    DrawLineV({static_cast<float>(sx), static_cast<float>(sy)},
              {static_cast<float>(ex), static_cast<float>(ey)}, color);
}

void DrawLineEx(Vector2 start, Vector2 end, float thick, Color color) {
    // Build a rotated quad along the segment for thickness.
    Vector2 dir = Vector2Normalize(Vector2Subtract(end, start));
    Vector2 normal = {-dir.y * thick * 0.5f, dir.x * thick * 0.5f};
    Quad(Vector2Add(start, normal), Vector2Add(end, normal),
         Vector2Subtract(end, normal), Vector2Subtract(start, normal), color);
}

// ---------------------------------------------------------------------------
// Rectangles
// ---------------------------------------------------------------------------
void DrawRectangleRec(Rectangle rec, Color color) {
    Quad({rec.x, rec.y}, {rec.x + rec.width, rec.y},
         {rec.x + rec.width, rec.y + rec.height}, {rec.x, rec.y + rec.height},
         color);
}
void DrawRectangle(int x, int y, int w, int h, Color color) {
    DrawRectangleRec({static_cast<float>(x), static_cast<float>(y),
                      static_cast<float>(w), static_cast<float>(h)}, color);
}
void DrawRectangleV(Vector2 position, Vector2 size, Color color) {
    DrawRectangleRec({position.x, position.y, size.x, size.y}, color);
}

void DrawRectanglePro(Rectangle rec, Vector2 origin, float rotation, Color color) {
    const float rad = rotation * DEG2RAD;
    const float c = std::cos(rad), s = std::sin(rad);
    // Corners relative to origin, then rotated and translated.
    auto rot = [&](float px, float py) -> Vector2 {
        const float dx = px - origin.x, dy = py - origin.y;
        return {rec.x + dx * c - dy * s, rec.y + dx * s + dy * c};
    };
    Quad(rot(0, 0), rot(rec.width, 0), rot(rec.width, rec.height),
         rot(0, rec.height), color);
}

void DrawRectangleLinesEx(Rectangle rec, float t, Color color) {
    DrawRectangleRec({rec.x, rec.y, rec.width, t}, color);                     // top
    DrawRectangleRec({rec.x, rec.y + rec.height - t, rec.width, t}, color);    // bottom
    DrawRectangleRec({rec.x, rec.y + t, t, rec.height - 2 * t}, color);        // left
    DrawRectangleRec({rec.x + rec.width - t, rec.y + t, t, rec.height - 2 * t}, color); // right
}
void DrawRectangleLines(int x, int y, int w, int h, Color color) {
    DrawRectangleLinesEx({static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(w), static_cast<float>(h)}, 1.0f, color);
}

void DrawRectangleGradientV(int x, int y, int w, int h, Color top, Color bottom) {
    SetBatchState(DrawMode::Triangles, WhiteTex());
    const float fx = x, fy = y, fw = w, fh = h;
    PushVertex(fx, fy, 0, 0, top);
    PushVertex(fx, fy + fh, 0, 0, bottom);
    PushVertex(fx + fw, fy + fh, 0, 0, bottom);
    PushVertex(fx, fy, 0, 0, top);
    PushVertex(fx + fw, fy + fh, 0, 0, bottom);
    PushVertex(fx + fw, fy, 0, 0, top);
}
void DrawRectangleGradientH(int x, int y, int w, int h, Color left, Color right) {
    SetBatchState(DrawMode::Triangles, WhiteTex());
    const float fx = x, fy = y, fw = w, fh = h;
    PushVertex(fx, fy, 0, 0, left);
    PushVertex(fx, fy + fh, 0, 0, left);
    PushVertex(fx + fw, fy + fh, 0, 0, right);
    PushVertex(fx, fy, 0, 0, left);
    PushVertex(fx + fw, fy + fh, 0, 0, right);
    PushVertex(fx + fw, fy, 0, 0, right);
}

void DrawRectangleRounded(Rectangle rec, float roundness, int segments, Color color) {
    // Simplified: draw center + edge rects + corner sectors.
    const float r = std::fmin(rec.width, rec.height) * 0.5f * Clamp(roundness, 0.0f, 1.0f);
    if (r <= 0.0f) { DrawRectangleRec(rec, color); return; }
    DrawRectangleRec({rec.x + r, rec.y, rec.width - 2 * r, rec.height}, color);
    DrawRectangleRec({rec.x, rec.y + r, r, rec.height - 2 * r}, color);
    DrawRectangleRec({rec.x + rec.width - r, rec.y + r, r, rec.height - 2 * r}, color);
    DrawCircleSector({rec.x + r, rec.y + r}, r, 180, 270, segments, color);
    DrawCircleSector({rec.x + rec.width - r, rec.y + r}, r, 270, 360, segments, color);
    DrawCircleSector({rec.x + rec.width - r, rec.y + rec.height - r}, r, 0, 90, segments, color);
    DrawCircleSector({rec.x + r, rec.y + rec.height - r}, r, 90, 180, segments, color);
}

// ---------------------------------------------------------------------------
// Circles / ellipses / polygons
// ---------------------------------------------------------------------------
void DrawCircleSector(Vector2 center, float radius, float startAngle,
                      float endAngle, int segments, Color color) {
    if (segments < 4) segments = 36;
    const float step = (endAngle - startAngle) / segments;
    float angle = startAngle;
    for (int i = 0; i < segments; ++i) {
        const float a0 = angle * DEG2RAD;
        const float a1 = (angle + step) * DEG2RAD;
        Tri(center,
            {center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius},
            {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius},
            color);
        angle += step;
    }
}

void DrawCircleV(Vector2 center, float radius, Color color) {
    DrawCircleSector(center, radius, 0, 360, 36, color);
}
void DrawCircle(int cx, int cy, float radius, Color color) {
    DrawCircleV({static_cast<float>(cx), static_cast<float>(cy)}, radius, color);
}

void DrawCircleLines(int cx, int cy, float radius, Color color) {
    const int segments = 36;
    SetBatchState(DrawMode::Lines, WhiteTex());
    for (int i = 0; i < segments; ++i) {
        const float a0 = (360.0f / segments * i) * DEG2RAD;
        const float a1 = (360.0f / segments * (i + 1)) * DEG2RAD;
        PushVertex(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, 0, 0, color);
        PushVertex(cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, 0, 0, color);
    }
}

void DrawEllipse(int cx, int cy, float rh, float rv, Color color) {
    const int segments = 36;
    for (int i = 0; i < segments; ++i) {
        const float a0 = (360.0f / segments * i) * DEG2RAD;
        const float a1 = (360.0f / segments * (i + 1)) * DEG2RAD;
        Tri({static_cast<float>(cx), static_cast<float>(cy)},
            {cx + std::cos(a0) * rh, cy + std::sin(a0) * rv},
            {cx + std::cos(a1) * rh, cy + std::sin(a1) * rv}, color);
    }
}

void DrawPoly(Vector2 center, int sides, float radius, float rotation, Color color) {
    if (sides < 3) sides = 3;
    const float step = 360.0f / sides;
    for (int i = 0; i < sides; ++i) {
        const float a0 = (rotation + step * i) * DEG2RAD;
        const float a1 = (rotation + step * (i + 1)) * DEG2RAD;
        Tri(center,
            {center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius},
            {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius},
            color);
    }
}

// ---------------------------------------------------------------------------
// Triangles
// ---------------------------------------------------------------------------
void DrawTriangle(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
    Tri(v1, v2, v3, color);
}
void DrawTriangleLines(Vector2 v1, Vector2 v2, Vector2 v3, Color color) {
    DrawLineV(v1, v2, color);
    DrawLineV(v2, v3, color);
    DrawLineV(v3, v1, color);
}

// ---------------------------------------------------------------------------
// Collision helpers
// ---------------------------------------------------------------------------
bool CheckCollisionRecs(Rectangle a, Rectangle b) {
    return (a.x < b.x + b.width && a.x + a.width > b.x &&
            a.y < b.y + b.height && a.y + a.height > b.y);
}
bool CheckCollisionCircles(Vector2 c1, float r1, Vector2 c2, float r2) {
    return Vector2Distance(c1, c2) <= (r1 + r2);
}
bool CheckCollisionPointRec(Vector2 p, Rectangle rec) {
    return (p.x >= rec.x && p.x <= rec.x + rec.width &&
            p.y >= rec.y && p.y <= rec.y + rec.height);
}
bool CheckCollisionPointCircle(Vector2 p, Vector2 center, float radius) {
    return Vector2Distance(p, center) <= radius;
}
bool CheckCollisionCircleRec(Vector2 center, float radius, Rectangle rec) {
    const float dx = center.x - std::fmax(rec.x, std::fmin(center.x, rec.x + rec.width));
    const float dy = center.y - std::fmax(rec.y, std::fmin(center.y, rec.y + rec.height));
    return (dx * dx + dy * dy) <= (radius * radius);
}
bool CheckCollisionCircleLine(Vector2 center, float radius, Vector2 p1, Vector2 p2) {
    // Distance from circle center to the segment [p1,p2].
    const float dx = p2.x - p1.x, dy = p2.y - p1.y;
    const float lenSq = dx * dx + dy * dy;
    float t = lenSq > 0.0f ? ((center.x - p1.x) * dx + (center.y - p1.y) * dy) / lenSq : 0.0f;
    t = Clamp(t, 0.0f, 1.0f);
    const float cx = p1.x + t * dx, cy = p1.y + t * dy;
    const float ddx = center.x - cx, ddy = center.y - cy;
    return (ddx * ddx + ddy * ddy) <= (radius * radius);
}
bool CheckCollisionPointTriangle(Vector2 p, Vector2 p1, Vector2 p2, Vector2 p3) {
    // Barycentric sign test.
    const float d1 = (p.x - p2.x) * (p1.y - p2.y) - (p1.x - p2.x) * (p.y - p2.y);
    const float d2 = (p.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p.y - p3.y);
    const float d3 = (p.x - p1.x) * (p3.y - p1.y) - (p3.x - p1.x) * (p.y - p1.y);
    const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(hasNeg && hasPos);
}
bool CheckCollisionPointLine(Vector2 p, Vector2 p1, Vector2 p2, int threshold) {
    // Point-to-segment distance <= threshold pixels.
    const float dx = p2.x - p1.x, dy = p2.y - p1.y;
    const float lenSq = dx * dx + dy * dy;
    float t = lenSq > 0.0f ? ((p.x - p1.x) * dx + (p.y - p1.y) * dy) / lenSq : 0.0f;
    t = Clamp(t, 0.0f, 1.0f);
    const float cx = p1.x + t * dx, cy = p1.y + t * dy;
    return Vector2Distance(p, {cx, cy}) <= static_cast<float>(threshold);
}
bool CheckCollisionPointPoly(Vector2 p, const Vector2* points, int pointCount) {
    if (!points || pointCount < 3) return false;
    // Ray-casting even-odd rule.
    bool inside = false;
    for (int i = 0, j = pointCount - 1; i < pointCount; j = i++) {
        if (((points[i].y > p.y) != (points[j].y > p.y)) &&
            (p.x < (points[j].x - points[i].x) * (p.y - points[i].y) /
                       (points[j].y - points[i].y) + points[i].x))
            inside = !inside;
    }
    return inside;
}
bool CheckCollisionLines(Vector2 a1, Vector2 a2, Vector2 b1, Vector2 b2, Vector2* collisionPoint) {
    const float d = (a2.x - a1.x) * (b2.y - b1.y) - (a2.y - a1.y) * (b2.x - b1.x);
    if (std::fabs(d) < 1e-8f) return false; // parallel
    const float t = ((b1.x - a1.x) * (b2.y - b1.y) - (b1.y - a1.y) * (b2.x - b1.x)) / d;
    const float u = ((b1.x - a1.x) * (a2.y - a1.y) - (b1.y - a1.y) * (a2.x - a1.x)) / d;
    if (t < 0 || t > 1 || u < 0 || u > 1) return false;
    if (collisionPoint) { collisionPoint->x = a1.x + t * (a2.x - a1.x); collisionPoint->y = a1.y + t * (a2.y - a1.y); }
    return true;
}
Rectangle GetCollisionRec(Rectangle r1, Rectangle r2) {
    const float x1 = std::fmax(r1.x, r2.x);
    const float y1 = std::fmax(r1.y, r2.y);
    const float x2 = std::fmin(r1.x + r1.width, r2.x + r2.width);
    const float y2 = std::fmin(r1.y + r1.height, r2.y + r2.height);
    if (x2 <= x1 || y2 <= y1) return {0, 0, 0, 0};
    return {x1, y1, x2 - x1, y2 - y1};
}

// ---------------------------------------------------------------------------
// Shapes texture accessors (raylib parity)
// ---------------------------------------------------------------------------
void SetShapesTexture(Texture2D texture, Rectangle source) {
    auto& s = detail::State();
    s.shapesTexture = texture;
    s.shapesTextureRec = source;
    s.shapesTextureSet = (texture.id != 0);
}
Texture2D GetShapesTexture() { return detail::State().shapesTexture; }
Rectangle GetShapesTextureRectangle() { return detail::State().shapesTextureRec; }

// ---------------------------------------------------------------------------
// Additional draw variants
// ---------------------------------------------------------------------------
void DrawTriangleFan(const Vector2* points, int pointCount, Color color) {
    if (!points || pointCount < 3) return;
    for (int i = 1; i < pointCount - 1; ++i) Tri(points[0], points[i], points[i + 1], color);
}
void DrawTriangleStrip(const Vector2* points, int pointCount, Color color) {
    if (!points || pointCount < 3) return;
    for (int i = 2; i < pointCount; ++i) {
        if (i % 2 == 0) Tri(points[i - 2], points[i - 1], points[i], color);
        else            Tri(points[i - 1], points[i - 2], points[i], color);
    }
}
void DrawPolyLines(Vector2 center, int sides, float radius, float rotation, Color color) {
    if (sides < 3) sides = 3;
    const float step = 360.0f / sides;
    for (int i = 0; i < sides; ++i) {
        const float a0 = (rotation + step * i) * DEG2RAD;
        const float a1 = (rotation + step * (i + 1)) * DEG2RAD;
        DrawLineV({center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius},
                  {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius}, color);
    }
}
void DrawPolyLinesEx(Vector2 center, int sides, float radius, float rotation, float lineThick, Color color) {
    if (sides < 3) sides = 3;
    const float step = 360.0f / sides;
    for (int i = 0; i < sides; ++i) {
        const float a0 = (rotation + step * i) * DEG2RAD;
        const float a1 = (rotation + step * (i + 1)) * DEG2RAD;
        DrawLineEx({center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius},
                   {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius}, lineThick, color);
    }
}
void DrawLineStrip(const Vector2* points, int pointCount, Color color) {
    if (!points || pointCount < 2) return;
    for (int i = 0; i < pointCount - 1; ++i) DrawLineV(points[i], points[i + 1], color);
}
void DrawLineBezier(Vector2 start, Vector2 end, float thick, Color color) {
    // raylib's DrawLineBezier is a quadratic curve with control at the midpoint
    // offset; emulate a smooth cubic between start and end via sampling.
    constexpr int steps = 24;
    Vector2 prev = start;
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        Vector2 cur = {start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t};
        DrawLineEx(prev, cur, thick, color);
        prev = cur;
    }
}
void DrawLineDashed(Vector2 start, Vector2 end, int dashSize, int spaceSize, Color color) {
    const float dashLength = static_cast<float>(dashSize);
    const float gapLength = static_cast<float>(spaceSize);
    if (dashLength <= 0.0f) { DrawLineV(start, end, color); return; }
    const float total = Vector2Distance(start, end);
    if (total <= 0.0f) return;
    Vector2 dir = Vector2Normalize(Vector2Subtract(end, start));
    float pos = 0.0f;
    while (pos < total) {
        const float segEnd = std::fmin(pos + dashLength, total);
        DrawLineV({start.x + dir.x * pos, start.y + dir.y * pos},
                  {start.x + dir.x * segEnd, start.y + dir.y * segEnd}, color);
        pos = segEnd + gapLength;
    }
}
void DrawCircleGradient(Vector2 center, float radius, Color inner, Color outer) {
    const int segments = 36;
    SetBatchState(DrawMode::Triangles, WhiteTex());
    for (int i = 0; i < segments; ++i) {
        const float a0 = (360.0f / segments * i) * DEG2RAD;
        const float a1 = (360.0f / segments * (i + 1)) * DEG2RAD;
        PushVertex(center.x, center.y, 0, 0, inner);
        PushVertex(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius, 0, 0, outer);
        PushVertex(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius, 0, 0, outer);
    }
}
void DrawCircleLinesV(Vector2 center, float radius, Color color) {
    DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y), radius, color);
}
void DrawCircleSectorLines(Vector2 center, float radius, float startAngle, float endAngle, int segments, Color color) {
    if (segments < 4) segments = 36;
    const float step = (endAngle - startAngle) / segments;
    float angle = startAngle;
    DrawLineV(center, {center.x + std::cos(startAngle * DEG2RAD) * radius, center.y + std::sin(startAngle * DEG2RAD) * radius}, color);
    for (int i = 0; i < segments; ++i) {
        const float a0 = angle * DEG2RAD, a1 = (angle + step) * DEG2RAD;
        DrawLineV({center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius},
                  {center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius}, color);
        angle += step;
    }
    DrawLineV(center, {center.x + std::cos(endAngle * DEG2RAD) * radius, center.y + std::sin(endAngle * DEG2RAD) * radius}, color);
}
void DrawEllipseV(Vector2 center, float rh, float rv, Color color) {
    DrawEllipse(static_cast<int>(center.x), static_cast<int>(center.y), rh, rv, color);
}
void DrawEllipseLines(int cx, int cy, float rh, float rv, Color color) {
    const int segments = 36;
    for (int i = 0; i < segments; ++i) {
        const float a0 = (360.0f / segments * i) * DEG2RAD;
        const float a1 = (360.0f / segments * (i + 1)) * DEG2RAD;
        DrawLineV({cx + std::cos(a0) * rh, cy + std::sin(a0) * rv},
                  {cx + std::cos(a1) * rh, cy + std::sin(a1) * rv}, color);
    }
}
void DrawEllipseLinesV(Vector2 center, float rh, float rv, Color color) {
    DrawEllipseLines(static_cast<int>(center.x), static_cast<int>(center.y), rh, rv, color);
}
void DrawRing(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color) {
    if (segments < 4) segments = 36;
    const float step = (endAngle - startAngle) / segments;
    float angle = startAngle;
    for (int i = 0; i < segments; ++i) {
        const float a0 = angle * DEG2RAD, a1 = (angle + step) * DEG2RAD;
        Vector2 oi = {center.x + std::cos(a0) * innerRadius, center.y + std::sin(a0) * innerRadius};
        Vector2 oo = {center.x + std::cos(a0) * outerRadius, center.y + std::sin(a0) * outerRadius};
        Vector2 ni = {center.x + std::cos(a1) * innerRadius, center.y + std::sin(a1) * innerRadius};
        Vector2 no = {center.x + std::cos(a1) * outerRadius, center.y + std::sin(a1) * outerRadius};
        Tri(oi, oo, no, color);
        Tri(oi, no, ni, color);
        angle += step;
    }
}
void DrawRingLines(Vector2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, int segments, Color color) {
    DrawCircleSectorLines(center, innerRadius, startAngle, endAngle, segments, color);
    DrawCircleSectorLines(center, outerRadius, startAngle, endAngle, segments, color);
}
void DrawRectangleGradientEx(Rectangle rec, Color topLeft, Color bottomLeft, Color topRight, Color bottomRight) {
    SetBatchState(DrawMode::Triangles, WhiteTex());
    const float x = rec.x, y = rec.y, w = rec.width, h = rec.height;
    PushVertex(x, y, 0, 0, topLeft);
    PushVertex(x, y + h, 0, 0, bottomLeft);
    PushVertex(x + w, y + h, 0, 0, bottomRight);
    PushVertex(x, y, 0, 0, topLeft);
    PushVertex(x + w, y + h, 0, 0, bottomRight);
    PushVertex(x + w, y, 0, 0, topRight);
}
void DrawRectangleRoundedLines(Rectangle rec, float roundness, int segments, Color color) {
    DrawRectangleRoundedLinesEx(rec, roundness, segments, 1.0f, color);
}
void DrawRectangleRoundedLinesEx(Rectangle rec, float roundness, int segments, float lineThick, Color color) {
    const float r = std::fmin(rec.width, rec.height) * 0.5f * Clamp(roundness, 0.0f, 1.0f);
    if (r <= 0.0f) { DrawRectangleLinesEx(rec, lineThick, color); return; }
    // Straight edges
    DrawLineEx({rec.x + r, rec.y}, {rec.x + rec.width - r, rec.y}, lineThick, color);
    DrawLineEx({rec.x + r, rec.y + rec.height}, {rec.x + rec.width - r, rec.y + rec.height}, lineThick, color);
    DrawLineEx({rec.x, rec.y + r}, {rec.x, rec.y + rec.height - r}, lineThick, color);
    DrawLineEx({rec.x + rec.width, rec.y + r}, {rec.x + rec.width, rec.y + rec.height - r}, lineThick, color);
    // Corner arcs
    DrawCircleSectorLines({rec.x + r, rec.y + r}, r, 180, 270, segments, color);
    DrawCircleSectorLines({rec.x + rec.width - r, rec.y + r}, r, 270, 360, segments, color);
    DrawCircleSectorLines({rec.x + rec.width - r, rec.y + rec.height - r}, r, 0, 90, segments, color);
    DrawCircleSectorLines({rec.x + r, rec.y + rec.height - r}, r, 90, 180, segments, color);
}

// ---------------------------------------------------------------------------
// Spline point evaluators
// ---------------------------------------------------------------------------
Vector2 GetSplinePointLinear(Vector2 a, Vector2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}
Vector2 GetSplinePointBasis(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float t) {
    const float t2 = t * t, t3 = t2 * t;
    const float a = (-t3 + 3 * t2 - 3 * t + 1) / 6.0f;
    const float b = (3 * t3 - 6 * t2 + 4) / 6.0f;
    const float c = (-3 * t3 + 3 * t2 + 3 * t + 1) / 6.0f;
    const float d = t3 / 6.0f;
    return {a * p1.x + b * p2.x + c * p3.x + d * p4.x,
            a * p1.y + b * p2.y + c * p3.y + d * p4.y};
}
Vector2 GetSplinePointCatmullRom(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float t) {
    const float t2 = t * t, t3 = t2 * t;
    const float a = -0.5f * t3 + t2 - 0.5f * t;
    const float b = 1.5f * t3 - 2.5f * t2 + 1.0f;
    const float c = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    const float d = 0.5f * t3 - 0.5f * t2;
    return {a * p1.x + b * p2.x + c * p3.x + d * p4.x,
            a * p1.y + b * p2.y + c * p3.y + d * p4.y};
}
Vector2 GetSplinePointBezierQuad(Vector2 p1, Vector2 c2, Vector2 p3, float t) {
    const float u = 1.0f - t;
    const float a = u * u, b = 2 * u * t, c = t * t;
    return {a * p1.x + b * c2.x + c * p3.x, a * p1.y + b * c2.y + c * p3.y};
}
Vector2 GetSplinePointBezierCubic(Vector2 p1, Vector2 c2, Vector2 c3, Vector2 p4, float t) {
    const float u = 1.0f - t;
    const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    return {a * p1.x + b * c2.x + c * c3.x + d * p4.x,
            a * p1.y + b * c2.y + c * c3.y + d * p4.y};
}

// ---------------------------------------------------------------------------
// Spline drawers (sampled polylines with thickness)
// ---------------------------------------------------------------------------
namespace {
constexpr int kSplineSegments = 24;
template <class Eval>
void DrawSampled(Vector2 from, Vector2 to, float thick, Color color, Eval eval) {
    Vector2 prev = eval(0.0f);
    for (int i = 1; i <= kSplineSegments; ++i) {
        Vector2 cur = eval(static_cast<float>(i) / kSplineSegments);
        DrawLineEx(prev, cur, thick, color);
        prev = cur;
    }
    (void)from; (void)to;
}
} // namespace

void DrawSplineSegmentLinear(Vector2 p1, Vector2 p2, float thick, Color color) {
    DrawLineEx(p1, p2, thick, color);
}
void DrawSplineSegmentBasis(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float thick, Color color) {
    DrawSampled(p2, p3, thick, color, [&](float t){ return GetSplinePointBasis(p1, p2, p3, p4, t); });
}
void DrawSplineSegmentCatmullRom(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, float thick, Color color) {
    DrawSampled(p2, p3, thick, color, [&](float t){ return GetSplinePointCatmullRom(p1, p2, p3, p4, t); });
}
void DrawSplineSegmentBezierQuadratic(Vector2 p1, Vector2 c2, Vector2 p3, float thick, Color color) {
    DrawSampled(p1, p3, thick, color, [&](float t){ return GetSplinePointBezierQuad(p1, c2, p3, t); });
}
void DrawSplineSegmentBezierCubic(Vector2 p1, Vector2 c2, Vector2 c3, Vector2 p4, float thick, Color color) {
    DrawSampled(p1, p4, thick, color, [&](float t){ return GetSplinePointBezierCubic(p1, c2, c3, p4, t); });
}

void DrawSplineLinear(const Vector2* points, int pointCount, float thick, Color color) {
    if (!points || pointCount < 2) return;
    for (int i = 0; i < pointCount - 1; ++i) DrawLineEx(points[i], points[i + 1], thick, color);
}
void DrawSplineBasis(const Vector2* points, int pointCount, float thick, Color color) {
    if (!points || pointCount < 4) return;
    for (int i = 0; i < pointCount - 3; ++i)
        DrawSplineSegmentBasis(points[i], points[i + 1], points[i + 2], points[i + 3], thick, color);
}
void DrawSplineCatmullRom(const Vector2* points, int pointCount, float thick, Color color) {
    if (!points || pointCount < 4) return;
    for (int i = 0; i < pointCount - 3; ++i)
        DrawSplineSegmentCatmullRom(points[i], points[i + 1], points[i + 2], points[i + 3], thick, color);
}
void DrawSplineBezierQuadratic(const Vector2* points, int pointCount, float thick, Color color) {
    if (!points || pointCount < 3) return;
    for (int i = 0; i + 2 < pointCount; i += 2)
        DrawSplineSegmentBezierQuadratic(points[i], points[i + 1], points[i + 2], thick, color);
}
void DrawSplineBezierCubic(const Vector2* points, int pointCount, float thick, Color color) {
    if (!points || pointCount < 4) return;
    for (int i = 0; i + 3 < pointCount; i += 3)
        DrawSplineSegmentBezierCubic(points[i], points[i + 1], points[i + 2], points[i + 3], thick, color);
}

} // namespace meowyrender
