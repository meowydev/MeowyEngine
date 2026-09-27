#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"
#include "backend/pixel_conversion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace meowyrender {
namespace {
Vector3 Direction(int face,float u,float v) {
    switch(face) {case 0:return {1,-v,-u};case 1:return {-1,-v,u};case 2:return {u,1,v};
                 case 3:return {u,-1,-v};case 4:return {u,-v,1};default:return {-u,-v,-1};}
}
Shader SkyShader() {
    if(GetBackendType()==BackendType::Metal) return LoadShaderFromMemory(R"(
#include <metal_stdlib>
using namespace metal;
struct In {float3 position [[attribute(0)]];float2 uv [[attribute(1)]];float4 color [[attribute(2)]];};
struct Out {float4 position [[position]];float3 direction;float4 color;};
struct Matrices {float4x4 projection,modelview;};
vertex Out vs_main(In in [[stage_in]],constant Matrices& m [[buffer(1)]]) {
    Out out;float3x3 rotation(m.modelview[0].xyz,m.modelview[1].xyz,m.modelview[2].xyz);
    float4 clip=m.projection*float4(rotation*in.position,1);out.position=float4(clip.xy,clip.w,clip.w);
    out.direction=in.position;out.color=in.color;return out;
})",R"(
fragment float4 fs_main(Out in [[stage_in]],texturecube<float> sky [[texture(0)]],sampler s [[sampler(0)]]) {return sky.sample(s,in.direction)*in.color;})");
    if(GetBackendType()==BackendType::Vulkan)return LoadShaderFromMemory(R"(#version 450
layout(location=0) in vec3 position;layout(location=2) in vec4 color;
layout(location=0) out vec3 direction;layout(location=1) out vec4 tint;
layout(push_constant) uniform Matrices {mat4 projection;mat4 modelview;} m;
void main(){vec4 clip=m.projection*vec4(mat3(m.modelview)*position,1);gl_Position=vec4(clip.x,-clip.y,clip.w,clip.w);direction=position;tint=color;})",R"(#version 450
layout(location=0) in vec3 direction;layout(location=1) in vec4 tint;layout(location=0) out vec4 color;
layout(set=0,binding=0) uniform samplerCube sky;
void main(){color=texture(sky,direction)*tint;})");
    return LoadShaderFromMemory(R"(#version 330 core
layout(location=0) in vec3 position;layout(location=2) in vec4 color;
uniform mat4 uProjection,uModelview;out vec3 direction;out vec4 tint;
void main(){vec4 clip=uProjection*vec4(mat3(uModelview)*position,1);gl_Position=clip.xyww;direction=position;tint=color;})",R"(#version 330 core
in vec3 direction;in vec4 tint;uniform samplerCube uTexture;out vec4 color;
void main(){color=texture(uTexture,direction)*tint;})");
}
}
TextureCubemap LoadTextureCubemap(Image image,CubemapLayout layout) {
    auto& s=detail::State();
    if(!s.backend||!image.data||image.width<=0||image.height<=0)return {};
    if(layout==CubemapLayout::AutoDetect) {
        if(image.height==image.width*6)layout=CubemapLayout::LineVertical;
        else if(image.width==image.height*6)layout=CubemapLayout::LineHorizontal;
        else if(image.width%4==0&&image.width/4==image.height/3&&image.height%3==0)layout=CubemapLayout::CrossFourByThree;
        else if(image.width%3==0&&image.width/3==image.height/4&&image.height%4==0)layout=CubemapLayout::CrossThreeByFour;
        else if(image.width==image.height*2)layout=CubemapLayout::Panorama;
        else throw std::invalid_argument("Cannot determine cubemap image layout");
    }
    int columns=1,rows=6;
    switch(layout) {case CubemapLayout::LineVertical:break;case CubemapLayout::LineHorizontal:columns=6;rows=1;break;
        case CubemapLayout::CrossThreeByFour:columns=3;rows=4;break;case CubemapLayout::CrossFourByThree:columns=4;rows=3;break;
        case CubemapLayout::Panorama:columns=4;rows=2;break;default:throw std::invalid_argument("Invalid cubemap layout");}
    int size=image.width/columns;
    if(size<=0||size>8192||image.width%columns||image.height%rows||image.height/rows!=size)throw std::invalid_argument("Cubemap layout requires equal square faces (panorama 2:1)");
    auto pixels=backend::ConvertUpload(image.data,image.width,image.height,image.format);
    if(!pixels.valid)throw std::invalid_argument("Cubemap source must be an uncompressed image");
    if(pixels.floating) {
        pixels.bytes.resize(pixels.floats.size());
        for(size_t i=0;i<pixels.floats.size();++i)pixels.bytes[i]=static_cast<unsigned char>(std::isnan(pixels.floats[i])?0:std::clamp(pixels.floats[i],0.0f,1.0f)*255+0.5f);
    }
    std::vector<unsigned char> faces(static_cast<size_t>(size)*size*6*4);
    const int crossX[]={2,0,1,1,1,3},crossY[]={1,1,0,2,1,1};
    for(int face=0;face<6;++face)for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
        int sx=x,sy=y;
        if(layout==CubemapLayout::Panorama) {
            Vector3 direction=Vector3Normalize(Direction(face,2*(x+0.5f)/size-1,2*(y+0.5f)/size-1));
            float u=0.5f+std::atan2(direction.z,direction.x)/(2*PI),v=std::acos(std::clamp(direction.y,-1.0f,1.0f))/PI;
            sx=std::clamp(static_cast<int>(u*image.width),0,image.width-1);sy=std::clamp(static_cast<int>(v*image.height),0,image.height-1);
        } else if(layout==CubemapLayout::LineVertical)sy+=face*size;
        else if(layout==CubemapLayout::LineHorizontal)sx+=face*size;
        else if(layout==CubemapLayout::CrossThreeByFour&&face==5){sx=size+(size-1-x);sy=3*size+(size-1-y);}
        else {sx+=crossX[face]*size;sy+=crossY[face]*size;}
        std::copy_n(pixels.bytes.data()+(static_cast<size_t>(sy)*image.width+sx)*4,4,faces.data()+((static_cast<size_t>(face)*size+y)*size+x)*4);
    }
    TextureCubemap cube;cube.id=s.backend->CreateCubemap(faces.data(),size);cube.width=cube.height=size;
    if(cube.id)s.backend->SetTextureWrap(cube.id,static_cast<int>(TextureWrap::Clamp));
    return cube;
}
Image LoadImageFromCubemapFace(TextureCubemap cubemap,int face) {
    detail::FlushBatch();auto& s=detail::State();return s.backend?s.backend->ReadCubemapFace(cubemap.id,face):Image{};
}
void DrawSkybox(TextureCubemap cube,Color tint) {
    auto& s=detail::State();if(!s.backend||!cube.id)return;
    detail::FlushBatch();
    if(!s.skyboxShader){s.skyboxShader=SkyShader().id;if(!s.skyboxShader)throw std::runtime_error("Skybox shader creation failed");}
    backend::Vertex vertices[36]{};
    const int corners[]={0,1,2,0,2,3};const Vector2 uv[]={{-1,-1},{1,-1},{1,1},{-1,1}};
    for(int face=0;face<6;++face)for(int c=0;c<6;++c) {
        auto p=Direction(face,uv[corners[c]].x,uv[corners[c]].y);auto& v=vertices[face*6+c];
        v.x=p.x;v.y=p.y;v.z=p.z;v.r=tint.r;v.g=tint.g;v.b=tint.b;v.a=tint.a;
    }
    s.backend->SetActiveShader(s.skyboxShader);
    try {detail::SubmitVertices(vertices,36,backend::DrawMode::Triangles,cube.id);}
    catch(...){s.backend->SetActiveShader(s.activeShader);throw;}
    s.backend->SetActiveShader(s.activeShader);
}
}
