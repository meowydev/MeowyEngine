// meowyrender - samples/vulkan_shadow_regression.cpp
// Isolated Vulkan/MoltenVK shadow-depth-pass rendering regression.
//
// The Vulkan backend fully plumbs directional shadow mapping (shadow.vert/frag
// depth pass -> R32F map, PCF at binding 9) but reports SupportsShadows()=false
// because, under MoltenVK, occluder fragments appeared not to be stored into the
// mid-frame offscreen shadow target. That was never proven by a rendering test:
// the shadow map is only ever *sampled*, never read back to the host.
//
// This test isolates exactly that mechanism. It drives the backend's shadow pass
// directly (bypassing the SupportsShadows() gate that disables the public
// BeginShadowMode path), renders a single occluder quad into the offscreen R32F
// shadow target, then reads the target back to the host via the Vulkan-only
// ReadShadowMap() diagnostic hook and checks whether any occluder depth (< 1.0,
// i.e. < 255 after packing) was actually stored. The clear value is depth 1.0.
//
// It does NOT enable shadow support. It only produces evidence about whether the
// offscreen shadow store works on the active driver.
#include <meowyrender/meowyrender.hpp>
#include "core/mr_state.hpp"
#include "backend/render_backend.hpp"
#include "backend/vulkan/vk_backend.hpp"

#include <cstdio>
#include <vector>
using namespace meowyrender;

int main() {
    // Vulkan-specific test: prefer the Vulkan backend (multi-backend builds
    // would otherwise auto-select Metal). Skips cleanly if Vulkan isn't active.
    SetPreferredBackend(Backend::Vulkan);
    InitWindow(128,128,"Vulkan shadow regression");
    int failures=0;
    auto expect=[&](bool ok,const char* label){std::printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};

    auto* base = detail::State().backend.get();
    auto* vk = dynamic_cast<backend::vulkan::VulkanBackend*>(base);
    if(!vk) { std::printf("SKIP: not the Vulkan backend\n"); CloseWindow(); return 0; }

    // Light looks straight down -Y at the origin; an orthographic frustum that
    // comfortably contains a quad on the XZ plane at y=0.
    const int resolution = 64;
    Matrix view = MatrixLookAt({0,4,0},{0,0,0},{0,0,-1});
    Matrix proj = MatrixOrtho(-2,2,-2,2,0.1,10.0);
    Matrix lightViewProj = MatrixMultiply(view, proj);

    // A single occluder quad on the XZ plane (y=0), well inside the frustum.
    // DrawVertices consumes positions in the backend's vertex layout; the shadow
    // pass projects them by the light view-projection push constant.
    auto V=[](float x,float y,float z){ backend::Vertex v{}; v.x=x; v.y=y; v.z=z; v.u=0; v.v=0; v.r=v.g=v.b=v.a=255; v.nx=0; v.ny=1; v.nz=0; return v; };
    std::vector<backend::Vertex> quad = {
        V(-1,0,-1), V(1,0,-1), V(1,0,1),
        V(-1,0,-1), V(1,0,1),  V(-1,0,1),
    };

    // Drive the shadow depth pass directly (this is what BeginShadowMode would
    // do if SupportsShadows() were true). BeginDrawing() opens the frame/screen
    // pass so BeginShadowPass exercises the mid-frame interleave that was
    // suspected of dropping the store.
    // BeginDrawing()+ClearBackground opens the screen render pass; BeginShadowPass
    // then ends it and runs the depth pass mid-frame, exercising the realistic
    // interleave the lit path uses.
    BeginDrawing();
    ClearBackground(BLACK);
    vk->BeginShadowPass(lightViewProj, resolution);
    vk->DrawVertices(quad.data(), quad.size(), backend::DrawMode::Triangles, 0);
    vk->EndShadowPass();

    Image shadow = vk->ReadShadowMap();
    expect(shadow.data && shadow.width==resolution && shadow.height==resolution, "shadow map reads back");

    // Scan for any stored occluder depth (< 255 = depth < 1.0). The quad covers
    // the whole frustum, so the entire map should be occluded if the store works.
    int occluded=0, minDepth=255;
    if(shadow.data) {
        for(int y=0;y<shadow.height;++y) for(int x=0;x<shadow.width;++x) {
            Color c=GetImageColor(shadow,x,y);
            if(c.r<250){ ++occluded; if(c.r<minDepth) minDepth=c.r; }
        }
    }
    const int total=resolution*resolution;
    std::printf("  (occluded texels=%d/%d, min packed depth=%d)\n", occluded, total, minDepth);
    // The quad projects to the central ~25% of the map (NDC [-0.5,0.5]^2). If
    // occluder fragments are stored as filled triangles, roughly a quarter of
    // the map holds depth < 1.0. A broken pipeline (e.g. POINT_LIST topology)
    // would store only a handful of vertex points, and a dropped store 0.
    expect(occluded > total/8, "occluder fragments are stored in the offscreen shadow target");
    // Center of the projected quad must carry the occluder depth (~0.39 -> ~100),
    // proving a filled rasterization rather than sparse vertex points.
    Color center = shadow.data ? GetImageColor(shadow,resolution/2,resolution/2) : Color{255,0,0,255};
    expect(center.r>50 && center.r<200, "shadow map center holds filled occluder depth");

    if(shadow.data) UnloadImage(shadow);
    EndDrawing();
    CloseWindow();

    std::printf("%s\n", failures==0 ? "SHADOW REGRESSION PASS" : "SHADOW REGRESSION FAIL");
    return failures==0 ? 0 : 1;
}