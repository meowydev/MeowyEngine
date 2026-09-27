#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <cmath>
using namespace meowyrender;
static int failures=0;
void Expect(bool ok,const char* message){std::printf("%s: %s\n",ok?"PASS":"FAIL",message);if(!ok)++failures;}
int main(){
    InitWindow(320,240,"Material and blend checks");
    BeginDrawing(); ClearBackground(Color{100,100,100,255});
    for(int mode=0;mode<3;++mode) {
        BeginBlendMode(mode);
        DrawRectangle(mode*80,0,80,80,Color{100,40,20,128});
        EndBlendMode();
    }
    TakeScreenshot("blend-check.png"); EndDrawing();
    Image blended=LoadImage("blend-check.png");
    const Color expected[]={{100,70,60,255},{150,120,110,255},{39,16,8,255}};
    for(int mode=0;mode<3;++mode) {
        Color actual=GetImageColor(blended,(mode*80+40)*blended.width/320,40*blended.height/240);
        Expect(blended.data && std::abs(int(actual.r)-expected[mode].r)<=2 && std::abs(int(actual.g)-expected[mode].g)<=2 && std::abs(int(actual.b)-expected[mode].b)<=2,"blend mode produces expected RGB");
    }
    UnloadImage(blended);
    BeginDrawing(); ClearBackground(BLACK);
    BeginScissorMode(-10,-10,30,30); DrawRectangle(0,0,320,240,RED);
    TakeScreenshot("clip-before.png");
    DrawRectangle(0,0,320,240,GREEN); EndScissorMode();
    TakeScreenshot("clip-after.png"); EndDrawing();
    Image clipped=LoadImage("clip-after.png");
    Color inside=GetImageColor(clipped,10*clipped.width/320,10*clipped.height/240);
    Color outside=GetImageColor(clipped,40*clipped.width/320,40*clipped.height/240);
    Expect(clipped.data && inside.g==GREEN.g && outside.g==0,"clipping survives readback and clamps negative origin");
    UnloadImage(clipped);
    float vertices[]={10,90,0,50,90,0,50,130,0,10,130,0};
    float normals[]={0,0,1,0,0,1,0,0,1,0,0,1};
    float uvs[]={0,0,1,0,1,1,0,1};
    unsigned short indices[]={0,1,2,0,2,3};
    Mesh quad{}; quad.vertices=vertices; quad.normals=normals; quad.texcoords=uvs; quad.indices=indices; quad.vertexCount=4; quad.triangleCount=2;
    Material pbr=LoadMaterialDefault(); pbr.lighting=true;
    SetAmbientLight(BLACK,0); SetDirectionalLight({0,0,-1},WHITE,3);
    BeginDrawing(); ClearBackground(BLACK); DrawMesh(quad,pbr,MatrixIdentity());
    SetDirectionalLight({0,0,1},WHITE,3); DrawMesh(quad,pbr,MatrixTranslate(60,0,0));
    pbr.maps[static_cast<int>(MaterialMapIndex::Emission)].color={255,0,0,255};
    DrawMesh(quad,pbr,MatrixTranslate(120,0,0));
    TakeScreenshot("pbr-check.png"); EndDrawing();
    Image lit=LoadImage("pbr-check.png");
    Color bright=GetImageColor(lit,30*lit.width/320,110*lit.height/240);
    Color dark=GetImageColor(lit,90*lit.width/320,110*lit.height/240);
    Color emissive=GetImageColor(lit,150*lit.width/320,110*lit.height/240);
    Expect(lit.data && bright.r>100 && dark.r==0,"PBR respects normal and light direction");
    Expect(lit.data && emissive.r>180 && emissive.g==0,"emission remains visible without direct light");
    UnloadImage(lit);

    // Lit GPU instancing: three instances of the same lit quad at distinct
    // positions must each render lit (the shader applies each instance
    // transform to position and normal, not a per-instance CPU fallback).
    Material litInstanced=LoadMaterialDefault(); litInstanced.lighting=true;
    litInstanced.maps[static_cast<int>(MaterialMapIndex::Albedo)].color=WHITE;
    SetAmbientLight(BLACK,0); SetDirectionalLight({0,0,-1},WHITE,3);
    Matrix xforms[3]={MatrixIdentity(),MatrixTranslate(100,0,0),MatrixTranslate(200,0,0)};
    BeginDrawing(); ClearBackground(BLACK);
    DrawMeshInstanced(quad,litInstanced,xforms,3);
    TakeScreenshot("pbr-instanced.png"); EndDrawing();
    Image inst=LoadImage("pbr-instanced.png");
    Color a=GetImageColor(inst,30*inst.width/320,110*inst.height/240);
    Color b=GetImageColor(inst,130*inst.width/320,110*inst.height/240);
    Color c=GetImageColor(inst,230*inst.width/320,110*inst.height/240);
    Expect(inst.data && a.r>100 && b.r>100 && c.r>100,"lit instancing lights every instance at its own transform");
    UnloadImage(inst); UnloadMaterial(litInstanced);
    UnloadMaterial(pbr);

    // Image-based lighting: a metallic surface under a purely blue environment
    // cubemap (no directional light) must pick up blue ambient/reflection.
    // IBL is implemented natively on OpenGL, Metal, and Vulkan.
    {
        Image blueFaces=GenImageColor(8*6,8,BLUE); // 6 blue faces (horizontal line)
        TextureCubemap env=LoadTextureCubemap(blueFaces,CubemapLayout::LineHorizontal);
        UnloadImage(blueFaces);
        Expect(env.id!=0,"IBL environment cubemap uploads");
        SetAmbientLight(BLACK,0);
        SetDirectionalLight({0,-1,0},BLACK,0); // no direct light; ambient is IBL only
        SetEnvironmentLight(env,1.0f);
        Material metal=LoadMaterialDefault(); metal.lighting=true;
        metal.maps[static_cast<int>(MaterialMapIndex::Albedo)].color=WHITE;
        metal.maps[static_cast<int>(MaterialMapIndex::Metalness)].value=1.0f;
        metal.maps[static_cast<int>(MaterialMapIndex::Roughness)].value=0.3f;
        float vertices2[]={40,90,0,120,90,0,120,150,0,40,150,0};
        float normals2[]={0,0,1,0,0,1,0,0,1,0,0,1};
        float uvs2[]={0,0,1,0,1,1,0,1};
        unsigned short idx2[]={0,1,2,0,2,3};
        Mesh iblQuad{}; iblQuad.vertices=vertices2; iblQuad.normals=normals2; iblQuad.texcoords=uvs2; iblQuad.indices=idx2; iblQuad.vertexCount=4; iblQuad.triangleCount=2;
        BeginDrawing(); ClearBackground(BLACK);
        DrawMesh(iblQuad,metal,MatrixIdentity()); // 2D ortho: quad covers x40-120,y90-150
        TakeScreenshot("ibl-check.png"); EndDrawing();
        Image ibl=LoadImage("ibl-check.png");
        Color surf=GetImageColor(ibl,80*ibl.width/320,120*ibl.height/240);
        std::printf("  (IBL surface rgba=%d,%d,%d,%d)\n",surf.r,surf.g,surf.b,surf.a);
        Expect(ibl.data && surf.b>surf.r && surf.b>surf.g && surf.b>20,"IBL environment tints the lit surface blue");
        UnloadImage(ibl); UnloadMaterial(metal);
        ClearEnvironmentLight(); UnloadTexture(env);
    }

    // Directional shadow mapping (OpenGL). Render an occluder into the shadow
    // map from a light above, then draw a lit floor; the area under the occluder
    // must be darker than an unshadowed area of the same floor.
    if(IsShadowMappingSupported()) {
        ClearEnvironmentLight();
        SetAmbientLight(WHITE,0.15f);
        SetDirectionalLight({0,-1,0.01f},WHITE,3.0f); // straight down
        // A large floor at y=0 and an occluder quad at y=2 covering x[-1,1] z[-1,1].
        float floorV[]={-6,0,-6, 6,0,-6, 6,0,6, -6,0,6};
        float floorN[]={0,1,0, 0,1,0, 0,1,0, 0,1,0};
        float floorUV[]={0,0,1,0,1,1,0,1}; unsigned short qi[]={0,1,2,0,2,3};
        Mesh floor{}; floor.vertices=floorV; floor.normals=floorN; floor.texcoords=floorUV; floor.indices=qi; floor.vertexCount=4; floor.triangleCount=2;
        float occV[]={-1.5f,2,-1.5f, 1.5f,2,-1.5f, 1.5f,2,1.5f, -1.5f,2,1.5f};
        Mesh occluder{}; occluder.vertices=occV; occluder.normals=floorN; occluder.texcoords=floorUV; occluder.indices=qi; occluder.vertexCount=4; occluder.triangleCount=2;
        Material floorMat=LoadMaterialDefault(); floorMat.lighting=true;
        floorMat.maps[static_cast<int>(MaterialMapIndex::Albedo)].color=WHITE;
        floorMat.maps[static_cast<int>(MaterialMapIndex::Roughness)].value=1.0f;

        BeginDrawing(); ClearBackground(BLACK);
        // Shadow depth pass: light looking straight down at the origin.
        Camera3D lightCam{{0,10,0.001f},{0,0,0},{0,0,-1},8.0f,CameraProjection::Orthographic};
        BeginShadowMode(lightCam,1024);
            DrawMesh(occluder,floorMat,MatrixIdentity());
        EndShadowMode();
        // Lit pass: camera looking down at the floor.
        BeginMode3D({{0,9,0.001f},{0,0,0},{0,0,-1},60,CameraProjection::Perspective});
            DrawMesh(floor,floorMat,MatrixIdentity());
        EndMode3D();
        TakeScreenshot("shadow-check.png"); EndDrawing();
        Image shadow=LoadImage("shadow-check.png");
        Color under=GetImageColor(shadow,shadow.width/2,shadow.height/2);      // beneath occluder
        Color lit=GetImageColor(shadow,shadow.width/2,shadow.height*7/8);       // floor edge, not occluded
        std::printf("  (shadow under=%d lit=%d)\n",under.r,lit.r);
        Expect(shadow.data && under.r<lit.r && lit.r>60,"directional shadow darkens the occluded floor region");
        UnloadImage(shadow); UnloadMaterial(floorMat);
        ClearShadowMap();
    } else {
        std::printf("SKIP: directional shadow mapping (unsupported on this backend)\n");
    }

    CloseWindow();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
