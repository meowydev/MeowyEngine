// meowyrender - src/modules/models.cpp
// Mesh generators, material/model management, and mesh/model drawing.
//
// Drawing tessellates mesh triangles into the shared immediate batch, applying
// the model transform on the CPU. This is backend-agnostic (works identically
// on GL/Metal/Vulkan). GPU instancing and skinning use backend-owned streams;
// UploadMesh remains optional because meshes do not retain a GPU cache.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace meowyrender {

using detail::SetBatchState;
using backend::DrawMode;

namespace {

unsigned int WhiteTex() {
    auto& s = detail::State();
    return s.backend ? s.backend->WhiteTexture() : 0;
}
Vector3 TransformNormal(Vector3 normal,Matrix m) {
    Vector3 a{m.m0,m.m1,m.m2},b{m.m4,m.m5,m.m6},c{m.m8,m.m9,m.m10};
    float determinant=Vector3DotProduct(a,Vector3CrossProduct(b,c));
    if(std::abs(determinant)<1e-8f) return normal;
    Vector3 result=Vector3Add(Vector3Add(Vector3Scale(Vector3CrossProduct(b,c),normal.x),Vector3Scale(Vector3CrossProduct(c,a),normal.y)),Vector3Scale(Vector3CrossProduct(a,b),normal.z));
    return Vector3Normalize(Vector3Scale(result,1/determinant));
}
// Forward declaration; defined in the drawing section below.
struct TessellationResult { Color tint; unsigned int textureId; };
TessellationResult TessellateMesh(std::vector<backend::Vertex>& out, Mesh mesh,
                                  Material material, Matrix transform, bool emitSkinWeights);

backend::Surface MakeSurface(Material material) {
    backend::Surface surface; surface.enabled=material.lighting; surface.light=detail::State().lighting;
    if(material.alphaMode==MaterialAlphaMode::Mask) surface.alphaCutoff=material.alphaCutoff;
    if(material.maps) {
        surface.metallic=material.maps[1].value; surface.roughness=material.maps[3].value;
        auto emission=material.maps[static_cast<int>(MaterialMapIndex::Emission)];
        surface.emission={emission.color.r/255.0f*emission.value,emission.color.g/255.0f*emission.value,emission.color.b/255.0f*emission.value};
        const int slots[]={1,3,2,4,5};
        for(int i=0;i<5;++i) {surface.maps[i]=material.maps[slots[i]].texture.id; if(surface.maps[i]) surface.mask|=1u<<i;}
    }
    return surface;
}

// Allocate the vertex/normal/texcoord arrays for a mesh with N triangles
// (3N vertices), no shared indexing (simpler for immediate tessellation).
Mesh AllocMesh(int triangleCount) {
    Mesh m;
    m.triangleCount = triangleCount;
    m.vertexCount = triangleCount * 3;
    m.vertices = static_cast<float*>(std::calloc(m.vertexCount * 3, sizeof(float)));
    m.texcoords = static_cast<float*>(std::calloc(m.vertexCount * 2, sizeof(float)));
    m.normals = static_cast<float*>(std::calloc(m.vertexCount * 3, sizeof(float)));
    return m;
}

// Writer to fill mesh arrays sequentially.
struct MeshWriter {
    Mesh& m;
    int v = 0;
    void push(Vector3 p, Vector2 uv, Vector3 n) {
        m.vertices[v * 3 + 0] = p.x; m.vertices[v * 3 + 1] = p.y; m.vertices[v * 3 + 2] = p.z;
        m.texcoords[v * 2 + 0] = uv.x; m.texcoords[v * 2 + 1] = uv.y;
        m.normals[v * 3 + 0] = n.x; m.normals[v * 3 + 1] = n.y; m.normals[v * 3 + 2] = n.z;
        ++v;
    }
};

Vector3 GetVertex(const Mesh& m, int i) {
    Vector3 original{m.vertices[i*3],m.vertices[i*3+1],m.vertices[i*3+2]};
    if(!m.boneIds || !m.boneWeights || !m.boneMatrices || m.boneCount<=0) return original;
    Vector3 result{}; float sum=0;
    for(int c=0;c<4;++c) {
        int bone=m.boneIds[i*4+c]; float weight=m.boneWeights[i*4+c];
        if(bone>=m.boneCount || weight<=0) continue;
        result=Vector3Add(result,Vector3Scale(Vector3Transform(original,m.boneMatrices[bone]),weight)); sum+=weight;
    }
    return sum>0?Vector3Scale(result,1/sum):original;
}

} // namespace

// ===========================================================================
// Mesh generators
// ===========================================================================
Mesh GenMeshCube(float width, float height, float length) {
    Mesh m = AllocMesh(12); // 6 faces * 2 tris
    MeshWriter w{m};
    const float x = width / 2, y = height / 2, z = length / 2;
    struct Face { Vector3 a, b, c, d, n; };
    const Face faces[6] = {
        {{-x,-y, z},{ x,-y, z},{ x, y, z},{-x, y, z},{0,0,1}},   // front
        {{ x,-y,-z},{-x,-y,-z},{-x, y,-z},{ x, y,-z},{0,0,-1}},  // back
        {{-x,-y,-z},{-x,-y, z},{-x, y, z},{-x, y,-z},{-1,0,0}},  // left
        {{ x,-y, z},{ x,-y,-z},{ x, y,-z},{ x, y, z},{1,0,0}},   // right
        {{-x, y, z},{ x, y, z},{ x, y,-z},{-x, y,-z},{0,1,0}},   // top
        {{-x,-y,-z},{ x,-y,-z},{ x,-y, z},{-x,-y, z},{0,-1,0}},  // bottom
    };
    for (const Face& f : faces) {
        w.push(f.a, {0,0}, f.n); w.push(f.b, {1,0}, f.n); w.push(f.c, {1,1}, f.n);
        w.push(f.a, {0,0}, f.n); w.push(f.c, {1,1}, f.n); w.push(f.d, {0,1}, f.n);
    }
    return m;
}

Mesh GenMeshPlane(float width, float length, int resX, int resZ) {
    if (resX < 1) resX = 1;
    if (resZ < 1) resZ = 1;
    Mesh m = AllocMesh(resX * resZ * 2);
    MeshWriter w{m};
    const float halfW = width / 2, halfL = length / 2;
    for (int z = 0; z < resZ; ++z) {
        for (int x = 0; x < resX; ++x) {
            const float x0 = -halfW + width * x / resX;
            const float x1 = -halfW + width * (x + 1) / resX;
            const float z0 = -halfL + length * z / resZ;
            const float z1 = -halfL + length * (z + 1) / resZ;
            Vector3 n{0, 1, 0};
            w.push({x0,0,z0},{0,0},n); w.push({x0,0,z1},{0,1},n); w.push({x1,0,z1},{1,1},n);
            w.push({x0,0,z0},{0,0},n); w.push({x1,0,z1},{1,1},n); w.push({x1,0,z0},{1,0},n);
        }
    }
    return m;
}

Mesh GenMeshSphere(float radius, int rings, int slices) {
    if (rings < 3) rings = 16;
    if (slices < 3) slices = 16;
    Mesh m = AllocMesh(rings * slices * 2);
    MeshWriter w{m};
    for (int i = 0; i < rings; ++i) {
        const float lat0 = PI * (-0.5f + static_cast<float>(i) / rings);
        const float lat1 = PI * (-0.5f + static_cast<float>(i + 1) / rings);
        for (int j = 0; j < slices; ++j) {
            const float lng0 = 2 * PI * static_cast<float>(j) / slices;
            const float lng1 = 2 * PI * static_cast<float>(j + 1) / slices;
            auto pt = [&](float lat, float lng) -> Vector3 {
                return {radius * std::cos(lat) * std::cos(lng),
                        radius * std::sin(lat),
                        radius * std::cos(lat) * std::sin(lng)};
            };
            Vector3 a = pt(lat0, lng0), b = pt(lat1, lng0);
            Vector3 c = pt(lat1, lng1), d = pt(lat0, lng1);
            w.push(a, {0,0}, Vector3Normalize(a)); w.push(b, {0,1}, Vector3Normalize(b)); w.push(c, {1,1}, Vector3Normalize(c));
            w.push(a, {0,0}, Vector3Normalize(a)); w.push(c, {1,1}, Vector3Normalize(c)); w.push(d, {1,0}, Vector3Normalize(d));
        }
    }
    return m;
}

Mesh GenMeshCylinder(float radius, float height, int slices) {
    if (slices < 3) slices = 16;
    Mesh m = AllocMesh(slices * 2);
    MeshWriter w{m};
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2 * PI * i / slices;
        const float a1 = 2 * PI * (i + 1) / slices;
        Vector3 b0{radius*std::cos(a0), 0, radius*std::sin(a0)};
        Vector3 b1{radius*std::cos(a1), 0, radius*std::sin(a1)};
        Vector3 t0{radius*std::cos(a0), height, radius*std::sin(a0)};
        Vector3 t1{radius*std::cos(a1), height, radius*std::sin(a1)};
        Vector3 n0 = Vector3Normalize({std::cos(a0),0,std::sin(a0)});
        w.push(b0,{0,0},n0); w.push(b1,{1,0},n0); w.push(t1,{1,1},n0);
        w.push(b0,{0,0},n0); w.push(t1,{1,1},n0); w.push(t0,{0,1},n0);
    }
    return m;
}

Mesh GenMeshTorus(float radius, float size, int radSeg, int sides) {
    if (radSeg < 3) radSeg = 16;
    if (sides < 3) sides = 16;
    Mesh m = AllocMesh(radSeg * sides * 2);
    MeshWriter w{m};
    auto pt = [&](int i, int j) -> Vector3 {
        const float u = 2 * PI * i / radSeg;
        const float v = 2 * PI * j / sides;
        const float cx = (radius + size * std::cos(v)) * std::cos(u);
        const float cy = size * std::sin(v);
        const float cz = (radius + size * std::cos(v)) * std::sin(u);
        return {cx, cy, cz};
    };
    for (int i = 0; i < radSeg; ++i)
        for (int j = 0; j < sides; ++j) {
            Vector3 a = pt(i, j), b = pt(i + 1, j);
            Vector3 c = pt(i + 1, j + 1), d = pt(i, j + 1);
            w.push(a,{0,0},Vector3Normalize(a)); w.push(b,{1,0},Vector3Normalize(b)); w.push(c,{1,1},Vector3Normalize(c));
            w.push(a,{0,0},Vector3Normalize(a)); w.push(c,{1,1},Vector3Normalize(c)); w.push(d,{0,1},Vector3Normalize(d));
        }
    return m;
}

Mesh GenMeshCone(float radius, float height, int slices) {
    if (slices < 3) slices = 16;
    Mesh m = AllocMesh(slices * 2);   // side triangle + base triangle per slice
    MeshWriter w{m};
    const Vector3 apex{0, height, 0};
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2 * PI * i / slices, a1 = 2 * PI * (i + 1) / slices;
        Vector3 b0{radius * std::cos(a0), 0, radius * std::sin(a0)};
        Vector3 b1{radius * std::cos(a1), 0, radius * std::sin(a1)};
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b1, b0), Vector3Subtract(apex, b0)));
        w.push(b0, {0, 0}, n); w.push(b1, {1, 0}, n); w.push(apex, {0.5f, 1}, n);
        // Base
        w.push(b1, {1, 0}, {0, -1, 0}); w.push(b0, {0, 0}, {0, -1, 0}); w.push({0, 0, 0}, {0.5f, 0.5f}, {0, -1, 0});
    }
    return m;
}

Mesh GenMeshHemiSphere(float radius, int rings, int slices) {
    if (rings < 3) rings = 8;
    if (slices < 3) slices = 16;
    Mesh m = AllocMesh(rings * slices * 2);
    MeshWriter w{m};
    for (int i = 0; i < rings; ++i) {
        const float lat0 = (PI * 0.5f) * (static_cast<float>(i) / rings);
        const float lat1 = (PI * 0.5f) * (static_cast<float>(i + 1) / rings);
        for (int j = 0; j < slices; ++j) {
            const float lng0 = 2 * PI * j / slices, lng1 = 2 * PI * (j + 1) / slices;
            auto pt = [&](float lat, float lng) -> Vector3 {
                return {radius * std::cos(lat) * std::cos(lng), radius * std::sin(lat), radius * std::cos(lat) * std::sin(lng)};
            };
            Vector3 a = pt(lat0, lng0), b = pt(lat1, lng0), c = pt(lat1, lng1), d = pt(lat0, lng1);
            w.push(a, {0,0}, Vector3Normalize(a)); w.push(b, {0,1}, Vector3Normalize(b)); w.push(c, {1,1}, Vector3Normalize(c));
            w.push(a, {0,0}, Vector3Normalize(a)); w.push(c, {1,1}, Vector3Normalize(c)); w.push(d, {1,0}, Vector3Normalize(d));
        }
    }
    return m;
}

Mesh GenMeshKnot(float radius, float size, int radSeg, int sides) {
    // Torus knot (p=2,q=3). Sweep a tube around the knot centerline.
    if (radSeg < 3) radSeg = 32;
    if (sides < 3) sides = 12;
    Mesh m = AllocMesh(radSeg * sides * 2);
    MeshWriter w{m};
    const int p = 2, q = 3;
    auto center = [&](float t) -> Vector3 {
        const float r = radius * (2.0f + std::cos(q * t)) * 0.5f;
        return {r * std::cos(p * t), r * std::sin(p * t), radius * std::sin(q * t) * 0.5f};
    };
    auto ring = [&](int i, int j) -> Vector3 {
        const float t = 2 * PI * i / radSeg;
        Vector3 c0 = center(t), c1 = center(t + 0.01f);
        Vector3 tangent = Vector3Normalize(Vector3Subtract(c1, c0));
        Vector3 up = std::fabs(tangent.y) < 0.99f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        Vector3 nrm = Vector3Normalize(Vector3CrossProduct(tangent, up));
        Vector3 bnrm = Vector3CrossProduct(tangent, nrm);
        const float a = 2 * PI * j / sides;
        return Vector3Add(c0, Vector3Add(Vector3Scale(nrm, std::cos(a) * size), Vector3Scale(bnrm, std::sin(a) * size)));
    };
    for (int i = 0; i < radSeg; ++i)
        for (int j = 0; j < sides; ++j) {
            Vector3 a = ring(i, j), b = ring(i + 1, j), c = ring(i + 1, j + 1), d = ring(i, j + 1);
            w.push(a, {0,0}, Vector3Normalize(a)); w.push(b, {1,0}, Vector3Normalize(b)); w.push(c, {1,1}, Vector3Normalize(c));
            w.push(a, {0,0}, Vector3Normalize(a)); w.push(c, {1,1}, Vector3Normalize(c)); w.push(d, {0,1}, Vector3Normalize(d));
        }
    return m;
}

Mesh GenMeshPoly(int sides, float radius) {
    if (sides < 3) sides = 3;
    Mesh m = AllocMesh(sides);   // fan of triangles from center
    MeshWriter w{m};
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2 * PI * i / sides, a1 = 2 * PI * (i + 1) / sides;
        w.push({0, 0, 0}, {0.5f, 0.5f}, {0, 1, 0});
        w.push({radius * std::cos(a0), 0, radius * std::sin(a0)}, {0, 0}, {0, 1, 0});
        w.push({radius * std::cos(a1), 0, radius * std::sin(a1)}, {1, 0}, {0, 1, 0});
    }
    return m;
}

Mesh GenMeshHeightmap(Image heightmap, Vector3 size) {
    const int w = heightmap.width, h = heightmap.height;
    if (w < 2 || h < 2) return {};
    Mesh m = AllocMesh((w - 1) * (h - 1) * 2);
    MeshWriter mw{m};
    auto height = [&](int x, int z) -> float {
        Color c = GetImageColor(heightmap, std::min(x, w - 1), std::min(z, h - 1));
        return (c.r / 255.0f) * size.y;
    };
    for (int z = 0; z < h - 1; ++z)
        for (int x = 0; x < w - 1; ++x) {
            auto vtx = [&](int px, int pz) -> Vector3 {
                return {(static_cast<float>(px) / (w - 1) - 0.5f) * size.x, height(px, pz),
                        (static_cast<float>(pz) / (h - 1) - 0.5f) * size.z};
            };
            Vector3 a = vtx(x, z), b = vtx(x, z + 1), c = vtx(x + 1, z + 1), d = vtx(x + 1, z);
            Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(d, a)));
            mw.push(a, {0,0}, n); mw.push(b, {0,1}, n); mw.push(c, {1,1}, n);
            mw.push(a, {0,0}, n); mw.push(c, {1,1}, n); mw.push(d, {1,0}, n);
        }
    return m;
}

Mesh GenMeshCubicmap(Image cubicmap, Vector3 cubeSize) {
    // Emit a unit cube (top+sides) for every non-black pixel in the map.
    const int w = cubicmap.width, h = cubicmap.height;
    if (w < 1 || h < 1) return {};
    std::vector<std::pair<int,int>> cells;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            Color c = GetImageColor(cubicmap, x, y);
            if (c.r > 128 || c.g > 128 || c.b > 128) cells.emplace_back(x, y);
        }
    if (cells.empty()) return {};
    Mesh m = AllocMesh(static_cast<int>(cells.size()) * 12); // full cube per cell
    MeshWriter mw{m};
    for (auto [cx, cz] : cells) {
        const float x = cx * cubeSize.x, z = cz * cubeSize.z;
        const float wd = cubeSize.x, hg = cubeSize.y, ln = cubeSize.z;
        // 8 corners
        Vector3 p[8] = {
            {x, 0, z}, {x + wd, 0, z}, {x + wd, 0, z + ln}, {x, 0, z + ln},
            {x, hg, z}, {x + wd, hg, z}, {x + wd, hg, z + ln}, {x, hg, z + ln}};
        auto quad = [&](int a, int b, int c, int d, Vector3 n) {
            mw.push(p[a], {0,0}, n); mw.push(p[b], {1,0}, n); mw.push(p[c], {1,1}, n);
            mw.push(p[a], {0,0}, n); mw.push(p[c], {1,1}, n); mw.push(p[d], {0,1}, n);
        };
        quad(4,5,6,7,{0,1,0}); quad(0,3,2,1,{0,-1,0}); quad(0,1,5,4,{0,0,-1});
        quad(2,3,7,6,{0,0,1}); quad(1,2,6,5,{1,0,0}); quad(3,0,4,7,{-1,0,0});
    }
    return m;
}

void GenMeshTangents(Mesh* mesh) {
    if (!mesh || !mesh->vertices || !mesh->texcoords || !mesh->normals) return;
    if (!mesh->tangents) mesh->tangents = static_cast<float*>(std::calloc(mesh->vertexCount * 4, sizeof(float)));
    // Per-triangle tangents accumulated to vertices (no index sharing here).
    for (int t = 0; t < mesh->triangleCount; ++t) {
        const int i0 = t * 3, i1 = t * 3 + 1, i2 = t * 3 + 2;
        auto pos = [&](int i){ return Vector3{mesh->vertices[i*3], mesh->vertices[i*3+1], mesh->vertices[i*3+2]}; };
        auto uv = [&](int i){ return Vector2{mesh->texcoords[i*2], mesh->texcoords[i*2+1]}; };
        Vector3 e1 = Vector3Subtract(pos(i1), pos(i0)), e2 = Vector3Subtract(pos(i2), pos(i0));
        Vector2 d1 = {uv(i1).x - uv(i0).x, uv(i1).y - uv(i0).y};
        Vector2 d2 = {uv(i2).x - uv(i0).x, uv(i2).y - uv(i0).y};
        const float denom = d1.x * d2.y - d2.x * d1.y;
        const float f = std::fabs(denom) < 1e-8f ? 0.0f : 1.0f / denom;
        Vector3 tan = Vector3Scale(Vector3Subtract(Vector3Scale(e1, d2.y), Vector3Scale(e2, d1.y)), f);
        tan = Vector3Normalize(tan);
        for (int k : {i0, i1, i2}) {
            mesh->tangents[k*4+0] = tan.x; mesh->tangents[k*4+1] = tan.y;
            mesh->tangents[k*4+2] = tan.z; mesh->tangents[k*4+3] = 1.0f;
        }
    }
}

void SetMeshMorphWeights(Mesh* mesh, const float* weights, int count) {
    if (!mesh || !weights || mesh->morphTargetCount <= 0) return;
    if (!mesh->morphWeights) mesh->morphWeights = static_cast<float*>(std::calloc(mesh->morphTargetCount, sizeof(float)));
    const int n = std::min(count, mesh->morphTargetCount);
    for (int i = 0; i < n; ++i) mesh->morphWeights[i] = weights[i];
}
int GetMeshMorphTargetCount(Mesh mesh) { return mesh.morphTargetCount; }

void UploadMesh(Mesh* mesh, bool /*dynamic*/) {
    // Persist the mesh's base geometry in a GPU-resident buffer so meshes drawn
    // every frame are not re-tessellated/re-streamed. The persistent path is
    // used for static (non-skinned) meshes drawn with a white albedo tint; the
    // per-draw model transform and instance transforms are applied by the
    // shader. Skinned/animated meshes keep the streaming path (their bind pose
    // changes each frame). Backends without a mesh cache leave vaoId at 0.
    if (!mesh || !mesh->vertices || mesh->vertexCount <= 0 || mesh->triangleCount <= 0) return;
    auto& state = detail::State();
    if (!state.backend || !state.backend->SupportsPersistentMesh()) { mesh->vaoId = 0; return; }
    if (mesh->boneCount > 0) { mesh->vaoId = 0; return; } // skinned meshes stream
    if (mesh->morphTargetCount > 0) { mesh->vaoId = 0; return; } // morphed meshes stream

    // Build the base interleaved vertex list once (white tint, world normals).
    Material white = LoadMaterialDefault();
    std::vector<backend::Vertex> verts;
    TessellateMesh(verts, *mesh, white, MatrixIdentity(), /*emitSkinWeights=*/false);
    UnloadMaterial(white);
    if (verts.empty()) { mesh->vaoId = 0; return; }
    mesh->vaoId = state.backend->UploadMeshBuffer(verts.data(), verts.size());
}

void UnloadMesh(Mesh mesh) {
    auto& state = detail::State();
    if (mesh.vaoId && state.backend) state.backend->DestroyMeshBuffer(mesh.vaoId);
    std::free(mesh.vertices);
    std::free(mesh.texcoords);
    std::free(mesh.texcoords2);
    std::free(mesh.normals);
    std::free(mesh.tangents);
    std::free(mesh.morphPositions);
    std::free(mesh.morphNormals);
    std::free(mesh.morphWeights);
    std::free(mesh.colors);
    std::free(mesh.indices);
    delete[] mesh.boneIds; delete[] mesh.boneWeights; delete[] mesh.boneNodes;
    delete[] mesh.inverseBindMatrices; delete[] mesh.boneMatrices;
}

namespace {
// The persistent GPU buffer bakes a white per-vertex tint; only take that path
// when the material albedo tint is opaque white so per-draw tinting stays
// correct (raylib DrawModel tint). Skinned meshes always stream.
bool CanUsePersistentMesh(const Mesh& mesh, const Material& material) {
    if (mesh.vaoId == 0 || mesh.boneCount > 0) return false;
    // Morph-target meshes deform per-frame on the CPU; always stream them.
    if (mesh.morphTargetCount > 0) return false;
    // Masked materials need the surface (alpha cutoff) bound; use streaming.
    if (material.alphaMode == MaterialAlphaMode::Mask) return false;
    // The persistent buffer bakes TEXCOORD_0; secondary-UV maps must stream so
    // the sampled UV set/transform is honored.
    if (material.maps)
        for (int s = 0; s < 6; ++s)
            if (material.maps[s].uvSet != 0 ||
                material.maps[s].uvOffset.x != 0 || material.maps[s].uvOffset.y != 0 ||
                material.maps[s].uvScale.x != 1 || material.maps[s].uvScale.y != 1 ||
                material.maps[s].uvRotation != 0) return false;
    if (!material.maps) return true;
    Color tint = material.maps[static_cast<int>(MaterialMapIndex::Albedo)].color;
    return tint.r==255 && tint.g==255 && tint.b==255 && tint.a==255;
}
} // namespace

BoundingBox GetMeshBoundingBox(Mesh mesh) {
    BoundingBox box{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (int i = 0; i < mesh.vertexCount; ++i) {
        Vector3 v = GetVertex(mesh, i);
        box.min.x = std::min(box.min.x, v.x); box.max.x = std::max(box.max.x, v.x);
        box.min.y = std::min(box.min.y, v.y); box.max.y = std::max(box.max.y, v.y);
        box.min.z = std::min(box.min.z, v.z); box.max.z = std::max(box.max.z, v.z);
    }
    return box;
}

// ===========================================================================
// Materials
// ===========================================================================
Material LoadMaterialDefault() {
    Material mat;
    mat.maps = new MaterialMap[MaxMaterialMaps];
    for (int i = 0; i < MaxMaterialMaps; ++i) mat.maps[i] = MaterialMap{};
    mat.maps[static_cast<int>(MaterialMapIndex::Albedo)].color = WHITE;
    mat.maps[static_cast<int>(MaterialMapIndex::Roughness)].value=0.5f;
    mat.maps[static_cast<int>(MaterialMapIndex::Emission)].color=BLACK;
    mat.maps[static_cast<int>(MaterialMapIndex::Emission)].value=1.0f;
    return mat;
}
void SetAmbientLight(Color color,float intensity) {
    detail::FlushBatch(); intensity=std::max(0.0f,intensity);
    detail::State().lighting.ambient={color.r/255.0f*intensity,color.g/255.0f*intensity,color.b/255.0f*intensity};
}
void SetDirectionalLight(Vector3 direction,Color color,float intensity) {
    detail::FlushBatch(); intensity=std::max(0.0f,intensity);
    auto& light=detail::State().lighting; light.direction=Vector3Normalize(direction);
    light.radiance={color.r/255.0f*intensity,color.g/255.0f*intensity,color.b/255.0f*intensity};
}
void SetEnvironmentLight(TextureCubemap cubemap,float intensity) {
    detail::FlushBatch();
    auto& light=detail::State().lighting;
    light.environment=cubemap.id;
    light.environmentIntensity=std::max(0.0f,intensity);
    // Reflection blur uses the cubemap's available mip levels; the runtime path
    // does not use precomputed maps, so clear them.
    light.environmentMips=std::max(1,cubemap.mipmaps);
    light.irradiance=0; light.prefilter=0; light.brdfLut=0;
}
void ClearEnvironmentLight() {
    detail::FlushBatch();
    detail::State().lighting.irradiance=0;
    detail::State().lighting.prefilter=0;
    detail::State().lighting.brdfLut=0;
    detail::State().lighting.environment=0;
}

void UnloadMaterial(Material material) {
    delete[] material.maps;
}

void SetMaterialTexture(Material* material, MaterialMapIndex mapType, Texture2D texture) {
    if (material && material->maps)
        material->maps[static_cast<int>(mapType)].texture = texture;
}

// ===========================================================================
// Models
// ===========================================================================
Model LoadModelFromMesh(Mesh mesh) {
    Model model;
    model.transform = MatrixIdentity();
    model.meshCount = 1;
    model.materialCount = 1;
    model.meshes = new Mesh[1]{mesh};
    model.materials = new Material[1]{LoadMaterialDefault()};
    model.meshMaterial = new int[1]{0};
    return model;
}

bool IsModelValid(Model model) { return model.meshCount > 0 && model.meshes != nullptr; }

void UnloadModel(Model model) {
    for(int i=0;model.ownedTextures&&i<model.ownedTextureCount;++i) UnloadTexture(model.ownedTextures[i]);
    delete[] model.ownedTextures;
    for (int i = 0; model.meshes && i < model.meshCount; ++i) UnloadMesh(model.meshes[i]);
    for (int i = 0; model.materials && i < model.materialCount; ++i) UnloadMaterial(model.materials[i]);
    delete[] model.meshes;
    delete[] model.materials;
    delete[] model.meshMaterial;
    delete[] model.bones; delete[] model.bindPose; delete[] model.bindMatrices;
}

BoundingBox GetModelBoundingBox(Model model) {
    if (model.meshCount == 0) return {};
    BoundingBox box = GetMeshBoundingBox(model.meshes[0]);
    for (int i = 1; i < model.meshCount; ++i) {
        BoundingBox b = GetMeshBoundingBox(model.meshes[i]);
        box.min.x = std::min(box.min.x, b.min.x); box.max.x = std::max(box.max.x, b.max.x);
        box.min.y = std::min(box.min.y, b.min.y); box.max.y = std::max(box.max.y, b.max.y);
        box.min.z = std::min(box.min.z, b.min.z); box.max.z = std::max(box.max.z, b.max.z);
    }
    return box;
}

void SetModelMaterialTexture(Model* model, int materialIndex,
                             MaterialMapIndex mapType, Texture2D texture) {
    if (model && materialIndex < model->materialCount)
        SetMaterialTexture(&model->materials[materialIndex], mapType, texture);
}

// ===========================================================================
// Drawing (immediate tessellation into the batch)
// ===========================================================================
namespace {

// Tessellate a mesh into a flat backend::Vertex list.
//   transform      : applied on the CPU to positions/normals (identity when the
//                    GPU will apply it, e.g. instanced or GPU-skinned draws).
//   emitSkinWeights : write joint/weight attributes for GPU skinning.
// Returns the tint + texture derived from the material albedo map.
TessellationResult TessellateMesh(std::vector<backend::Vertex>& out, Mesh mesh,
                                  Material material, Matrix transform,
                                  bool emitSkinWeights) {
    Color tint = WHITE;
    unsigned int texId = WhiteTex();
    const MaterialMap* albedo = material.maps ? &material.maps[static_cast<int>(MaterialMapIndex::Albedo)] : nullptr;
    if (albedo) {
        tint = albedo->color;
        if (albedo->texture.id) texId = albedo->texture.id;
    }
    // Choose the UV set + KHR_texture_transform of the sampled (albedo) map so
    // the built-in shader samples the same coordinates the asset intends.
    const float* uvArray = mesh.texcoords;
    Vector2 uvOffset{0,0}, uvScale{1,1}; float uvRot = 0.0f;
    if (albedo) {
        if (albedo->uvSet == 1 && mesh.texcoords2) uvArray = mesh.texcoords2;
        uvOffset = albedo->uvOffset; uvScale = albedo->uvScale; uvRot = albedo->uvRotation;
    }
    const float cosR = std::cos(uvRot), sinR = std::sin(uvRot);
    const bool cpuTransform = !emitSkinWeights;
    const int count = mesh.indices ? mesh.triangleCount * 3
                                   : std::min(mesh.vertexCount, mesh.triangleCount * 3);
    out.reserve(out.size() + static_cast<std::size_t>(count));
    for (int element = 0; element < count; ++element) {
        const int i = mesh.indices ? mesh.indices[element] : element;
        if (i >= mesh.vertexCount) continue;
        Vector3 v{mesh.vertices[i*3], mesh.vertices[i*3+1], mesh.vertices[i*3+2]};
        // Apply glTF morph targets: p' = p + sum(weight_t * delta_t). Deltas are
        // stored per emitted vertex in target-major order.
        if (mesh.morphTargetCount > 0 && mesh.morphPositions && mesh.morphWeights) {
            for (int t = 0; t < mesh.morphTargetCount; ++t) {
                const float w = mesh.morphWeights[t];
                if (w == 0.0f) continue;
                const std::size_t off = (static_cast<std::size_t>(t)*mesh.vertexCount + i)*3;
                v.x += w*mesh.morphPositions[off]; v.y += w*mesh.morphPositions[off+1]; v.z += w*mesh.morphPositions[off+2];
            }
        }
        Vector3 tv = cpuTransform ? Vector3Transform(v, transform) : v;
        float u = uvArray ? uvArray[i * 2 + 0] : 0.0f;
        float t = uvArray ? uvArray[i * 2 + 1] : 0.0f;
        // Apply KHR_texture_transform: scale, rotate, then offset in UV space.
        if (uvRot != 0.0f || uvScale.x != 1.0f || uvScale.y != 1.0f ||
            uvOffset.x != 0.0f || uvOffset.y != 0.0f) {
            const float su = u * uvScale.x, sv = t * uvScale.y;
            u = cosR * su - sinR * sv + uvOffset.x;
            t = sinR * su + cosR * sv + uvOffset.y;
        }
        Color color = tint;
        if (mesh.colors) {
            color = {static_cast<unsigned char>(tint.r * mesh.colors[i*4] / 255),
                     static_cast<unsigned char>(tint.g * mesh.colors[i*4+1] / 255),
                     static_cast<unsigned char>(tint.b * mesh.colors[i*4+2] / 255),
                     static_cast<unsigned char>(tint.a * mesh.colors[i*4+3] / 255)};
        }
        out.push_back(backend::Vertex{tv.x, tv.y, tv.z, u, t,
                                      color.r, color.g, color.b, color.a});
        if (emitSkinWeights && mesh.boneIds && mesh.boneWeights) {
            auto& vertex = out.back();
            for (int c = 0; c < 4; ++c)
                if (mesh.boneIds[i*4+c] < mesh.boneCount) {
                    vertex.joints[c] = mesh.boneIds[i*4+c];
                    vertex.weights[c] = std::max(0.0f, mesh.boneWeights[i*4+c]);
                }
        }
        if (mesh.normals) {
            Vector3 normal{mesh.normals[i*3], mesh.normals[i*3+1], mesh.normals[i*3+2]};
            if (mesh.morphTargetCount > 0 && mesh.morphNormals && mesh.morphWeights) {
                for (int t = 0; t < mesh.morphTargetCount; ++t) {
                    const float w = mesh.morphWeights[t];
                    if (w == 0.0f) continue;
                    const std::size_t off = (static_cast<std::size_t>(t)*mesh.vertexCount + i)*3;
                    normal.x += w*mesh.morphNormals[off]; normal.y += w*mesh.morphNormals[off+1]; normal.z += w*mesh.morphNormals[off+2];
                }
                normal = Vector3Normalize(normal);
            }
            if (cpuTransform) {
                if (mesh.boneCount>0 && mesh.boneMatrices && mesh.boneWeights && mesh.boneIds) {
                    Vector3 skinned{};
                    for (int c = 0; c < 4; ++c)
                        if (mesh.boneIds[i*4+c] < mesh.boneCount)
                            skinned = Vector3Add(skinned, Vector3Scale(
                                TransformNormal(normal, mesh.boneMatrices[mesh.boneIds[i*4+c]]),
                                mesh.boneWeights[i*4+c]));
                    normal = skinned;
                }
                normal = TransformNormal(normal, transform);
            }
            out.back().nx = normal.x; out.back().ny = normal.y; out.back().nz = normal.z;
        }
    }
    return {tint, texId};
}

} // namespace

// Transparent (BLEND) draw queue. Inside a 3D pass, BLEND meshes are deferred
// and flushed back-to-front at EndMode3D so overlapping transparency composites
// correctly. This is a per-draw center-distance sort (not per-triangle), which
// resolves inter-object ordering; self-overlapping concave transparent meshes
// still rely on submission order (documented limit).
namespace {
struct TransparentDraw { Mesh mesh; Material material; Matrix transform; float depth; };
std::vector<TransparentDraw> g_transparentQueue;
bool g_transparentDeferral = false; // true between BeginMode3D and EndMode3D
} // namespace
// Draws a mesh immediately (no transparent deferral). Defined below at namespace
// scope so DrawMesh and the deferral flush can both call it.
void DrawMeshImmediate(Mesh mesh, Material material, Matrix transform);

namespace detail {
void SetTransparentDeferral(bool on) { g_transparentDeferral = on; }
void FlushTransparentQueue() {
    if (g_transparentQueue.empty()) return;
    // Sort back-to-front (farthest first) by center distance from the eye.
    std::stable_sort(g_transparentQueue.begin(), g_transparentQueue.end(),
                     [](const TransparentDraw& a, const TransparentDraw& b){ return a.depth > b.depth; });
    auto& state = State();
    // Transparent geometry tests depth but does not write it.
    if (state.backend) state.backend->SetDepthMask(false);
    const bool wasDeferring = g_transparentDeferral;
    g_transparentDeferral = false; // draw immediately now
    for (auto& d : g_transparentQueue) DrawMeshImmediate(d.mesh, d.material, d.transform);
    detail::FlushBatch();
    if (state.backend) state.backend->SetDepthMask(true);
    g_transparentDeferral = wasDeferring;
    g_transparentQueue.clear();
}
} // namespace detail

void DrawMesh(Mesh mesh, Material material, Matrix transform) {
    if (!mesh.vertices || mesh.vertexCount <= 0 || mesh.triangleCount <= 0) return;
    // Defer BLEND meshes to the sorted transparent pass at EndMode3D.
    if (g_transparentDeferral && material.alphaMode == MaterialAlphaMode::Blend) {
        // Distance from the eye to the mesh center (in world space).
        Vector3 c{transform.m12, transform.m13, transform.m14};
        const Vector3 eye = detail::State().lighting.eye;
        const float depth = Vector3Length(Vector3Subtract(c, eye));
        g_transparentQueue.push_back({mesh, material, transform, depth});
        return;
    }
    DrawMeshImmediate(mesh, material, transform);
}

void DrawMeshImmediate(Mesh mesh, Material material, Matrix transform) {
    auto& state=detail::State();

    // Fast path: a previously uploaded static mesh draws straight from its
    // GPU-resident buffer with the model transform, skipping re-tessellation.
    if (CanUsePersistentMesh(mesh, material)) {
        detail::FlushBatch();
        if(material.lighting) {
            if(!state.backend->SupportsPBR()) throw std::runtime_error("PBR is unavailable on this backend");
            state.backend->SetSurface(MakeSurface(material));
        }
        unsigned int texId = WhiteTex();
        if (material.maps) {
            const auto& albedo = material.maps[static_cast<int>(MaterialMapIndex::Albedo)];
            if (albedo.texture.id) texId = albedo.texture.id;
        }
        const std::size_t count = mesh.indices ? static_cast<std::size_t>(mesh.triangleCount*3)
                                               : std::min<std::size_t>(mesh.vertexCount, mesh.triangleCount*3);
        if (state.backend->DrawMeshBuffer(mesh.vaoId, count, texId, &transform, 1)) {
            if(material.lighting) state.backend->SetSurface({});
            return;
        }
        // Backend declined (e.g. custom shader active); fall through to stream.
        if(material.lighting) state.backend->SetSurface({});
    }

    // Bind the surface when lighting is enabled OR the material is alpha-masked
    // (the cutoff lives in the surface uniforms and drives the shader discard).
    const bool needsSurface = material.lighting || material.alphaMode==MaterialAlphaMode::Mask;
    if(needsSurface) {
        if(material.lighting && (!state.backend || !state.backend->SupportsPBR())) throw std::runtime_error("PBR is unavailable on this backend");
        detail::FlushBatch(); if(state.backend) state.backend->SetSurface(MakeSurface(material));
    }
    const bool gpuSkin=mesh.boneCount>0 && mesh.boneIds && mesh.boneWeights && mesh.boneMatrices && state.backend && state.backend->SupportsGpuSkinning();
    if(gpuSkin) {
        detail::FlushBatch();
        std::vector<Matrix> palette(mesh.boneCount);
        for(int i=0;i<mesh.boneCount;++i) palette[i]=MatrixMultiply(mesh.boneMatrices[i],transform);
        state.backend->SetSkinning(palette.data(),mesh.boneCount);
    }
    // Select the batch texture/mode BEFORE appending (SetBatchState may flush).
    unsigned int texId = WhiteTex();
    if (material.maps) {
        const auto& albedo = material.maps[static_cast<int>(MaterialMapIndex::Albedo)];
        if (albedo.texture.id) texId = albedo.texture.id;
    }
    SetBatchState(DrawMode::Triangles, texId);
    auto& verts = detail::State().batch.verts;
    TessellateMesh(verts, mesh, material, transform, gpuSkin);
    if(gpuSkin) {detail::FlushBatch(); state.backend->SetSkinning(nullptr,0);}
    if(needsSurface) {detail::FlushBatch(); state.backend->SetSurface({});}
}

void DrawMeshInstanced(Mesh mesh, Material material, const Matrix* transforms, int instances) {
    if(!transforms || instances<=0 || !mesh.vertices) return;
    auto& state=detail::State();
    if(!state.backend) return;

    // Skinned meshes need a distinct pose per instance, which the shared
    // instance-transform buffer cannot express; fall back to individual draws.
    const bool skinned = mesh.boneCount>0 && mesh.boneIds && mesh.boneWeights && mesh.boneMatrices;
    if(skinned) { for(int i=0;i<instances;++i) DrawMesh(mesh,material,transforms[i]); return; }

    detail::FlushBatch();
    // Tessellate the base mesh once (no CPU transform); the shader applies each
    // instance transform to position and normal. Lit materials work here because
    // the backend applies the surface during the instanced draw.
    std::vector<backend::Vertex> vertices;
    auto result = TessellateMesh(vertices, mesh, material, MatrixIdentity(), true);
    if(vertices.empty()) return;

    if(material.lighting) {
        if(!state.backend->SupportsPBR()) throw std::runtime_error("PBR is unavailable on this backend");
        state.backend->SetSurface(MakeSurface(material));
    }
    detail::SubmitVertices(vertices.data(), vertices.size(), backend::DrawMode::Triangles,
                           result.textureId, transforms, instances);
    if(material.lighting) state.backend->SetSurface({});
}

void DrawModelEx(Model model, Vector3 position, Vector3 rotationAxis,
                 float rotationAngle, Vector3 scale, Color tint) {
    Matrix matScale = MatrixScale(scale.x, scale.y, scale.z);
    Matrix matRot = MatrixRotate(rotationAxis, rotationAngle * DEG2RAD);
    Matrix matTrans = MatrixTranslate(position.x, position.y, position.z);
    Matrix transform = MatrixMultiply(MatrixMultiply(matScale, matRot), matTrans);
    transform = MatrixMultiply(model.transform, transform);

    for (int i = 0; i < model.meshCount; ++i) {
        Material mat = model.materials[model.meshMaterial[i]];
        MaterialMap localMaps[MaxMaterialMaps];
        if (mat.maps) {
            std::copy_n(mat.maps, MaxMaterialMaps, localMaps);
            mat.maps = localMaps;
        }
        // Apply the tint on top of the material albedo color.
        if (mat.maps) {
            Color base = mat.maps[static_cast<int>(MaterialMapIndex::Albedo)].color;
            mat.maps[static_cast<int>(MaterialMapIndex::Albedo)].color =
                {static_cast<unsigned char>(base.r * tint.r / 255),
                 static_cast<unsigned char>(base.g * tint.g / 255),
                 static_cast<unsigned char>(base.b * tint.b / 255),
                 static_cast<unsigned char>(base.a * tint.a / 255)};
        }
        DrawMesh(model.meshes[i], mat, transform);
    }
}

void DrawModel(Model model, Vector3 position, float scale, Color tint) {
    DrawModelEx(model, position, {0, 1, 0}, 0.0f, {scale, scale, scale}, tint);
}

void DrawModelWires(Model model, Vector3 position, float scale, Color tint) {
    // Wireframe: draw triangle edges as lines.
    Matrix transform = MatrixMultiply(model.transform,
        MatrixMultiply(MatrixScale(scale, scale, scale),
                       MatrixTranslate(position.x, position.y, position.z)));
    for (int mi = 0; mi < model.meshCount; ++mi) {
        const Mesh& mesh = model.meshes[mi];
        SetBatchState(DrawMode::Lines, WhiteTex());
        auto& verts = detail::State().batch.verts;
        auto edge = [&](Vector3 a, Vector3 b) {
            Vector3 ta = Vector3Transform(a, transform);
            Vector3 tb = Vector3Transform(b, transform);
            verts.push_back(backend::Vertex{ta.x, ta.y, ta.z, 0, 0, tint.r, tint.g, tint.b, tint.a});
            verts.push_back(backend::Vertex{tb.x, tb.y, tb.z, 0, 0, tint.r, tint.g, tint.b, tint.a});
        };
        for (int i = 0; i < mesh.triangleCount; ++i) {
            Vector3 a = GetVertex(mesh, mesh.indices ? mesh.indices[i*3+0] : i*3+0);
            Vector3 b = GetVertex(mesh, mesh.indices ? mesh.indices[i*3+1] : i*3+1);
            Vector3 c = GetVertex(mesh, mesh.indices ? mesh.indices[i*3+2] : i*3+2);
            edge(a, b); edge(b, c); edge(c, a);
        }
    }
}

void DrawBoundingBox(BoundingBox box, Color color) {
    const Vector3 mn = box.min, mx = box.max;
    Vector3 c[8] = {
        {mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},{mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},
        {mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},{mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},
    };
    const int edges[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
                              {6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    for (auto& e : edges) DrawLine3D(c[e[0]], c[e[1]], color);
}

// ===========================================================================
// Billboards (camera-facing textured quads)
// ===========================================================================
void DrawBillboardRec(Camera3D camera, Texture2D texture, Rectangle source,
                      Vector3 position, Vector2 size, Color tint) {
    if (texture.id == 0) return;
    // Build camera-facing basis vectors.
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    Vector3 rw = Vector3Scale(right, size.x * 0.5f);
    Vector3 uw = Vector3Scale(up, size.y * 0.5f);

    Vector3 tl = Vector3Add(position, Vector3Subtract(uw, rw));
    Vector3 tr = Vector3Add(position, Vector3Add(uw, rw));
    Vector3 br = Vector3Subtract(position, Vector3Subtract(uw, rw));
    Vector3 bl = Vector3Subtract(position, Vector3Add(uw, rw));

    const float u0 = source.x / texture.width;
    const float v0 = source.y / texture.height;
    const float u1 = (source.x + source.width) / texture.width;
    const float v1 = (source.y + source.height) / texture.height;

    SetBatchState(DrawMode::Triangles, texture.id);
    auto& verts = detail::State().batch.verts;
    auto V = [&](Vector3 p, float u, float v) {
        verts.push_back(backend::Vertex{p.x, p.y, p.z, u, v, tint.r, tint.g, tint.b, tint.a});
    };
    V(tl,u0,v0); V(bl,u0,v1); V(br,u1,v1);
    V(tl,u0,v0); V(br,u1,v1); V(tr,u1,v0);
}

void DrawBillboard(Camera3D camera, Texture2D texture, Vector3 position,
                   float scale, Color tint) {
    DrawBillboardRec(camera, texture,
                     {0, 0, static_cast<float>(texture.width), static_cast<float>(texture.height)},
                     position, {scale, scale}, tint);
}

void DrawBillboardPro(Camera3D camera, Texture2D texture, Rectangle source, Vector3 position,
                      Vector3 up, Vector2 size, Vector2 origin, float rotation, Color tint) {
    if (texture.id == 0) return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up));
    Vector3 realUp = Vector3CrossProduct(right, forward);
    // Apply in-plane rotation to the basis vectors.
    const float rad = rotation * DEG2RAD, cr = std::cos(rad), sr = std::sin(rad);
    Vector3 rRot = Vector3Add(Vector3Scale(right, cr), Vector3Scale(realUp, sr));
    Vector3 uRot = Vector3Add(Vector3Scale(right, -sr), Vector3Scale(realUp, cr));
    Vector3 rw = Vector3Scale(rRot, size.x);
    Vector3 uw = Vector3Scale(uRot, size.y);
    Vector3 originShift = Vector3Add(Vector3Scale(rRot, origin.x), Vector3Scale(uRot, origin.y));
    Vector3 base = Vector3Subtract(position, originShift);
    Vector3 tl = Vector3Add(base, uw);
    Vector3 tr = Vector3Add(base, Vector3Add(uw, rw));
    Vector3 br = Vector3Add(base, rw);
    Vector3 bl = base;
    const float u0 = source.x / texture.width, v0 = source.y / texture.height;
    const float u1 = (source.x + source.width) / texture.width, v1 = (source.y + source.height) / texture.height;
    SetBatchState(DrawMode::Triangles, texture.id);
    auto& verts = detail::State().batch.verts;
    auto V = [&](Vector3 p, float u, float v) {
        verts.push_back(backend::Vertex{p.x, p.y, p.z, u, v, tint.r, tint.g, tint.b, tint.a});
    };
    V(tl,u0,v0); V(bl,u0,v1); V(br,u1,v1);
    V(tl,u0,v0); V(br,u1,v1); V(tr,u1,v0);
}

void DrawModelWiresEx(Model model, Vector3 position, Vector3 rotationAxis, float rotationAngle,
                      Vector3 scale, Color tint) {
    // Reuse DrawModelWires by temporarily composing the requested transform.
    Model tmp = model;
    tmp.transform = MatrixMultiply(model.transform,
        MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z),
                       MatrixRotate(rotationAxis, rotationAngle * DEG2RAD)));
    DrawModelWires(tmp, position, 1.0f, tint);
}

// ===========================================================================
// Material / model helpers
// ===========================================================================
bool IsMaterialValid(Material material) { return material.maps != nullptr; }

Material* LoadMaterials(const std::string& fileName, int* materialCount) {
    // Load the materials referenced by a model file (glTF/OBJ) and hand back
    // their material array. The model's mesh/material wiring is discarded.
    Model model = LoadModel(fileName);
    if (model.materialCount <= 0 || !model.materials) {
        if (materialCount) *materialCount = 0;
        UnloadModel(model);
        return nullptr;
    }
    const int count = model.materialCount;
    auto* out = new Material[count];
    for (int i = 0; i < count; ++i) out[i] = model.materials[i];
    // Detach materials from the model so UnloadModel won't free their maps.
    model.materials = nullptr;
    model.materialCount = 0;
    UnloadModel(model);
    if (materialCount) *materialCount = count;
    return out;
}

void SetModelMeshMaterial(Model* model, int meshId, int materialId) {
    if (!model || !model->meshMaterial) return;
    if (meshId < 0 || meshId >= model->meshCount) return;
    if (materialId < 0 || materialId >= model->materialCount) return;
    model->meshMaterial[meshId] = materialId;
}

void UpdateMeshBuffer(Mesh mesh, int index, const void* data, int dataSize, int offset) {
    // Update the CPU-side arrays; index mirrors raylib's vertex attribute slots
    // (0=positions, 1=texcoords, 2=normals, 3=colors). GPU re-upload happens on
    // the next DrawMesh via the streaming/persistent path.
    if (!data || dataSize <= 0) return;
    auto patch = [&](void* dst, std::size_t size) {
        if (!dst) return;
        std::memcpy(static_cast<unsigned char*>(dst) + offset, data, std::min<std::size_t>(dataSize, size - offset));
    };
    switch (index) {
        case 0: patch(mesh.vertices, mesh.vertexCount * 3 * sizeof(float)); break;
        case 1: patch(mesh.texcoords, mesh.vertexCount * 2 * sizeof(float)); break;
        case 2: patch(mesh.normals, mesh.vertexCount * 3 * sizeof(float)); break;
        case 3: patch(mesh.colors, mesh.vertexCount * 4); break;
        default: break;
    }
}

bool ExportMesh(Mesh mesh, const std::string& fileName) {
    // Export as a Wavefront OBJ (positions/normals/texcoords + faces).
    if (!mesh.vertices || mesh.vertexCount <= 0) return false;
    std::string obj = "# MeowyRender ExportMesh\n";
    for (int i = 0; i < mesh.vertexCount; ++i)
        obj += "v " + std::to_string(mesh.vertices[i*3]) + " " + std::to_string(mesh.vertices[i*3+1]) +
               " " + std::to_string(mesh.vertices[i*3+2]) + "\n";
    if (mesh.texcoords)
        for (int i = 0; i < mesh.vertexCount; ++i)
            obj += "vt " + std::to_string(mesh.texcoords[i*2]) + " " + std::to_string(mesh.texcoords[i*2+1]) + "\n";
    if (mesh.normals)
        for (int i = 0; i < mesh.vertexCount; ++i)
            obj += "vn " + std::to_string(mesh.normals[i*3]) + " " + std::to_string(mesh.normals[i*3+1]) +
                   " " + std::to_string(mesh.normals[i*3+2]) + "\n";
    for (int t = 0; t < mesh.triangleCount; ++t) {
        const int a = (mesh.indices ? mesh.indices[t*3] : t*3) + 1;
        const int b = (mesh.indices ? mesh.indices[t*3+1] : t*3+1) + 1;
        const int c = (mesh.indices ? mesh.indices[t*3+2] : t*3+2) + 1;
        obj += "f " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c) + "\n";
    }
    return SaveFileText(fileName, obj);
}
bool ExportMeshAsCode(Mesh mesh, const std::string& fileName) {
    if (!mesh.vertices || mesh.vertexCount <= 0) return false;
    return ExportDataAsCode(reinterpret_cast<const unsigned char*>(mesh.vertices),
                            mesh.vertexCount * 3 * static_cast<int>(sizeof(float)), fileName);
}

} // namespace meowyrender
