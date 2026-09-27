#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
using namespace meowyrender;
int main() {
    int failures=0;
    auto expect=[&](bool ok,const char* label){std::printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    InitWindow(160,120,"Stereo checks");
    auto config=LoadVrStereoConfig();
    expect(config.viewOffset[0].m12>0&&config.viewOffset[1].m12<0,"device config generates opposing eye offsets");
    for(int i=0;i<2;++i) {config.projection[i]=MatrixPerspective(PI/2,80.0/120,0.01,1000);config.viewOffset[i]=MatrixTranslate(i==0?0.25f:-0.25f,0,0);}
    Mesh mesh=GenMeshCube(0.4f,0.4f,0.4f);Material material=LoadMaterialDefault();
    Matrix transform=MatrixTranslate(0,0,-2);
    BeginDrawing();ClearBackground(BLACK);BeginVrStereoMode(config);
    BeginMode3D({{0,0,0},{0,0,-1},{0,1,0},90,CameraProjection::Perspective});
    DrawMeshInstanced(mesh,material,&transform,1);
    EndMode3D();EndVrStereoMode();
    DrawRectangle(0,110,160,10,RED);
    TakeScreenshot("stereo-check.png");EndDrawing();
    Image screen=LoadImage("stereo-check.png");
    double sums[2]{};int counts[2]{};
    for(int y=0;y<screen.height*100/120;++y)for(int x=0;x<screen.width;++x) {
        Color pixel=GetImageColor(screen,x,y);
        if(pixel.r>200&&pixel.g>200&&pixel.b>200){int eye=x>=screen.width/2;sums[eye]+=x*160.0/screen.width;++counts[eye];}
    }
    expect(counts[0]>10&&counts[1]>10,"instanced geometry rendered into both eye viewports");
    expect(counts[0]&&counts[1]&&sums[0]/counts[0]>40&&sums[1]/counts[1]<120,"eye matrices produce binocular disparity");
    expect(GetImageColor(screen,screen.width/2,115*screen.height/120).r==RED.r,"normal viewport and projection restored after stereo");
    UnloadImage(screen);UnloadMaterial(material);UnloadMesh(mesh);

    // --- VrStereoConfig lifecycle: it is a pure value type (fixed Matrix[2] +
    // float[2] arrays, no pointers/handles/GPU resources), so UnloadVrStereoConfig
    // has nothing to release. Verify that contract: unloading is a safe no-op that
    // neither mutates the config nor prevents continued use, and is idempotent.
    {
        VrStereoConfig lifecycle = LoadVrStereoConfig();
        VrStereoConfig before = lifecycle;
        UnloadVrStereoConfig(lifecycle);
        bool unchanged = lifecycle.scale[0]==before.scale[0] && lifecycle.scaleIn[0]==before.scaleIn[0] &&
                         lifecycle.leftLensCenter[0]==before.leftLensCenter[0] &&
                         lifecycle.viewOffset[0].m12==before.viewOffset[0].m12;
        expect(unchanged,"UnloadVrStereoConfig leaves the value-type config intact (no-op cleanup)");
        // Still usable after "unload"; and unloading again / an empty config is safe.
        BeginDrawing();ClearBackground(BLACK);BeginVrStereoMode(lifecycle);EndVrStereoMode();EndDrawing();
        UnloadVrStereoConfig(lifecycle);          // idempotent
        UnloadVrStereoConfig(VrStereoConfig{});   // empty/default config safe
        expect(true,"UnloadVrStereoConfig is idempotent and safe on an empty config");
    }
    UnloadVrStereoConfig(config);CloseWindow();
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
