#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
using namespace meowyrender;
int main(){
    SetConfigFlags(FLAG_MSAA_4X_HINT);InitWindow(160,120,"MSAA coverage checks");
    BeginDrawing();ClearBackground(BLACK);DrawTriangle({1,1},{10,100},{150,10},WHITE);
    TakeScreenshot("msaa-coverage.png");EndDrawing();Image image=LoadImage("msaa-coverage.png");int partial=0;
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){auto c=GetImageColor(image,x,y);if(c.r>10&&c.r<240)++partial;}
    bool ok=image.data&&partial>10;std::printf("%s: multisample edge coverage (%d partial pixels)\n",ok?"PASS":"FAIL",partial);
    UnloadImage(image);CloseWindow();return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
