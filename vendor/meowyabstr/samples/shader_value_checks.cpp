#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
using namespace meowyrender;
int main() {
    InitWindow(160,120,"Shader values");
    // Custom-shader source is backend-native. With runtime backend selection the
    // dialect is chosen from the ACTIVE backend (GetActiveBackend) rather than a
    // compile-time macro, so this test works in single- and multi-backend builds.
    const char* vs=nullptr; const char* fs=nullptr;
    const char* vsMetal=R"(
#include <metal_stdlib>
using namespace metal;
struct In{float3 position [[attribute(0)]];};struct Out{float4 position [[position]];};
struct Matrices{float4x4 projection,modelview;};struct Params{float4x4 transform;float3 colors[2];float gains[2];int selected;};
vertex Out vs_main(In in [[stage_in]],constant Matrices& m [[buffer(1)]],constant Params& p [[buffer(2)]]){
    Out out;out.position=m.projection*m.modelview*p.transform*float4(in.position,1);out.position.z=(out.position.z+out.position.w)*0.5;return out;})";
    const char* fsMetal=R"(fragment float4 fs_main(Out in [[stage_in]],constant Params& p [[buffer(2)]]){return float4(p.colors[p.selected]*p.gains[p.selected],1);})";
    const char* vsVulkan=R"(#version 450
layout(location=0)in vec3 position;
layout(push_constant)uniform Matrices{mat4 projection;mat4 modelview;}m;
layout(set=0,binding=1,std140)uniform Params{mat4 transform;vec3 colors[2];float gains[2];int selected;}p;
void main(){gl_Position=m.projection*m.modelview*p.transform*vec4(position,1);gl_Position.y=-gl_Position.y;gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;})";
    const char* fsVulkan=R"(#version 450
layout(set=0,binding=1,std140)uniform Params{mat4 transform;vec3 colors[2];float gains[2];int selected;}p;
layout(location=0)out vec4 result;void main(){result=vec4(p.colors[p.selected]*p.gains[p.selected],1);})";
    const char* vsGL=R"(#version 330 core
layout(location=0)in vec3 position;uniform mat4 uProjection,uModelview,transform;
void main(){gl_Position=uProjection*uModelview*transform*vec4(position,1);})";
    const char* fsGL=R"(#version 330 core
uniform vec3 colors[2];uniform float gains[2];uniform int selected;out vec4 result;
void main(){result=vec4(colors[selected]*gains[selected],1);})";
    switch(GetActiveBackend()) {
        case Backend::Metal:  vs=vsMetal;  fs=fsMetal;  break;
        case Backend::Vulkan: vs=vsVulkan; fs=fsVulkan; break;
        default:              vs=vsGL;     fs=fsGL;     break; // OpenGL
    }
    auto shader=LoadShaderFromMemory(vs,fs);
    bool reflected=shader.id&&GetShaderLocation(shader,"colors")>=0&&GetShaderLocation(shader,"gains")>=0&&GetShaderLocation(shader,"transform")>=0;
    Vector3 colors[]={{1,0,0},{0,1,0}};float gains[]={0.5f,1};
    SetShaderValueV(shader,GetShaderLocation(shader,"colors"),colors,ShaderUniformType::Vec3,2);
    SetShaderValueV(shader,GetShaderLocation(shader,"gains"),gains,ShaderUniformType::Float,2);
    SetShaderValue(shader,"transform",MatrixTranslate(20,0,0));
    BeginDrawing();ClearBackground(BLACK);BeginShaderMode(shader);
    SetShaderValue(shader,"selected",0);DrawRectangle(0,0,20,20,WHITE);
    SetShaderValue(shader,"selected",1);DrawRectangle(40,0,20,20,WHITE);
    EndShaderMode();TakeScreenshot("shader-values.png");EndDrawing();
    auto image=LoadImage("shader-values.png");auto red=GetImageColor(image,30*image.width/160,10*image.height/120);
    auto green=GetImageColor(image,70*image.width/160,10*image.height/120);auto empty=GetImageColor(image,10*image.width/160,10*image.height/120);
    bool ok=reflected&&image.data&&red.r>=127&&red.r<=128&&red.g==0&&green.g==255&&green.r==0&&empty.r==0;
    std::printf("%s: matrix transforms, vec3/scalar arrays and per-draw integer uniforms\n",ok?"PASS":"FAIL");
    UnloadImage(image);UnloadShader(shader);CloseWindow();return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
