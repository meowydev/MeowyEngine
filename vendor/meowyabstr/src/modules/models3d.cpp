// meowyrender - src/modules/models3d.cpp
// 3D core: camera helpers, 3D primitives, and 3D collision. Primitives push
// world-space vertices into the shared batch; the backend applies the active
// perspective projection + view matrix set by BeginMode3D.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <cmath>
#include <algorithm>

namespace meowyrender {

using detail::SetBatchState;
using backend::DrawMode;

namespace {

unsigned int WhiteTex() {
    auto& s = detail::State();
    return s.backend ? s.backend->WhiteTexture() : 0;
}

void V3(float x, float y, float z, Color c) {
    detail::State().batch.verts.push_back(
        backend::Vertex{x, y, z, 0.0f, 0.0f, c.r, c.g, c.b, c.a});
}

void Tri3(Vector3 a, Vector3 b, Vector3 c, Color col) {
    SetBatchState(DrawMode::Triangles, WhiteTex());
    V3(a.x, a.y, a.z, col);
    V3(b.x, b.y, b.z, col);
    V3(c.x, c.y, c.z, col);
}

void Line3(Vector3 a, Vector3 b, Color col) {
    SetBatchState(DrawMode::Lines, WhiteTex());
    V3(a.x, a.y, a.z, col);
    V3(b.x, b.y, b.z, col);
}

} // namespace

// ===========================================================================
// Camera helpers
// ===========================================================================
Matrix GetCameraMatrix(Camera3D camera) {
    return MatrixLookAt(camera.position, camera.target, camera.up);
}

void UpdateCamera(Camera3D* camera, CameraMode mode) {
    if (!camera) return;
    // A pragmatic subset: orbital spins around the target; first-person uses
    // WASD + mouse look. Enough to drive a game camera; extend as needed.
    const float dt = GetFrameTime();
    if (mode == CameraMode::Orbital) {
        const float angle = 0.5f * dt;
        Vector3 view = Vector3Subtract(camera->position, camera->target);
        const float c = std::cos(angle), s = std::sin(angle);
        const float nx = view.x * c - view.z * s;
        const float nz = view.x * s + view.z * c;
        camera->position = Vector3Add(camera->target, {nx, view.y, nz});
    } else if (mode == CameraMode::FirstPerson || mode == CameraMode::Free) {
        const float speed = 5.0f * dt;
        Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
        Vector3 move{};
        if (IsKeyDown(KeyboardKey::W)) move = Vector3Add(move, forward);
        if (IsKeyDown(KeyboardKey::S)) move = Vector3Subtract(move, forward);
        if (IsKeyDown(KeyboardKey::D)) move = Vector3Add(move, right);
        if (IsKeyDown(KeyboardKey::A)) move = Vector3Subtract(move, right);
        move = Vector3Scale(move, speed);
        camera->position = Vector3Add(camera->position, move);
        camera->target = Vector3Add(camera->target, move);
    }
}

Vector2 GetWorldToScreen(Vector3 position, Camera3D camera) {
    auto& s = detail::State();
    const double aspect = static_cast<double>(s.screenWidth) / s.screenHeight;
    Matrix proj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, 0.01, 1000.0);
    Matrix view = MatrixLookAt(camera.position, camera.target, camera.up);
    // Transform to clip space.
    Vector3 v = Vector3Transform(position, MatrixMultiply(view, proj));
    // Perspective divide approximated (w from view-space z).
    Vector3 vv = Vector3Transform(position, view);
    const float w = -vv.z;
    if (w <= 0.0f) return {-1, -1};
    const float ndcX = v.x / w;
    const float ndcY = v.y / w;
    return {(ndcX + 1.0f) * 0.5f * s.screenWidth,
            (1.0f - (ndcY + 1.0f) * 0.5f) * s.screenHeight};
}

Ray GetMouseRay(Vector2 mousePosition, Camera3D camera) {
    auto& s = detail::State();
    // Build a ray from the camera through the mouse pixel.
    const float x = (2.0f * mousePosition.x) / s.screenWidth - 1.0f;
    const float y = 1.0f - (2.0f * mousePosition.y) / s.screenHeight;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    const float aspect = static_cast<float>(s.screenWidth) / s.screenHeight;
    const float tanF = std::tan(camera.fovy * 0.5f * DEG2RAD);
    Vector3 dir = Vector3Normalize(Vector3Add(forward,
        Vector3Add(Vector3Scale(right, x * tanF * aspect),
                   Vector3Scale(up, y * tanF))));
    return {camera.position, dir};
}

// ===========================================================================
// 3D primitives
// ===========================================================================
void DrawLine3D(Vector3 start, Vector3 end, Color color) { Line3(start, end, color); }

void DrawPoint3D(Vector3 position, Color color) {
    Line3(position, {position.x + 0.01f, position.y, position.z}, color);
}

void DrawTriangle3D(Vector3 v1, Vector3 v2, Vector3 v3, Color color) {
    Tri3(v1, v2, v3, color);
}

void DrawCubeV(Vector3 position, Vector3 size, Color color) {
    const float x = position.x, y = position.y, z = position.z;
    const float w = size.x / 2, h = size.y / 2, l = size.z / 2;
    // 8 corners
    Vector3 c[8] = {
        {x - w, y - h, z + l}, {x + w, y - h, z + l},
        {x + w, y + h, z + l}, {x - w, y + h, z + l},
        {x - w, y - h, z - l}, {x + w, y - h, z - l},
        {x + w, y + h, z - l}, {x - w, y + h, z - l},
    };
    auto face = [&](int a, int b, int cc, int d) {
        Tri3(c[a], c[b], c[cc], color);
        Tri3(c[a], c[cc], c[d], color);
    };
    face(0, 1, 2, 3); // front
    face(5, 4, 7, 6); // back
    face(4, 0, 3, 7); // left
    face(1, 5, 6, 2); // right
    face(3, 2, 6, 7); // top
    face(4, 5, 1, 0); // bottom
}

void DrawCube(Vector3 position, float width, float height, float length, Color color) {
    DrawCubeV(position, {width, height, length}, color);
}

void DrawCubeWires(Vector3 position, float width, float height, float length, Color color) {
    const float x = position.x, y = position.y, z = position.z;
    const float w = width / 2, h = height / 2, l = length / 2;
    Vector3 c[8] = {
        {x - w, y - h, z + l}, {x + w, y - h, z + l},
        {x + w, y + h, z + l}, {x - w, y + h, z + l},
        {x - w, y - h, z - l}, {x + w, y - h, z - l},
        {x + w, y + h, z - l}, {x - w, y + h, z - l},
    };
    const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
                              {6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto& e : edges) Line3(c[e[0]], c[e[1]], color);
}

void DrawSphereEx(Vector3 center, float radius, int rings, int slices, Color color) {
    if (rings < 3) rings = 8;
    if (slices < 3) slices = 8;
    for (int i = 0; i < rings; ++i) {
        const float lat0 = PI * (-0.5f + static_cast<float>(i) / rings);
        const float lat1 = PI * (-0.5f + static_cast<float>(i + 1) / rings);
        for (int j = 0; j < slices; ++j) {
            const float lng0 = 2 * PI * static_cast<float>(j) / slices;
            const float lng1 = 2 * PI * static_cast<float>(j + 1) / slices;
            auto pt = [&](float lat, float lng) -> Vector3 {
                return {center.x + radius * std::cos(lat) * std::cos(lng),
                        center.y + radius * std::sin(lat),
                        center.z + radius * std::cos(lat) * std::sin(lng)};
            };
            Vector3 a = pt(lat0, lng0), b = pt(lat1, lng0);
            Vector3 c = pt(lat1, lng1), d = pt(lat0, lng1);
            Tri3(a, b, c, color);
            Tri3(a, c, d, color);
        }
    }
}

void DrawSphere(Vector3 center, float radius, Color color) {
    DrawSphereEx(center, radius, 16, 16, color);
}

void DrawSphereWires(Vector3 center, float radius, int rings, int slices, Color color) {
    if (rings < 3) rings = 8;
    if (slices < 3) slices = 8;
    for (int i = 0; i < rings; ++i) {
        const float lat0 = PI * (-0.5f + static_cast<float>(i) / rings);
        const float lat1 = PI * (-0.5f + static_cast<float>(i + 1) / rings);
        for (int j = 0; j < slices; ++j) {
            const float lng0 = 2 * PI * static_cast<float>(j) / slices;
            auto pt = [&](float lat, float lng) -> Vector3 {
                return {center.x + radius * std::cos(lat) * std::cos(lng),
                        center.y + radius * std::sin(lat),
                        center.z + radius * std::cos(lat) * std::sin(lng)};
            };
            Line3(pt(lat0, lng0), pt(lat1, lng0), color);
        }
    }
}

void DrawCylinder(Vector3 position, float radiusTop, float radiusBottom,
                  float height, int slices, Color color) {
    if (slices < 3) slices = 16;
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2 * PI * i / slices;
        const float a1 = 2 * PI * (i + 1) / slices;
        Vector3 b0 = {position.x + radiusBottom * std::cos(a0), position.y,
                      position.z + radiusBottom * std::sin(a0)};
        Vector3 b1 = {position.x + radiusBottom * std::cos(a1), position.y,
                      position.z + radiusBottom * std::sin(a1)};
        Vector3 t0 = {position.x + radiusTop * std::cos(a0), position.y + height,
                      position.z + radiusTop * std::sin(a0)};
        Vector3 t1 = {position.x + radiusTop * std::cos(a1), position.y + height,
                      position.z + radiusTop * std::sin(a1)};
        Tri3(b0, b1, t1, color);
        Tri3(b0, t1, t0, color);
    }
}

void DrawPlane(Vector3 center, Vector2 size, Color color) {
    const float w = size.x / 2, l = size.y / 2;
    Vector3 a = {center.x - w, center.y, center.z - l};
    Vector3 b = {center.x - w, center.y, center.z + l};
    Vector3 c = {center.x + w, center.y, center.z + l};
    Vector3 d = {center.x + w, center.y, center.z - l};
    Tri3(a, b, c, color);
    Tri3(a, c, d, color);
}

void DrawGrid(int slices, float spacing) {
    const int half = slices / 2;
    for (int i = -half; i <= half; ++i) {
        const Color col = (i == 0) ? Color{120, 120, 120, 255}
                                   : Color{80, 80, 80, 255};
        Line3({static_cast<float>(i) * spacing, 0, static_cast<float>(-half) * spacing},
              {static_cast<float>(i) * spacing, 0, static_cast<float>(half) * spacing}, col);
        Line3({static_cast<float>(-half) * spacing, 0, static_cast<float>(i) * spacing},
              {static_cast<float>(half) * spacing, 0, static_cast<float>(i) * spacing}, col);
    }
}

// ===========================================================================
// 3D collision
// ===========================================================================
bool CheckCollisionSpheres(Vector3 c1, float r1, Vector3 c2, float r2) {
    return Vector3Distance(c1, c2) <= (r1 + r2);
}

bool CheckCollisionBoxes(BoundingBox a, BoundingBox b) {
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
           (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

bool CheckCollisionBoxSphere(BoundingBox box, Vector3 center, float radius) {
    const float dx = std::max(box.min.x - center.x, std::max(0.0f, center.x - box.max.x));
    const float dy = std::max(box.min.y - center.y, std::max(0.0f, center.y - box.max.y));
    const float dz = std::max(box.min.z - center.z, std::max(0.0f, center.z - box.max.z));
    return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
}

RayCollision GetRayCollisionSphere(Ray ray, Vector3 center, float radius) {
    RayCollision col{};
    Vector3 oc = Vector3Subtract(ray.position, center);
    const float b = Vector3DotProduct(oc, ray.direction);
    const float c = Vector3DotProduct(oc, oc) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) return col;
    const float dist = -b - std::sqrt(disc);
    if (dist < 0.0f) return col;
    col.hit = true;
    col.distance = dist;
    col.point = Vector3Add(ray.position, Vector3Scale(ray.direction, dist));
    col.normal = Vector3Normalize(Vector3Subtract(col.point, center));
    return col;
}

RayCollision GetRayCollisionBox(Ray ray, BoundingBox box) {
    RayCollision col{};
    // Slab method.
    float tmin = 0.0f, tmax = 1e30f;
    const float o[3] = {ray.position.x, ray.position.y, ray.position.z};
    const float d[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float bmin[3] = {box.min.x, box.min.y, box.min.z};
    const float bmax[3] = {box.max.x, box.max.y, box.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-8f) {
            if (o[i] < bmin[i] || o[i] > bmax[i]) return col;
        } else {
            float t1 = (bmin[i] - o[i]) / d[i];
            float t2 = (bmax[i] - o[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return col;
        }
    }
    col.hit = true;
    col.distance = tmin;
    col.point = Vector3Add(ray.position, Vector3Scale(ray.direction, tmin));
    return col;
}

RayCollision GetRayCollisionTriangle(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3) {
    // Möller–Trumbore intersection.
    RayCollision col{};
    const Vector3 e1 = Vector3Subtract(p2, p1);
    const Vector3 e2 = Vector3Subtract(p3, p1);
    const Vector3 pv = Vector3CrossProduct(ray.direction, e2);
    const float det = Vector3DotProduct(e1, pv);
    if (std::fabs(det) < 1e-8f) return col;
    const float inv = 1.0f / det;
    const Vector3 tv = Vector3Subtract(ray.position, p1);
    const float u = Vector3DotProduct(tv, pv) * inv;
    if (u < 0.0f || u > 1.0f) return col;
    const Vector3 qv = Vector3CrossProduct(tv, e1);
    const float v = Vector3DotProduct(ray.direction, qv) * inv;
    if (v < 0.0f || u + v > 1.0f) return col;
    const float t = Vector3DotProduct(e2, qv) * inv;
    if (t < 0.0f) return col;
    col.hit = true;
    col.distance = t;
    col.point = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    col.normal = Vector3Normalize(Vector3CrossProduct(e1, e2));
    return col;
}
RayCollision GetRayCollisionQuad(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 p4) {
    RayCollision col = GetRayCollisionTriangle(ray, p1, p2, p3);
    if (!col.hit) col = GetRayCollisionTriangle(ray, p1, p3, p4);
    return col;
}
RayCollision GetRayCollisionMesh(Ray ray, Mesh mesh, Matrix transform) {
    RayCollision nearest{};
    if (!mesh.vertices || mesh.triangleCount <= 0) return nearest;
    auto vertexAt = [&](int i) -> Vector3 {
        const int idx = mesh.indices ? mesh.indices[i] : i;
        Vector3 v{mesh.vertices[idx * 3], mesh.vertices[idx * 3 + 1], mesh.vertices[idx * 3 + 2]};
        return Vector3Transform(v, transform);
    };
    for (int t = 0; t < mesh.triangleCount; ++t) {
        RayCollision hit = GetRayCollisionTriangle(ray, vertexAt(t * 3), vertexAt(t * 3 + 1), vertexAt(t * 3 + 2));
        if (hit.hit && (!nearest.hit || hit.distance < nearest.distance)) nearest = hit;
    }
    return nearest;
}

// ===========================================================================
// Additional 3D primitives
// ===========================================================================
void DrawRay(Ray ray, Color color) {
    Line3(ray.position, Vector3Add(ray.position, Vector3Scale(ray.direction, 1000.0f)), color);
}
void DrawCubeWiresV(Vector3 position, Vector3 size, Color color) {
    DrawCubeWires(position, size.x, size.y, size.z, color);
}
void DrawCircle3D(Vector3 center, float radius, Vector3 rotationAxis, float rotationAngle, Color color) {
    const int segments = 36;
    Matrix rot = MatrixRotate(rotationAxis, rotationAngle * DEG2RAD);
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2 * PI * i / segments, a1 = 2 * PI * (i + 1) / segments;
        Vector3 p0 = Vector3Transform({std::cos(a0) * radius, std::sin(a0) * radius, 0}, rot);
        Vector3 p1 = Vector3Transform({std::cos(a1) * radius, std::sin(a1) * radius, 0}, rot);
        Line3(Vector3Add(center, p0), Vector3Add(center, p1), color);
    }
}
void DrawCylinderEx(Vector3 startPos, Vector3 endPos, float startRadius, float endRadius, int sides, Color color) {
    if (sides < 3) sides = 8;
    Vector3 dir = Vector3Normalize(Vector3Subtract(endPos, startPos));
    // Build an orthonormal basis around the axis.
    Vector3 up = std::fabs(dir.y) < 0.99f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, up));
    Vector3 fwd = Vector3CrossProduct(right, dir);
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
        auto ring = [&](Vector3 c, float r, float a) {
            return Vector3Add(c, Vector3Add(Vector3Scale(right, std::cos(a) * r), Vector3Scale(fwd, std::sin(a) * r)));
        };
        Vector3 b0 = ring(startPos, startRadius, a0), b1 = ring(startPos, startRadius, a1);
        Vector3 t0 = ring(endPos, endRadius, a0), t1 = ring(endPos, endRadius, a1);
        Tri3(b0, b1, t1, color);
        Tri3(b0, t1, t0, color);
    }
}
void DrawCylinderWires(Vector3 position, float radiusTop, float radiusBottom, float height, int slices, Color color) {
    if (slices < 3) slices = 16;
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2 * PI * i / slices, a1 = 2 * PI * (i + 1) / slices;
        Vector3 b0 = {position.x + radiusBottom * std::cos(a0), position.y, position.z + radiusBottom * std::sin(a0)};
        Vector3 b1 = {position.x + radiusBottom * std::cos(a1), position.y, position.z + radiusBottom * std::sin(a1)};
        Vector3 t0 = {position.x + radiusTop * std::cos(a0), position.y + height, position.z + radiusTop * std::sin(a0)};
        Line3(b0, b1, color); Line3(b0, t0, color);
    }
}
void DrawCylinderWiresEx(Vector3 startPos, Vector3 endPos, float startRadius, float endRadius, int sides, Color color) {
    if (sides < 3) sides = 8;
    Vector3 dir = Vector3Normalize(Vector3Subtract(endPos, startPos));
    Vector3 up = std::fabs(dir.y) < 0.99f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, up));
    Vector3 fwd = Vector3CrossProduct(right, dir);
    auto ring = [&](Vector3 c, float r, float a) {
        return Vector3Add(c, Vector3Add(Vector3Scale(right, std::cos(a) * r), Vector3Scale(fwd, std::sin(a) * r)));
    };
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
        Line3(ring(startPos, startRadius, a0), ring(startPos, startRadius, a1), color);
        Line3(ring(endPos, endRadius, a0), ring(endPos, endRadius, a1), color);
        Line3(ring(startPos, startRadius, a0), ring(endPos, endRadius, a0), color);
    }
}
void DrawCapsule(Vector3 startPos, Vector3 endPos, float radius, int slices, int rings, Color color) {
    // Cylinder body + hemisphere caps (approximated with spheres at the ends).
    DrawCylinderEx(startPos, endPos, radius, radius, slices < 3 ? 8 : slices, color);
    DrawSphereEx(startPos, radius, rings < 3 ? 8 : rings, slices < 3 ? 8 : slices, color);
    DrawSphereEx(endPos, radius, rings < 3 ? 8 : rings, slices < 3 ? 8 : slices, color);
}
void DrawCapsuleWires(Vector3 startPos, Vector3 endPos, float radius, int slices, int rings, Color color) {
    DrawCylinderWiresEx(startPos, endPos, radius, radius, slices < 3 ? 8 : slices, color);
    DrawSphereWires(startPos, radius, rings < 3 ? 8 : rings, slices < 3 ? 8 : slices, color);
    DrawSphereWires(endPos, radius, rings < 3 ? 8 : rings, slices < 3 ? 8 : slices, color);
}
void DrawTriangleStrip3D(const Vector3* points, int pointCount, Color color) {
    if (!points || pointCount < 3) return;
    for (int i = 2; i < pointCount; ++i) {
        if (i % 2 == 0) Tri3(points[i - 2], points[i - 1], points[i], color);
        else            Tri3(points[i - 1], points[i - 2], points[i], color);
    }
}

} // namespace meowyrender
