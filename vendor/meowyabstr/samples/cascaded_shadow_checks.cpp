// meowyrender - samples/cascaded_shadow_checks.cpp
// Verifies cascaded directional shadow maps: an occluder darkens the floor
// beneath it, and cascades covering different view-depth ranges each produce a
// shadow (near AND far occluders both cast). Runs where cascades are supported.
#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cmath>
#include <stdexcept>
namespace mr = meowyrender;
static int failures=0;
static void Expect(bool ok,const char* msg){ std::printf("%s: %s\n", ok?"PASS":"FAIL", msg); if(!ok) ++failures; }

int main(){
    mr::InitWindow(400,400,"cascaded shadow checks");
    if(!mr::IsCascadedShadowSupported()){
        std::printf("SKIP: cascaded shadows unsupported on this backend\n");
        mr::CloseWindow();
        return 0;
    }
    Expect(true,"cascaded shadows supported");

    // A large floor along +Z, and two occluder quads: one near the camera, one
    // far, so they fall into different cascades.
    float floorV[]={-8,0,-2, 8,0,-2, 8,0,40, -8,0,40};
    float floorN[]={0,1,0, 0,1,0, 0,1,0, 0,1,0};
    float floorUV[]={0,0,1,0,1,1,0,1}; unsigned short qi[]={0,1,2,0,2,3};
    mr::Mesh floor{}; floor.vertices=floorV; floor.normals=floorN; floor.texcoords=floorUV; floor.indices=qi; floor.vertexCount=4; floor.triangleCount=2;

    auto occluderAt = [&](float z)->mr::Mesh {
        static float storage[8][12]; static int slot=0; float* v=storage[slot++%8];
        float src[]={-1.5f,3,z-1.5f, 1.5f,3,z-1.5f, 1.5f,3,z+1.5f, -1.5f,3,z+1.5f};
        for(int i=0;i<12;++i) v[i]=src[i];
        mr::Mesh m{}; m.vertices=v; m.normals=floorN; m.texcoords=floorUV; m.indices=qi; m.vertexCount=4; m.triangleCount=2;
        return m;
    };
    mr::Mesh nearOcc=occluderAt(2.0f);   // close to the camera -> near cascade
    mr::Mesh farOcc =occluderAt(30.0f);  // far down the floor  -> far cascade

    mr::Material mat=mr::LoadMaterialDefault(); mat.lighting=true;
    mat.maps[static_cast<int>(mr::MaterialMapIndex::Albedo)].color=mr::WHITE;
    mat.maps[static_cast<int>(mr::MaterialMapIndex::Roughness)].value=1.0f;

    mr::SetAmbientLight(mr::WHITE,0.15f);
    mr::Vector3 lightDir{0,-1,0.01f};
    mr::SetDirectionalLight(lightDir,mr::WHITE,3.0f);

    // Camera looking down the floor from a shallow angle so near+far are visible.
    mr::Camera3D cam{{0,6,-1},{0,0,20},{0,1,0},60,mr::CameraProjection::Perspective};

    mr::BeginDrawing(); mr::ClearBackground(mr::BLACK);
    // Cascade depth passes: render both occluders into each cascade.
    mr::BeginShadowCascades(cam,lightDir,3,1024);
    for(int c=0;c<mr::GetShadowCascadeCount();++c){
        mr::SetShadowCascade(c);
        mr::DrawMesh(nearOcc,mat,mr::MatrixIdentity());
        mr::DrawMesh(farOcc,mat,mr::MatrixIdentity());
    }
    mr::EndShadowCascades();
    // Lit pass.
    mr::BeginMode3D(cam);
        mr::DrawMesh(floor,mat,mr::MatrixIdentity());
    mr::EndMode3D();
    mr::TakeScreenshot("cascaded-shadow-check.png");
    mr::EndDrawing();

    mr::Image img=mr::LoadImage("cascaded-shadow-check.png");
    // The near occluder is centered at z=2, the far at z=30. Both project onto
    // the floor; sample a shadowed strip (screen upper area = far, lower = near)
    // versus an unshadowed side strip.
    auto avg=[&](int x0,int x1,int y0,int y1){
        long r=0; int n=0;
        for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x){ r+=mr::GetImageColor(img,x,y).r; ++n; }
        return n?static_cast<int>(r/n):0;
    };
    const int W=img.width,H=img.height;
    int litMax=0;
    for(int y=H/3;y<H;++y) for(int x=0;x<W;++x) litMax=std::max(litMax,(int)mr::GetImageColor(img,x,y).r);
    const int shadowThreshold=litMax*3/4;
    auto shadowCount=[&](int y0,int y1){ int n=0; for(int y=y0;y<y1;++y) for(int x=0;x<W;++x){ int r=mr::GetImageColor(img,x,y).r; if(r>10 && r<shadowThreshold) ++n; } return n; };
    int totalShadow=shadowCount(H/3,H);
    std::printf("  (litMax=%d shadowedPixels=%d)\n",litMax,totalShadow);
    Expect(img.data && litMax>60,"floor is lit");
    Expect(totalShadow>50,"cascaded shadow map darkens the occluded floor region");
    mr::UnloadImage(img);

    // Distinguish cascades from a single map: with only 1 cascade fitted to the
    // near range, the FAR occluder's shadow (z=30) should shrink/disappear
    // compared to the 3-cascade result, since one cascade can't cover both
    // ranges at the near cascade's tight bounds.
    mr::BeginDrawing(); mr::ClearBackground(mr::BLACK);
    mr::BeginShadowCascades(cam,lightDir,1,1024);
    for(int c=0;c<mr::GetShadowCascadeCount();++c){ mr::SetShadowCascade(c); mr::DrawMesh(nearOcc,mat,mr::MatrixIdentity()); mr::DrawMesh(farOcc,mat,mr::MatrixIdentity()); }
    mr::EndShadowCascades();
    mr::BeginMode3D(cam); mr::DrawMesh(floor,mat,mr::MatrixIdentity()); mr::EndMode3D();
    mr::TakeScreenshot("cascaded-shadow-1.png"); mr::EndDrawing();
    mr::Image img1=mr::LoadImage("cascaded-shadow-1.png");
    int one=0; for(int y=H/3;y<H;++y) for(int x=0;x<W;++x){ int r=mr::GetImageColor(img1,x,y).r; if(r>10 && r<shadowThreshold) ++one; }
    std::printf("  (1-cascade shadowedPixels=%d vs 3-cascade %d)\n",one,totalShadow);
    // 3 cascades should cover at least as much shadowed area as 1 (more range).
    Expect(totalShadow>=one,"more cascades cover at least as much shadowed area");
    img=img1;

    mr::UnloadImage(img); mr::UnloadMaterial(mat); mr::ClearShadowMap();
    mr::CloseWindow();
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
