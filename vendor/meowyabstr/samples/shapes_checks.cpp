// meowyrender - samples/shapes_checks.cpp
// Deterministic tests for rshapes collision helpers and spline evaluators. No
// graphics context required (these are pure geometry).
#include "meowyrender/meowyrender.hpp"
#include <cstdio>
#include <cmath>
#include <stdexcept>

namespace mr = meowyrender;
static void Check(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
static bool Near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

int main() {
    try {
        // --- Rect / circle / point collisions ---
        Check(mr::CheckCollisionRecs({0,0,10,10}, {5,5,10,10}), "recs overlap");
        Check(!mr::CheckCollisionRecs({0,0,10,10}, {20,20,5,5}), "recs disjoint");
        mr::Rectangle inter = mr::GetCollisionRec({0,0,10,10}, {5,5,10,10});
        Check(Near(inter.x,5) && Near(inter.y,5) && Near(inter.width,5) && Near(inter.height,5), "GetCollisionRec");
        Check(mr::CheckCollisionCircles({0,0}, 5, {8,0}, 5), "circles overlap");
        Check(!mr::CheckCollisionCircles({0,0}, 2, {10,0}, 2), "circles disjoint");
        Check(mr::CheckCollisionCircleRec({0,0}, 3, {2,2,10,10}), "circle-rec hit");
        Check(!mr::CheckCollisionCircleRec({0,0}, 1, {5,5,10,10}), "circle-rec miss");
        Check(mr::CheckCollisionCircleLine({5,1}, 2, {0,0}, {10,0}), "circle-line hit");
        Check(!mr::CheckCollisionCircleLine({5,10}, 2, {0,0}, {10,0}), "circle-line miss");
        Check(mr::CheckCollisionPointRec({5,5}, {0,0,10,10}), "point-rec inside");
        Check(mr::CheckCollisionPointCircle({1,0}, {0,0}, 2), "point-circle inside");
        Check(mr::CheckCollisionPointTriangle({1,1}, {0,0}, {10,0}, {0,10}), "point-tri inside");
        Check(!mr::CheckCollisionPointTriangle({9,9}, {0,0}, {10,0}, {0,10}), "point-tri outside");
        Check(mr::CheckCollisionPointLine({5,0}, {0,0}, {10,0}, 1), "point-line on");
        Check(!mr::CheckCollisionPointLine({5,5}, {0,0}, {10,0}, 1), "point-line off");

        mr::Vector2 poly[4] = {{0,0},{10,0},{10,10},{0,10}};
        Check(mr::CheckCollisionPointPoly({5,5}, poly, 4), "point-poly inside");
        Check(!mr::CheckCollisionPointPoly({15,5}, poly, 4), "point-poly outside");

        mr::Vector2 cp{};
        Check(mr::CheckCollisionLines({0,0},{10,10},{0,10},{10,0}, &cp), "lines cross");
        Check(Near(cp.x,5) && Near(cp.y,5), "lines cross point");
        Check(!mr::CheckCollisionLines({0,0},{10,0},{0,5},{10,5}, nullptr), "parallel lines");

        // --- Spline evaluators: endpoints and midpoints ---
        {
            mr::Vector2 a{0,0}, b{10,20};
            mr::Vector2 m = mr::GetSplinePointLinear(a, b, 0.5f);
            Check(Near(m.x,5) && Near(m.y,10), "linear midpoint");
        }
        {
            // Cubic bezier at t=0 is p1, t=1 is p4.
            mr::Vector2 p1{0,0}, c2{0,10}, c3{10,10}, p4{10,0};
            mr::Vector2 s = mr::GetSplinePointBezierCubic(p1,c2,c3,p4,0.0f);
            mr::Vector2 e = mr::GetSplinePointBezierCubic(p1,c2,c3,p4,1.0f);
            Check(Near(s.x,0) && Near(s.y,0), "bezier cubic start");
            Check(Near(e.x,10) && Near(e.y,0), "bezier cubic end");
        }
        {
            // Quadratic bezier endpoints.
            mr::Vector2 p1{0,0}, c2{5,10}, p3{10,0};
            mr::Vector2 mid = mr::GetSplinePointBezierQuad(p1,c2,p3,0.5f);
            Check(Near(mid.x,5) && Near(mid.y,5), "bezier quad midpoint");
        }
        {
            // Catmull-Rom passes through p2 at t=0 and p3 at t=1.
            mr::Vector2 p1{0,0}, p2{1,1}, p3{2,4}, p4{3,9};
            mr::Vector2 s = mr::GetSplinePointCatmullRom(p1,p2,p3,p4,0.0f);
            mr::Vector2 e = mr::GetSplinePointCatmullRom(p1,p2,p3,p4,1.0f);
            Check(Near(s.x,1) && Near(s.y,1), "catmull start = p2");
            Check(Near(e.x,2) && Near(e.y,4), "catmull end = p3");
        }

        std::printf("shapes_checks: all checks passed\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "shapes_checks FAILED: %s\n", e.what());
        return 1;
    }
}
