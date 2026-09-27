#include <meowyrender/meowyrender.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace meowyrender;
int main() {
    int failures=0;
    auto expect=[&](bool ok,const char* message){std::printf("%s: %s\n",ok?"PASS":"FAIL",message); if(!ok) ++failures;};
    InitWindow(240,160,"Skeletal animation checks");
    Model model=LoadModel(MEOWY_TEST_ASSETS "/skinned_triangle.gltf");
    int count=0; auto* animations=LoadModelAnimations(MEOWY_TEST_ASSETS "/skinned_triangle.gltf",&count);
    expect(model.meshCount==1 && model.boneCount==3 && count==3,"glTF mesh, joint hierarchy and clips load");
    if(model.meshCount!=1 || count!=3) {UnloadModel(model); UnloadModelAnimations(animations,count); CloseWindow(); return 1;}
    expect(std::abs(GetModelBoundingBox(model).min.x-15)<0.001f,"bind pose includes parent-node transform");
    expect(IsModelAnimationValid(model,animations[0]),"animation matches skeleton");
    UpdateModelAnimation(model,animations[0],30);
    expect(std::abs(GetModelBoundingBox(model).min.x-65)<0.001f,"linear keyframe interpolation deforms mesh");
    model.materials[0].maps[0].color=GREEN;
    BeginDrawing(); ClearBackground(BLACK);
    DrawModel(model,{0,60,0},1,WHITE);
    TakeScreenshot("animation-check.png"); EndDrawing();
    Image screenshot=LoadImage("animation-check.png");
    Color pixel=GetImageColor(screenshot,70*screenshot.width/240,80*screenshot.height/160);
    expect(screenshot.data && pixel.g==GREEN.g,"skinned rendering agrees with CPU bounds and model transform");
    UnloadImage(screenshot);
    UpdateModelAnimation(model,animations[1],30);
    expect(std::abs(GetModelBoundingBox(model).min.x-15)<0.001f,"STEP holds the previous pose");
    UpdateModelAnimation(model,animations[2],15);
    expect(std::abs(GetModelBoundingBox(model).min.x-30.625f)<0.001f,"CUBICSPLINE uses Hermite interpolation");
    UpdateModelAnimation(model,animations[0],60);
    expect(std::abs(GetModelBoundingBox(model).min.x-115)<0.001f,"last animation key is reachable");
    UpdateModelAnimation(model,animations[0],61);
    expect(std::abs(GetModelBoundingBox(model).min.x-15)<0.001f,"frame index wraps safely");
    UnloadModelAnimations(animations,count); UnloadModel(model);
    model=LoadModel(MEOWY_TEST_ASSETS "/animated_scene.gltf");
    animations=LoadModelAnimations(MEOWY_TEST_ASSETS "/animated_scene.gltf",&count);
    expect(model.meshCount==1&&model.boneCount==4&&count==5,"inactive glTF scene is excluded without losing skeleton indices");
    if(count==5){
        UpdateModelAnimation(model,animations[3],30);
        expect(std::abs(GetModelBoundingBox(model).min.x-(5-40*std::sqrt(0.5f)))<0.002f,"quaternion interpolation rotates the hierarchy");
        UpdateModelAnimation(model,animations[3],60);
        expect(std::abs(GetModelBoundingBox(model).min.x+45)<0.002f,"rotation endpoint is exact");
        UpdateModelAnimation(model,animations[4],30);
        expect(std::abs(GetModelBoundingBox(model).min.x-20)<0.002f,"scale interpolation preserves parent translation");
    }
    UnloadModelAnimations(animations,count);UnloadModel(model);CloseWindow();
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
