#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
using namespace meowyrender;
int main() {
    int failures=0;
    auto expect=[&](bool ok,const char* label){std::printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    auto equal=[](Color a,Color b){return a.r==b.r&&a.g==b.g&&a.b==b.b;};
    InitWindow(160,120,"Cubemap checks");
    const Color colors[]={RED,GREEN,BLUE,YELLOW,MAGENTA,SKYBLUE};
    const Vector3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(auto layout:{CubemapLayout::LineVertical,CubemapLayout::LineHorizontal,CubemapLayout::CrossThreeByFour,CubemapLayout::CrossFourByThree}) {
        int columns=layout==CubemapLayout::LineVertical?1:layout==CubemapLayout::LineHorizontal?6:layout==CubemapLayout::CrossThreeByFour?3:4;
        int rows=columns==1?6:columns==6?1:columns==3?4:3;
        Image atlas=GenImageColor(columns*8,rows*8,BLACK);
        int cx[]={2,0,1,1,1,columns==3?1:3},cy[]={1,1,0,2,1,columns==3?3:1};
        for(int f=0;f<6;++f) {
            int x=columns==1?0:columns==6?f:cx[f],y=columns==1?f:columns==6?0:cy[f];
            for(int py=0;py<8;++py)for(int px=0;px<8;++px)static_cast<Color*>(atlas.data)[(y*8+py)*atlas.width+x*8+px]=colors[f];
        }
        auto cube=LoadTextureCubemap(atlas);UnloadImage(atlas);
        expect(cube.id!=0,"native cubemap atlas upload");
        GenTextureMipmaps(&cube);SetTextureFilter(cube,TextureFilter::Trilinear);
        for(int f=0;f<6;++f) {
            Image face=LoadImageFromCubemapFace(cube,f);expect(face.data&&equal(GetImageColor(face,3,3),colors[f]),"cubemap face readback");UnloadImage(face);
            BeginDrawing();ClearBackground(BLACK);
            Camera3D camera{{10,20,30},Vector3Add({10,20,30},directions[f]),f==2||f==3?Vector3{0,0,1}:Vector3{0,1,0},60,CameraProjection::Perspective};
            BeginMode3D(camera);DrawSkybox(cube);EndMode3D();TakeScreenshot("cube-check.png");EndDrawing();
            Image screen=LoadImage("cube-check.png");expect(screen.data&&equal(GetImageColor(screen,screen.width/2,screen.height/2),colors[f]),"skybox samples expected face and ignores camera translation");UnloadImage(screen);
        }
        BeginDrawing();ClearBackground(BLACK);BeginMode3D({{0,0,0},{0,0,-1},{0,1,0},60,CameraProjection::Perspective});
        DrawCube({0,0,-2},0.5f,0.5f,0.5f,WHITE);DrawSkybox(cube);EndMode3D();TakeScreenshot("cube-depth.png");EndDrawing();
        Image depth=LoadImage("cube-depth.png");expect(depth.data&&equal(GetImageColor(depth,depth.width/2,depth.height/2),WHITE),"skybox preserves foreground depth");UnloadImage(depth);UnloadTexture(cube);
    }
    Image panorama=GenImageColor(64,32,ORANGE);auto cube=LoadTextureCubemap(panorama);UnloadImage(panorama);
    Image face=LoadImageFromCubemapFace(cube,4);expect(face.data&&equal(GetImageColor(face,3,3),ORANGE),"equirectangular panorama conversion");UnloadImage(face);UnloadTexture(cube);
    CloseWindow();return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
