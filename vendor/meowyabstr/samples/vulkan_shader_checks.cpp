#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
using namespace meowyrender;
int main() {
    // This test exercises Vulkan-specific shader/reflection behavior, so it
    // explicitly selects the Vulkan backend (in a multi-backend build the
    // automatic choice would be Metal). Skips cleanly if Vulkan can't init.
    SetPreferredBackend(Backend::Vulkan);
    try { InitWindow(160,120,"Vulkan custom shader checks"); }
    catch (const std::exception& e) { std::printf("SKIP: Vulkan unavailable (%s)\n", e.what()); return 0; }
    if (GetActiveBackend() != Backend::Vulkan) { std::printf("SKIP: Vulkan backend not active\n"); CloseWindow(); return 0; }
    const char* vs=R"(#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=0) out vec2 texcoord;
layout(location=1) out vec4 tint;
layout(push_constant) uniform Transforms {mat4 projection;mat4 modelview;} transforms;
void main(){gl_Position=transforms.projection*transforms.modelview*vec4(position,1);gl_Position.y=-gl_Position.y;gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;texcoord=uv;tint=color;})";
    const char* fs=R"(#version 450
layout(location=0) in vec2 texcoord;
layout(location=1) in vec4 tint;
layout(location=0) out vec4 outputColor;
layout(set=0,binding=0) uniform sampler2D albedo;
layout(set=0,binding=1,std140) uniform Parameters {vec4 colors[2];float amount;} parameters;
void main(){outputColor=texture(albedo,texcoord)*tint*mix(parameters.colors[0],parameters.colors[1],parameters.amount);})";
    Shader shader=LoadShaderFromMemory(vs,fs);
    int failures=0;
    auto expect=[&](bool ok,const char* label){std::printf("%s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    expect(shader.id!=0,"runtime GLSL compilation and pipeline creation");
    Vector4 colors[]={{1,0,0,1},{0,1,0,1}};
    int location=GetShaderLocation(shader,"colors");
    expect(location>=0&&GetShaderLocation(shader,"amount")>=0,"named uniform and std140 array reflection");
    SetShaderValueV(shader,location,colors,ShaderUniformType::Vec4,2);
    auto target=LoadRenderTexture(64,64);
    BeginDrawing();ClearBackground(BLUE);
    BeginShaderMode(shader);
    SetShaderValue(shader,"amount",0.0f);DrawRectangle(0,0,40,40,WHITE);
    SetShaderValue(shader,"amount",1.0f);DrawRectangle(40,0,40,40,WHITE);
    EndShaderMode();
    BeginTextureMode(target);ClearBackground(BLACK);BeginShaderMode(shader);DrawRectangle(0,0,64,64,WHITE);EndShaderMode();EndTextureMode();
    Image offscreen=LoadImageFromTexture(target.texture);
    auto green=GetImageColor(offscreen,10,10);
    expect(offscreen.data&&green.g==255&&green.r==0,"custom offscreen pipeline");UnloadImage(offscreen);
    DrawRectangle(0,80,20,20,RED);
    BeginShaderMode(shader);
    for(int i=0;i<2200;++i){SetShaderValue(shader,"amount",1.0f);DrawRectangle(100,80,10,10,WHITE);}
    EndShaderMode();
    TakeScreenshot("vulkan-shader-check.png");EndDrawing();
    Image screen=LoadImage("vulkan-shader-check.png");
    auto red=GetImageColor(screen,20*screen.width/160,20*screen.height/120);
    green=GetImageColor(screen,60*screen.width/160,20*screen.height/120);
    expect(screen.data&&red.r==255&&red.g==0&&green.g==255&&green.r==0,"per-draw uniform snapshots preserve earlier draws");
    auto preserved=GetImageColor(screen,10*screen.width/160,90*screen.height/120);
    auto final=GetImageColor(screen,105*screen.width/160,85*screen.height/120);
    expect(preserved.r==RED.r&&preserved.g==RED.g&&final.g==255,"transient resource recycling preserves 2200 draws");UnloadImage(screen);
    expect(!LoadShaderFromMemory("invalid",fs).id,"invalid source is rejected cleanly");
    UnloadRenderTexture(target);UnloadShader(shader);CloseWindow();return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
