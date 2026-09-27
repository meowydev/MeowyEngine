#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <algorithm>
using namespace meowyrender;
static int failures = 0;
void Expect(bool condition, const char* label) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) ++failures;
}
int main() {
    if(std::getenv("MEOWY_TEST_MSAA")) SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(320, 240, "MeowyRender pixel checks");
    for(auto format:{PixelFormat::Compressed_PVRT_RGB,PixelFormat::Compressed_PVRT_RGBA}) {
        if(!IsTextureFormatSupported(format)){std::printf("SKIP: PVRTC unavailable on this device\n");continue;}
        unsigned char blocks[32]{};Image input;input.data=blocks;input.width=input.height=8;input.format=format;
        auto texture=LoadTextureFromImage(input);auto decoded=LoadImageFromTexture(texture);
        Color pixel=GetImageColor(decoded,3,3);
        Expect(texture.id&&decoded.data&&pixel.r==0&&pixel.g==0&&pixel.b==0,"PVRTC 4bpp upload and decoded readback");
        UpdateTexture(texture,blocks);UnloadImage(decoded);UnloadTexture(texture);
    }
    Expect(IsWindowReady(), "window initializes");
    for(auto format:{PixelFormat::Compressed_DXT1_RGB,PixelFormat::Compressed_DXT1_RGBA,PixelFormat::Compressed_DXT3_RGBA,PixelFormat::Compressed_DXT5_RGBA,PixelFormat::Compressed_ETC1_RGB,PixelFormat::Compressed_ETC2_RGB,PixelFormat::Compressed_ETC2_EAC_RGBA,PixelFormat::Compressed_ASTC_4x4_RGBA,PixelFormat::Compressed_ASTC_8x8_RGBA}) {
        if(!IsTextureFormatSupported(format)) {std::printf("SKIP: compressed format %d unavailable on this GPU\n",static_cast<int>(format)); continue;}
        unsigned char block[16]{};
        int colorOffset=0;
        if(format==PixelFormat::Compressed_DXT3_RGBA) {std::memset(block,255,8); colorOffset=8;}
        if(format==PixelFormat::Compressed_DXT5_RGBA) {block[0]=255; colorOffset=8;}
        block[colorOffset+1]=248;
        bool etc=format>=PixelFormat::Compressed_ETC1_RGB && format<=PixelFormat::Compressed_ETC2_EAC_RGBA;
        if(etc) {std::memset(block,0,16); if(format==PixelFormat::Compressed_ETC2_EAC_RGBA) block[0]=255;}
        if(format==PixelFormat::Compressed_ASTC_4x4_RGBA || format==PixelFormat::Compressed_ASTC_8x8_RGBA) {
            // LDR void-extent block containing an opaque constant red value.
            const unsigned char red[]={0xfc,0xfd,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0,0,0,0,0xff,0xff};
            std::memcpy(block,red,16);
        }
        Image compressed{}; compressed.data=block; compressed.width=4; compressed.height=4; compressed.format=format;
        if(format==PixelFormat::Compressed_ASTC_8x8_RGBA) compressed.width=compressed.height=8;
        Texture texture=LoadTextureFromImage(compressed);
        Expect(texture.id!=0,"native compressed texture upload");
        Image decoded=LoadImageFromTexture(texture);
        Color decodedPixel=GetImageColor(decoded,1,1);
        Expect(decoded.data && decodedPixel.r==(etc?2:255) && decodedPixel.g==(etc?2:0),"compressed texture readback decodes texels");
        UnloadImage(decoded);
        BeginDrawing(); ClearBackground(BLACK); DrawTextureEx(texture,{0,0},0,20,WHITE);
        TakeScreenshot("compressed-check.png"); EndDrawing();
        Image pixels=LoadImage("compressed-check.png"); Color pixel=GetImageColor(pixels,20*pixels.width/320,20*pixels.height/240);
        Expect(pixels.data && pixel.r==(etc?2:255) && pixel.g==(etc?2:0) && pixel.b==(etc?2:0),"GPU samples compressed texture correctly");
        UpdateTexture(texture,block); UnloadImage(pixels); UnloadTexture(texture);
    }
    for(int f=1;f<=13;++f) {
        auto format=static_cast<PixelFormat>(f);
        std::vector<unsigned char> data;
        auto append=[&](auto value){const auto* p=reinterpret_cast<const unsigned char*>(&value); data.insert(data.end(),p,p+sizeof(value));};
        for(int i=0;i<6;++i) {
            switch(format) {
                case PixelFormat::Uncompressed_Grayscale: append(static_cast<unsigned char>(255)); break;
                case PixelFormat::Uncompressed_GrayAlpha: append(static_cast<unsigned char>(255)); append(static_cast<unsigned char>(255)); break;
                case PixelFormat::Uncompressed_R5G6B5: append(static_cast<unsigned short>(0xf800)); break;
                case PixelFormat::Uncompressed_R8G8B8: append(static_cast<unsigned char>(255)); append(static_cast<unsigned char>(0)); append(static_cast<unsigned char>(0)); break;
                case PixelFormat::Uncompressed_R5G5B5A1: append(static_cast<unsigned short>(0xf801)); break;
                case PixelFormat::Uncompressed_R4G4B4A4: append(static_cast<unsigned short>(0xf00f)); break;
                case PixelFormat::Uncompressed_R8G8B8A8: append(static_cast<unsigned char>(255)); append(static_cast<unsigned char>(0)); append(static_cast<unsigned char>(0)); append(static_cast<unsigned char>(255)); break;
                default: {
                    bool half=f>=11; int kind=f-(half?11:8); int channels=kind==0?1:kind==1?3:4;
                    for(int c=0;c<channels;++c) {
                        if(half) append(static_cast<unsigned short>((c==0 || c==3)?0x3c00:0));
                        else append((c==0 || c==3)?1.0f:0.0f);
                    }
                }
            }
        }
        Image input{}; input.data=data.data(); input.width=3; input.height=2; input.format=format;
        Texture uploaded=LoadTextureFromImage(input);
        Image result=LoadImageFromTexture(uploaded);
        Color value=GetImageColor(result,2,1);
        bool gray=f==1 || f==2 || f==8 || f==11;
        Expect(uploaded.id && result.data && value.r==255 && value.g==(gray?255:0) && value.b==(gray?255:0),"uncompressed texture format upload/readback");
        UpdateTexture(uploaded,data.data());
        UnloadImage(result); UnloadTexture(uploaded);
    }
    Image source = GenImageColor(8, 8, Color{13, 79, 203, 255});
    Texture texture = LoadTextureFromImage(source);
    Image roundtrip = LoadImageFromTexture(texture);
    Color pixel = GetImageColor(roundtrip, 3, 4);
    Expect(roundtrip.data && pixel.r == 13 && pixel.g == 79 && pixel.b == 203, "texture upload/readback round trip");
    UnloadImage(roundtrip); UnloadImage(source);
    auto target = LoadRenderTexture(64, 64);
    Expect(target.id != 0, "render target creation");
    BeginDrawing();
    ClearBackground(BLACK);
    BeginTextureMode(target);
    ClearBackground(Color{17, 33, 65, 255});
    DrawRectangle(16, 16, 32, 32, Color{221, 111, 51, 255});
    EndTextureMode();
    Image rendered = LoadImageFromTexture(target.texture);
    Color background = GetImageColor(rendered, 4, 4);
    Color center = GetImageColor(rendered, 32, 32);
    Expect(rendered.data && background.r == 17 && background.b == 65, "offscreen clear pixels");
    Expect(rendered.data && center.r == 221 && center.g == 111 && center.b == 51, "offscreen geometry pixels");
    DrawTexture(target.texture, 0, 0, WHITE);
    TakeScreenshot("render-check.png");
    EndDrawing();
    Image screenshot = LoadImage("render-check.png");
    Expect(screenshot.data != nullptr, "screenshot export and decode");
    UnloadImage(screenshot); UnloadImage(rendered);
    UnloadRenderTexture(target); UnloadTexture(texture);
    // Backend-native custom-shader source, selected from the ACTIVE backend at
    // runtime (works for single- and multi-backend builds).
    const char* vsMetal=R"(
        #include <metal_stdlib>
        using namespace metal;
        struct Input {float3 position [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 color [[attribute(2)]];};
        struct Output {float4 position [[position]];};
        struct Matrices {float4x4 projection; float4x4 modelview;};
        struct Values {float amount;};
        vertex Output vs_main(Input v [[stage_in]],constant Matrices& m [[buffer(1)]]) {
            Output o; o.position=m.projection*m.modelview*float4(v.position,1); o.position.z=(o.position.z+o.position.w)*0.5; return o;
        }
    )";
    const char* fsMetal=R"(
        fragment float4 fs_main(Output v [[stage_in]],constant Values& values [[buffer(2)]]) {
            return float4(values.amount,0,1-values.amount,1);
        }
    )";
    const char* vsVulkan=R"(#version 450
        layout(location=0) in vec3 position;
        layout(push_constant) uniform Matrices {mat4 projection;mat4 modelview;} m;
        void main(){gl_Position=m.projection*m.modelview*vec4(position,1);gl_Position.y=-gl_Position.y;gl_Position.z=(gl_Position.z+gl_Position.w)*0.5;}
    )";
    const char* fsVulkan=R"(#version 450
        layout(set=0,binding=1,std140) uniform Parameters {float amount;} p;
        layout(location=0) out vec4 result;
        void main(){result=vec4(p.amount,0,1-p.amount,1);}
    )";
    const char* vsGL=R"(#version 330 core
        layout(location=0) in vec3 aPos;
        uniform mat4 uProjection; uniform mat4 uModelview;
        void main(){gl_Position=uProjection*uModelview*vec4(aPos,1);}
    )";
    const char* fsGL=R"(#version 330 core
        uniform float amount; out vec4 result;
        void main(){result=vec4(amount,0,1-amount,1);}
    )";
    const char* vs; const char* fs;
    switch(GetActiveBackend()) {
        case Backend::Metal:  vs=vsMetal;  fs=fsMetal;  break;
        case Backend::Vulkan: vs=vsVulkan; fs=fsVulkan; break;
        default:              vs=vsGL;     fs=fsGL;     break;
    }
    Shader shader=LoadShaderFromMemory(vs,fs);
    Expect(IsShaderValid(shader),"custom shader compiles");
    BeginDrawing(); ClearBackground(BLACK); BeginShaderMode(shader);
    SetShaderValue(shader,"amount",1.0f); DrawRectangle(0,0,100,100,WHITE);
    SetShaderValue(shader,"amount",0.0f); DrawRectangle(100,0,100,100,WHITE);
    EndShaderMode(); TakeScreenshot("shader-check.png"); EndDrawing();
    Image shaderImage=LoadImage("shader-check.png");
    Color left=GetImageColor(shaderImage,50*shaderImage.width/320,50*shaderImage.height/240);
    Color right=GetImageColor(shaderImage,150*shaderImage.width/320,50*shaderImage.height/240);
    Expect(shaderImage.data && left.r==255 && left.b==0 && right.r==0 && right.b==255,"named shader uniforms preserve per-draw values");
    UnloadImage(shaderImage); UnloadShader(shader);
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
    UnloadImage(lit); UnloadMaterial(pbr);

    // DrawTextPro must actually rotate the run. Drawn at 90 degrees from an
    // anchor, a long horizontal string becomes a vertical column: pixels appear
    // below the anchor (rotated) and NOT far to its right (where unrotated text
    // would land).
    BeginDrawing(); ClearBackground(BLACK);
    DrawTextPro(GetFontDefault(),"WWWWWWWWWWWW",{40,20},{0,0},90.0f,24.0f,2.0f,WHITE);
    TakeScreenshot("textpro.png"); EndDrawing();
    Image tp=LoadImage("textpro.png");
    auto lum=[](Color c){return c.r+c.g+c.b;};
    // Column of text runs downward from (40,20); sample well below the anchor.
    // Scan the full image: rotated text should have lit pixels whose vertical
    // extent (below the anchor) exceeds their horizontal extent (rotated run).
    int minX=tp.width,maxX=0,minY=tp.height,maxY=0,litCount=0;
    for(int yy=0;yy<tp.height;++yy)for(int xx=0;xx<tp.width;++xx){
        if(lum(GetImageColor(tp,xx,yy))>60){++litCount;minX=std::min(minX,xx);maxX=std::max(maxX,xx);minY=std::min(minY,yy);maxY=std::max(maxY,yy);}
    }
    const int spanX=maxX-minX,spanY=maxY-minY;
    // Rotated 90 degrees: the run is taller than it is wide, and its lit pixels
    // start at/near the anchor x and extend downward, not far to the right.
    Expect(tp.data && litCount>0 && spanY>spanX && minX < 60*tp.width/320,"DrawTextPro rotates the text run about its origin");
    UnloadImage(tp);
    CloseWindow();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
