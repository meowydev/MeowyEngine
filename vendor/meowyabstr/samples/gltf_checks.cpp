// glTF feature regression checks: alpha modes, secondary UVs, KHR_texture_
// transform, and >4 joint influences. The fixtures are generated in-process
// (base64 buffers) so the real cgltf load path is exercised end to end.
#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cmath>
using namespace meowyrender;

static int failures=0;
static void Expect(bool ok,const char* msg){std::printf("%s: %s\n",ok?"PASS":"FAIL",msg);if(!ok)++failures;}

// Minimal base64 encoder for the data: buffer URI.
static std::string Base64(const std::vector<unsigned char>& bytes){
    static const char* t="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; int val=0,bits=-6;
    for(unsigned char c:bytes){val=(val<<8)+c;bits+=8;while(bits>=0){out.push_back(t[(val>>bits)&0x3F]);bits-=6;}}
    if(bits>-6) out.push_back(t[((val<<8)>>(bits+8))&0x3F]);
    while(out.size()%4) out.push_back('=');
    return out;
}
static void PushFloats(std::vector<unsigned char>& b,std::initializer_list<float> v){
    for(float f:v){unsigned char* p=reinterpret_cast<unsigned char*>(&f);b.insert(b.end(),p,p+4);}
}
static void PushU16(std::vector<unsigned char>& b,std::initializer_list<unsigned short> v){
    for(unsigned short s:v){unsigned char* p=reinterpret_cast<unsigned char*>(&s);b.insert(b.end(),p,p+2);}
}

static std::string WriteFixture(const std::string& json,const std::string& name){
    auto path=(std::filesystem::temp_directory_path()/name).string();
    std::ofstream(path)<<json; return path;
}

int main(){
    InitWindow(64,64,"glTF feature checks");

    // ---- Fixture 1: a triangle with a MASK material + KHR_texture_transform +
    // a TEXCOORD_1 set on the base color texture. Single 1x1 white texture. ----
    {
        std::vector<unsigned char> buf;
        // positions (3 x vec3)
        std::size_t posOff=buf.size(); PushFloats(buf,{0,0,0, 1,0,0, 0,1,0});
        // TEXCOORD_0 (3 x vec2)
        std::size_t uv0Off=buf.size(); PushFloats(buf,{0,0, 1,0, 0,1});
        // TEXCOORD_1 (3 x vec2) distinct from uv0
        std::size_t uv1Off=buf.size(); PushFloats(buf,{0.25f,0.25f, 0.5f,0.25f, 0.25f,0.5f});
        // a tiny 1x1 white PNG is awkward; instead reference no texture and rely
        // on parsed fields. Use a data-uri 1x1 texture for base color.
        std::string b64=Base64(buf);
        // 1x1 white PNG (precomputed minimal PNG).
        const char* whitePng="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
        char json[4096];
        std::snprintf(json,sizeof(json),R"({
"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0}],
"materials":[{"alphaMode":"MASK","alphaCutoff":0.75,
  "pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":1,
     "extensions":{"KHR_texture_transform":{"offset":[0.1,0.2],"scale":[2.0,3.0],"rotation":0.5}}}}}],
"textures":[{"source":0}],"images":[{"uri":"%s"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1,"TEXCOORD_1":2},"material":0}]}],
"buffers":[{"uri":"data:application/octet-stream;base64,%s","byteLength":%zu}],
"bufferViews":[
 {"buffer":0,"byteOffset":%zu,"byteLength":36},
 {"buffer":0,"byteOffset":%zu,"byteLength":24},
 {"buffer":0,"byteOffset":%zu,"byteLength":24}],
"accessors":[
 {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
 {"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},
 {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}]})",
            whitePng,b64.c_str(),buf.size(),posOff,uv0Off,uv1Off);
        auto path=WriteFixture(json,"meowy-gltf-material.gltf");
        Model m=LoadModel(path);
        Expect(m.meshCount==1 && m.materialCount>=2,"material fixture loads one mesh + material");
        if(m.materialCount>=2){
            const Material& mat=m.materials[1];
            Expect(mat.alphaMode==MaterialAlphaMode::Mask,"alphaMode MASK parsed");
            Expect(std::abs(mat.alphaCutoff-0.75f)<0.001f,"alphaCutoff parsed");
            const MaterialMap& albedo=mat.maps[static_cast<int>(MaterialMapIndex::Albedo)];
            Expect(albedo.uvSet==1,"base color texCoord set (TEXCOORD_1) parsed");
            Expect(std::abs(albedo.uvOffset.x-0.1f)<0.001f && std::abs(albedo.uvOffset.y-0.2f)<0.001f,"KHR_texture_transform offset parsed");
            Expect(std::abs(albedo.uvScale.x-2.0f)<0.001f && std::abs(albedo.uvScale.y-3.0f)<0.001f,"KHR_texture_transform scale parsed");
            Expect(std::abs(albedo.uvRotation-0.5f)<0.001f,"KHR_texture_transform rotation parsed");
        }
        // The mesh must carry the secondary UV set (map references TEXCOORD_1).
        Expect(m.meshCount==1 && m.meshes[0].texcoords2!=nullptr,"secondary UV set stored on mesh");
        if(m.meshCount==1 && m.meshes[0].texcoords2)
            Expect(std::abs(m.meshes[0].texcoords2[0]-0.25f)<0.001f,"secondary UV values match TEXCOORD_1");
        UnloadModel(m);
    }

    // ---- Fixture 2: >4 joint influences via JOINTS_0/1 + WEIGHTS_0/1. The
    // loader must clamp to the four strongest and renormalize, not throw. ----
    {
        std::vector<unsigned char> buf;
        std::size_t posOff=buf.size(); PushFloats(buf,{0,0,0, 1,0,0, 0,1,0});
        // JOINTS_0 (3 x u16vec4) and JOINTS_1
        std::size_t j0Off=buf.size(); PushU16(buf,{0,1,2,3, 0,1,2,3, 0,1,2,3});
        std::size_t j1Off=buf.size(); PushU16(buf,{4,5,0,0, 4,5,0,0, 4,5,0,0});
        // WEIGHTS_0 and WEIGHTS_1 (8 influences; two are largest in set 1)
        std::size_t w0Off=buf.size(); PushFloats(buf,{0.1f,0.1f,0.05f,0.05f, 0.1f,0.1f,0.05f,0.05f, 0.1f,0.1f,0.05f,0.05f});
        std::size_t w1Off=buf.size(); PushFloats(buf,{0.4f,0.2f,0,0, 0.4f,0.2f,0,0, 0.4f,0.2f,0,0});
        std::string b64=Base64(buf);
        char json[3072];
        std::snprintf(json,sizeof(json),R"({
"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1,2,3,4,5,6]}],
"nodes":[{"name":"J0"},{"name":"J1"},{"name":"J2"},{"name":"J3"},{"name":"J4"},{"name":"J5"},
 {"name":"Mesh","mesh":0,"skin":0}],
"skins":[{"joints":[0,1,2,3,4,5]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"JOINTS_1":2,"WEIGHTS_0":3,"WEIGHTS_1":4}}]}],
"buffers":[{"uri":"data:application/octet-stream;base64,%s","byteLength":%zu}],
"bufferViews":[
 {"buffer":0,"byteOffset":%zu,"byteLength":36},
 {"buffer":0,"byteOffset":%zu,"byteLength":24},
 {"buffer":0,"byteOffset":%zu,"byteLength":24},
 {"buffer":0,"byteOffset":%zu,"byteLength":48},
 {"buffer":0,"byteOffset":%zu,"byteLength":48}],
"accessors":[
 {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
 {"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},
 {"bufferView":2,"componentType":5123,"count":3,"type":"VEC4"},
 {"bufferView":3,"componentType":5126,"count":3,"type":"VEC4"},
 {"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"}]})",
            b64.c_str(),buf.size(),posOff,j0Off,j1Off,w0Off,w1Off);
        auto path=WriteFixture(json,"meowy-gltf-joints.gltf");
        bool threw=false; Model m{};
        try { m=LoadModel(path); } catch(...) { threw=true; }
        Expect(!threw,">4 joint influences clamp instead of throwing");
        Expect(!threw && m.meshCount==1,"skinned >4-influence mesh loads one mesh");
        if(!threw) std::printf("  (meshCount=%d boneCount=%d)\n",m.meshCount,m.meshCount==1?m.meshes[0].boneCount:-1);
        if(!threw && m.meshCount==1){
            Mesh& mesh=m.meshes[0];
            // The four strongest influences across both sets are:
            // set1 j4=0.4, set1 j5=0.2, set0 j0=0.1, set0 j1=0.1 (sum=0.8).
            // After renormalizing they sum to 1. Verify weight sum per vertex.
            float sum=0; for(int k=0;k<4;++k) sum+=mesh.boneWeights[k];
            Expect(std::abs(sum-1.0f)<0.01f,"clamped joint weights renormalize to 1");
            // The dominant influence should be joint 4 (weight 0.4/0.8=0.5).
            bool hasStrongest=false; for(int k=0;k<4;++k) if(mesh.boneIds[k]==4 && std::abs(mesh.boneWeights[k]-0.5f)<0.02f) hasStrongest=true;
            Expect(hasStrongest,"strongest influence (joint 4) retained after clamp");
        }
        if(!threw) UnloadModel(m);
    }

    // ---- Fixture 4: a triangle with one morph target that displaces a vertex.
    // Verifies morph targets load (no longer rejected), default weights apply,
    // and SetMeshMorphWeights blends the delta. ----
    {
        std::vector<unsigned char> buf;
        std::size_t posOff=buf.size(); PushFloats(buf,{0,0,0, 2,0,0, 0,2,0}); // base triangle
        std::size_t dOff=buf.size();   PushFloats(buf,{0,0,0, 0,0,0, 0,3,0});  // target: move v2 up by +3
        std::string b64=Base64(buf);
        char json[3072];
        std::snprintf(json,sizeof(json),R"({
"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":1}]}],"weights":[0.0]}],
"buffers":[{"uri":"data:application/octet-stream;base64,%s","byteLength":%zu}],
"bufferViews":[
 {"buffer":0,"byteOffset":%zu,"byteLength":36},
 {"buffer":0,"byteOffset":%zu,"byteLength":36}],
"accessors":[
 {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[2,2,0]},
 {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0,3,0]}]})",
            b64.c_str(),buf.size(),posOff,dOff);
        auto path=WriteFixture(json,"meowy-gltf-morph.gltf");
        Model m=LoadModel(path);
        Expect(m.meshCount==1 && GetMeshMorphTargetCount(m.meshes[0])==1,"morph target loads (count=1)");
        if(m.meshCount==1 && GetMeshMorphTargetCount(m.meshes[0])==1){
            Mesh& mesh=m.meshes[0];
            Expect(mesh.morphWeights && std::abs(mesh.morphWeights[0]-0.0f)<0.001f,"default morph weight 0 applied");
            // Render with weight 0 (flat), then weight 1 (v2 pushed up), and
            // confirm a high pixel becomes covered only when the morph is active.
            // Orthographic camera looking straight down -Z at the XY plane, so
            // world Y maps directly to a screen row. The base apex is at y=2;
            // the morph raises it to y=5. A row corresponding to y~=3.5 is only
            // covered when the morph weight is applied.
            Camera3D cam{{1,2,10},{1,2,0},{0,1,0},8.0f,CameraProjection::Orthographic};
            Material mat=LoadMaterialDefault(); mat.maps[0].color=WHITE; mat.lighting=false;
            auto coverage=[&](float weight)->int{
                SetMeshMorphWeights(&mesh,&weight,1);
                BeginDrawing(); ClearBackground(BLACK);
                BeginMode3D(cam); DrawMesh(mesh,mat,MatrixIdentity()); EndMode3D();
                TakeScreenshot("morph-check.png"); EndDrawing();
                Image img=LoadImage("morph-check.png");
                // Count lit pixels in the upper region (world y in ~[3,5]).
                int lit=0;
                for(int y=0;y<img.height/3;++y) for(int x=0;x<img.width;++x){
                    Color c=GetImageColor(img,x,y); if(c.r>40||c.g>40||c.b>40) ++lit;
                }
                UnloadImage(img); return lit;
            };
            int flatLit=coverage(0.0f);
            int morphedLit=coverage(1.0f);
            std::printf("  (morph upper-region lit: weight0=%d weight1=%d)\n",flatLit,morphedLit);
            Expect(morphedLit>flatLit+20,"SetMeshMorphWeights deforms geometry (apex rises when weight=1)");
            UnloadMaterial(mat);
        }
        UnloadModel(m);
    }

    // ---- Fixture 5: morph-target WEIGHT ANIMATION. One target, a weight track
    // going 0 -> 1 over 1 second. Verifies LoadModelAnimations reads the weight
    // channel and UpdateModelAnimation drives the mesh's morph weight. ----
    {
        std::vector<unsigned char> buf;
        std::size_t posOff=buf.size(); PushFloats(buf,{0,0,0, 2,0,0, 0,2,0});
        std::size_t dOff=buf.size();   PushFloats(buf,{0,0,0, 0,0,0, 0,3,0}); // target delta
        std::size_t timeOff=buf.size(); PushFloats(buf,{0.0f, 1.0f});          // keyframe times
        std::size_t wOff=buf.size();   PushFloats(buf,{0.0f, 1.0f});          // weight outputs
        std::string b64=Base64(buf);
        char json[4096];
        std::snprintf(json,sizeof(json),R"({
"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":1}]}],"weights":[0.0]}],
"animations":[{"channels":[{"sampler":0,"target":{"node":0,"path":"weights"}}],
  "samplers":[{"input":2,"output":3,"interpolation":"LINEAR"}]}],
"buffers":[{"uri":"data:application/octet-stream;base64,%s","byteLength":%zu}],
"bufferViews":[
 {"buffer":0,"byteOffset":%zu,"byteLength":36},
 {"buffer":0,"byteOffset":%zu,"byteLength":36},
 {"buffer":0,"byteOffset":%zu,"byteLength":8},
 {"buffer":0,"byteOffset":%zu,"byteLength":8}],
"accessors":[
 {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[2,2,0]},
 {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0,3,0]},
 {"bufferView":2,"componentType":5126,"count":2,"type":"SCALAR","min":[0.0],"max":[1.0]},
 {"bufferView":3,"componentType":5126,"count":2,"type":"SCALAR"}]})",
            b64.c_str(),buf.size(),posOff,dOff,timeOff,wOff);
        auto path=WriteFixture(json,"meowy-gltf-morphanim.gltf");
        Model m=LoadModel(path);
        int animCount=0; ModelAnimation* anims=LoadModelAnimations(path,&animCount);
        Expect(anims && animCount==1,"morph-weight animation loads (1 clip)");
        if(anims && animCount==1){
            Expect(anims[0].morphTargetCount==1 && anims[0].frameWeights!=nullptr,"animation carries a morph-weight track");
            // Frame 0 -> weight ~0; last frame -> weight ~1.
            UpdateModelAnimation(m,anims[0],0);
            float w0=m.meshes[0].morphWeights?m.meshes[0].morphWeights[0]:-1;
            UpdateModelAnimation(m,anims[0],anims[0].frameCount-1);
            float w1=m.meshes[0].morphWeights?m.meshes[0].morphWeights[0]:-1;
            std::printf("  (morph anim weight frame0=%.2f frameEnd=%.2f)\n",w0,w1);
            Expect(w0<0.1f && w1>0.9f,"UpdateModelAnimation drives the mesh morph weight over time");
        }
        if(anims) UnloadModelAnimations(anims,animCount);
        UnloadModel(m);
    }

    CloseWindow();
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
