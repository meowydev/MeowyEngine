#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
using namespace meowyrender;
int main() {
    int failures=0;
    auto expect=[&](bool ok,const char* message){std::printf("%s: %s\n",ok?"PASS":"FAIL",message); if(!ok) ++failures;};
    InitWindow(160,120,"Offscreen checks");
    auto target=LoadRenderTexture(64,64);
    expect(target.id!=0,"offscreen target created");
    BeginDrawing(); ClearBackground(BLUE);
    BeginTextureMode(target); ClearBackground(RED); DrawRectangle(16,16,32,32,GREEN);
    BeginScissorMode(0,0,64,8); DrawRectangle(0,0,64,64,WHITE); EndScissorMode(); EndTextureMode();
    Image rendered=LoadImageFromTexture(target.texture);
    Color center=GetImageColor(rendered,32,32),corner=GetImageColor(rendered,2,62);
    expect(rendered.data && center.g==GREEN.g && corner.r==RED.r,"offscreen clear and geometry read back");
    expect(GetImageColor(rendered,10,2).g==255&&GetImageColor(rendered,10,60).g==RED.g,"offscreen readback has top-left origin and correct scissor");
    UnloadImage(rendered);
    DrawTexture(target.texture,0,0,WHITE); TakeScreenshot("offscreen-check.png"); EndDrawing();
    Image screen=LoadImage("offscreen-check.png");
    center=GetImageColor(screen,32*screen.width/160,32*screen.height/120);
    Color background=GetImageColor(screen,100*screen.width/160,90*screen.height/120);
    expect(screen.data && center.g==GREEN.g && background.b==BLUE.b,"offscreen sampling preserves screen contents across target switch");
    expect(GetImageColor(screen,10*screen.width/160,2*screen.height/120).g==255&&GetImageColor(screen,10*screen.width/160,60*screen.height/120).g==RED.g,"render textures draw upright without negative source height");
    UnloadImage(screen); UnloadRenderTexture(target); CloseWindow();
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
