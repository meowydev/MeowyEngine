#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
using namespace meowyrender;
namespace {
int failures=0;
void Expect(bool condition,const char* label) {
    std::printf("%s: %s\n",condition?"PASS":"FAIL",label);
    if(!condition) ++failures;
}
bool Near(Color a,Color b) {
    return std::abs(int(a.r)-b.r)<=2 && std::abs(int(a.g)-b.g)<=2 && std::abs(int(a.b)-b.b)<=2;
}
Color ScreenPixel(Image image,int x,int y) {
    return GetImageColor(image,x*image.width/320,y*image.height/240);
}
}
int main() {
    try { InitWindow(0,240,"invalid"); Expect(false,"invalid dimensions throw"); }
    catch(const std::invalid_argument&) { Expect(!IsWindowReady(),"invalid dimensions leave no window"); }
    InitWindow(320,240,"Backend regression checks");
    Image source=GenImageColor(8,8,RED);
    auto texture=LoadTextureFromImage(source);
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture,TextureFilter::Trilinear);
    SetTextureWrap(texture,TextureWrap::Clamp);
    Image upload=LoadImageFromTexture(texture);
    Expect(upload.data && Near(GetImageColor(upload,3,3),RED),"upload and mipmap generation preserve base pixels");
    UnloadImage(upload);
    BeginDrawing();
    ClearBackground(BLACK);
    DrawTextureEx(texture,{10,10},0,5,WHITE);
    // A screenshot flushes pending draws; later draws must preserve this region.
    TakeScreenshot("backend-before.png");
    UnloadImage(source); source=GenImageColor(8,8,BLUE);
    UpdateTexture(texture,source.data);
    SetTextureFilter(texture,TextureFilter::Point);
    DrawTextureEx(texture,{70,10},0,5,WHITE);
    float positions[]={140,10,0,180,10,0,180,50,0,140,50,0};
    unsigned short indices[]={0,1,2,0,2,3};
    Mesh mesh{}; mesh.vertexCount=4; mesh.triangleCount=2; mesh.vertices=positions; mesh.indices=indices;
    auto material=LoadMaterialDefault();
    material.maps[0].color=GREEN;
    DrawMesh(mesh,material,MatrixIdentity());
    Matrix instances[]={MatrixTranslate(-120,80,0),MatrixTranslate(-40,80,0),MatrixTranslate(40,80,0)};
    DrawMeshInstanced(mesh,material,instances,3);
    TakeScreenshot("backend-after.png");
    EndDrawing();
    Image screen=LoadImage("backend-after.png");
    Expect(screen.data && Near(ScreenPixel(screen,30,30),RED),"frame survives capture and texture update");
    Expect(screen.data && Near(ScreenPixel(screen,90,30),BLUE),"updated texture renders");
    Expect(screen.data && Near(ScreenPixel(screen,150,40),GREEN),"indexed mesh second triangle renders");
    Expect(screen.data && Near(ScreenPixel(screen,30,110),GREEN) && Near(ScreenPixel(screen,110,110),GREEN) && Near(ScreenPixel(screen,190,110),GREEN),"three mesh instances use independent transforms");
    UnloadImage(screen);
    BeginDrawing(); ClearBackground(BLACK);
    DrawTextureEx(texture,{10,10},0,5,WHITE);
    UnloadImage(source); source=GenImageColor(8,8,RED);
    UpdateTexture(texture,source.data);
    DrawTextureEx(texture,{70,10},0,5,WHITE);
    TakeScreenshot("backend-update.png"); EndDrawing();
    screen=LoadImage("backend-update.png");
    Expect(screen.data && Near(ScreenPixel(screen,30,30),BLUE) && Near(ScreenPixel(screen,90,30),RED),"mid-batch texture update preserves draw ordering");
    UnloadImage(screen);
    BeginDrawing(); ClearBackground(RED); DrawRectangle(0,0,320,240,BLUE);
    ClearBackground(GREEN); TakeScreenshot("backend-clear.png"); EndDrawing();
    screen=LoadImage("backend-clear.png");
    Expect(screen.data && Near(ScreenPixel(screen,160,120),GREEN),"repeated clear replaces previous geometry");
    UnloadImage(screen);

    // Persistent GPU mesh upload: an uploaded static mesh draws from its cached
    // buffer with the per-draw transform and matches the streamed result.
    float quadPos[]={0,0,0,40,0,0,40,40,0,0,40,0};
    unsigned short quadIdx[]={0,1,2,0,2,3};
    Mesh cached{}; cached.vertexCount=4; cached.triangleCount=2; cached.vertices=quadPos; cached.indices=quadIdx;
    // Copy positions/indices into owned storage so UploadMesh/UnloadMesh manage
    // only the GPU buffer (stack arrays must not be freed).
    Mesh owned=cached;
    owned.vertices=(float*)malloc(sizeof(quadPos)); memcpy(owned.vertices,quadPos,sizeof(quadPos));
    owned.indices=(unsigned short*)malloc(sizeof(quadIdx)); memcpy(owned.indices,quadIdx,sizeof(quadIdx));
    UploadMesh(&owned,false);
    Material meshMat=LoadMaterialDefault(); meshMat.maps[0].color=WHITE;
    BeginDrawing(); ClearBackground(BLACK);
    DrawMesh(owned,meshMat,MatrixTranslate(40,40,0));
    TakeScreenshot("backend-persistent.png"); EndDrawing();
    screen=LoadImage("backend-persistent.png");
    Expect(screen.data && Near(ScreenPixel(screen,60,60),WHITE),"uploaded mesh renders from persistent buffer at its transform");
    Expect(screen.data && Near(ScreenPixel(screen,10,10),BLACK),"uploaded mesh respects the model transform");
    UnloadImage(screen);
    UnloadMesh(owned); UnloadMaterial(meshMat);

    UnloadMaterial(material); UnloadImage(source); UnloadTexture(texture);
    CloseWindow(); CloseWindow();
    InitWindow(160,120,"Reinitialize"); BeginDrawing(); ClearBackground(BLACK); EndDrawing(); CloseWindow();
    Expect(!IsWindowReady(),"window can close twice and reopen");
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
