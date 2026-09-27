// meowyrender - samples/models_util_checks.cpp
// Deterministic CPU tests for rmodels mesh generation and ray collision. No GPU
// context needed (mesh generators and ray math run on the CPU).
#include "meowyrender/meowyrender.hpp"
#include <cstdio>
#include <cmath>
#include <stdexcept>

namespace mr = meowyrender;
static void Check(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
static bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

int main() {
    try {
        // --- Mesh generators produce valid geometry ---
        {
            mr::Mesh cone = mr::GenMeshCone(1.0f, 2.0f, 16);
            Check(cone.vertices && cone.vertexCount == 16 * 2 * 3, "GenMeshCone vertex count");
            mr::UnloadMesh(cone);

            mr::Mesh hemi = mr::GenMeshHemiSphere(1.0f, 8, 16);
            Check(hemi.vertices && hemi.triangleCount == 8 * 16 * 2, "GenMeshHemiSphere tris");
            // All hemisphere vertices should have y >= -epsilon.
            bool upperHalf = true;
            for (int i = 0; i < hemi.vertexCount; ++i) if (hemi.vertices[i*3+1] < -0.01f) upperHalf = false;
            Check(upperHalf, "GenMeshHemiSphere is upper half");
            mr::UnloadMesh(hemi);

            mr::Mesh poly = mr::GenMeshPoly(6, 1.0f);
            Check(poly.vertices && poly.triangleCount == 6, "GenMeshPoly tris");
            mr::UnloadMesh(poly);

            mr::Mesh knot = mr::GenMeshKnot(2.0f, 0.3f, 32, 12);
            Check(knot.vertices && knot.triangleCount == 32 * 12 * 2, "GenMeshKnot tris");
            mr::UnloadMesh(knot);

            // Heightmap from a tiny image.
            mr::Image hm = mr::GenImageColor(4, 4, mr::WHITE);
            mr::Mesh terrain = mr::GenMeshHeightmap(hm, {10, 3, 10});
            Check(terrain.vertices && terrain.triangleCount == 3 * 3 * 2, "GenMeshHeightmap tris");
            mr::UnloadMesh(terrain);
            mr::UnloadImage(hm);

            // Tangents
            mr::Mesh cube = mr::GenMeshCube(1, 1, 1);
            mr::GenMeshTangents(&cube);
            Check(cube.tangents != nullptr, "GenMeshTangents allocates");
            mr::UnloadMesh(cube);
        }

        // --- Ray/triangle collision ---
        {
            mr::Ray ray{{0, 0, -5}, {0, 0, 1}};
            mr::RayCollision hit = mr::GetRayCollisionTriangle(ray, {-1, -1, 0}, {1, -1, 0}, {0, 1, 0});
            Check(hit.hit, "ray hits triangle");
            Check(Near(hit.distance, 5.0f), "triangle hit distance");
            Check(Near(hit.point.z, 0.0f), "triangle hit point z");

            mr::Ray miss{{5, 5, -5}, {0, 0, 1}};
            Check(!mr::GetRayCollisionTriangle(miss, {-1, -1, 0}, {1, -1, 0}, {0, 1, 0}).hit, "ray misses triangle");
        }

        // --- Ray/quad collision ---
        {
            mr::Ray ray{{0.2f, 0.2f, -3}, {0, 0, 1}};
            mr::RayCollision hit = mr::GetRayCollisionQuad(ray, {-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0});
            Check(hit.hit && Near(hit.distance, 3.0f), "ray hits quad");
        }

        // --- Ray/mesh collision ---
        {
            mr::Mesh cube = mr::GenMeshCube(2, 2, 2);
            mr::Ray ray{{0, 0, -10}, {0, 0, 1}};
            mr::RayCollision hit = mr::GetRayCollisionMesh(ray, cube, mr::MatrixIdentity());
            Check(hit.hit, "ray hits mesh");
            Check(Near(hit.distance, 9.0f), "mesh front face distance");
            mr::UnloadMesh(cube);
        }

        // --- Mesh export ---
        {
            mr::Mesh cube = mr::GenMeshCube(1, 1, 1);
            Check(mr::ExportMesh(cube, "mesh-export.obj"), "ExportMesh OBJ");
            std::remove("mesh-export.obj");
            mr::UnloadMesh(cube);
        }

        std::printf("models_util_checks: all checks passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "models_util_checks FAILED: %s\n", e.what());
        return 1;
    }
}
