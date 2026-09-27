// meowyrender - samples/ibl_precompute_checks.cpp
// Pixel/readback checks for the precomputed split-sum IBL pipeline:
// GenEnvironmentLightMaps builds a cosine-convolved diffuse irradiance cubemap,
// a GGX roughness-prefiltered specular cubemap, and a BRDF integration LUT;
// SetEnvironmentLightPrecomputed feeds them to the lit shader (shadePBRSplitSum).
#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cmath>
using namespace meowyrender;
static int failures=0;
void Expect(bool ok,const char* message){std::printf("%s: %s\n",ok?"PASS":"FAIL",message);if(!ok)++failures;}

int main(){
    InitWindow(320,240,"Precomputed IBL checks");

    // A uniform blue environment: irradiance and prefiltered specular should
    // both come out blue, and the BRDF LUT should hold a valid integration.
    Image blueFaces=GenImageColor(8*6,8,BLUE); // 6 blue faces (horizontal line)
    TextureCubemap env=LoadTextureCubemap(blueFaces,CubemapLayout::LineHorizontal);
    UnloadImage(blueFaces);
    Expect(env.id!=0,"environment cubemap uploads");

    EnvironmentLight light=GenEnvironmentLightMaps(env,16,32,64);
    Expect(light.irradiance.id!=0,"irradiance cubemap generated");
    Expect(light.prefilter.id!=0,"prefiltered specular cubemap generated");
    Expect(light.prefilter.mipmaps>1,"prefiltered specular has a mip chain");
    Expect(light.brdfLut.id!=0,"BRDF integration LUT generated");

    // Readback: the convolved irradiance of a blue env stays blue.
    {
        Image irr=LoadImageFromCubemapFace(light.irradiance,0);
        Color c=GetImageColor(irr,irr.width/2,irr.height/2);
        std::printf("  (irradiance face0 rgba=%d,%d,%d,%d)\n",c.r,c.g,c.b,c.a);
        Expect(irr.data && c.b>c.r && c.b>c.g && c.b>10,"irradiance cubemap is blue-dominant");
        UnloadImage(irr);
    }
    // Readback: prefiltered specular of a blue env stays blue.
    {
        Image pf=LoadImageFromCubemapFace(light.prefilter,0);
        Color c=GetImageColor(pf,pf.width/2,pf.height/2);
        std::printf("  (prefilter face0 rgba=%d,%d,%d,%d)\n",c.r,c.g,c.b,c.a);
        Expect(pf.data && c.b>c.r && c.b>c.g && c.b>10,"prefiltered specular is blue-dominant");
        UnloadImage(pf);
    }
    // Readback: the BRDF LUT holds a non-trivial (non-zero) scale term.
    {
        Image lut=LoadImageFromTexture(light.brdfLut);
        // High NdotV, low roughness -> scale term near 1, bias small.
        Color hi=GetImageColor(lut,lut.width-1,0);
        std::printf("  (brdf hi-NdotV/low-rough rg=%d,%d)\n",hi.r,hi.g);
        Expect(lut.data && hi.r>64,"BRDF LUT scale term is populated");
        UnloadImage(lut);
    }

    // Render a metallic quad lit only by the precomputed environment: the
    // surface must pick up the blue reflection via the split-sum path.
    {
        SetAmbientLight(BLACK,0);
        SetDirectionalLight({0,-1,0},BLACK,0); // no direct light; IBL only
        SetEnvironmentLightPrecomputed(light,1.0f);
        Material metal=LoadMaterialDefault(); metal.lighting=true;
        metal.maps[static_cast<int>(MaterialMapIndex::Albedo)].color=WHITE;
        metal.maps[static_cast<int>(MaterialMapIndex::Metalness)].value=1.0f;
        metal.maps[static_cast<int>(MaterialMapIndex::Roughness)].value=0.3f;
        float vertices[]={40,90,0,120,90,0,120,150,0,40,150,0};
        float normals[]={0,0,1,0,0,1,0,0,1,0,0,1};
        float uvs[]={0,0,1,0,1,1,0,1};
        unsigned short idx[]={0,1,2,0,2,3};
        Mesh quad{}; quad.vertices=vertices; quad.normals=normals; quad.texcoords=uvs; quad.indices=idx; quad.vertexCount=4; quad.triangleCount=2;
        BeginDrawing(); ClearBackground(BLACK);
        DrawMesh(quad,metal,MatrixIdentity());
        TakeScreenshot("ibl-precompute-check.png"); EndDrawing();
        Image shot=LoadImage("ibl-precompute-check.png");
        Color surf=GetImageColor(shot,80*shot.width/320,120*shot.height/240);
        std::printf("  (precomputed IBL surface rgba=%d,%d,%d,%d)\n",surf.r,surf.g,surf.b,surf.a);
        Expect(shot.data && surf.b>surf.r && surf.b>surf.g && surf.b>20,"precomputed IBL tints the metallic surface blue");
        UnloadImage(shot); UnloadMaterial(metal);
        ClearEnvironmentLight();
    }

    UnloadEnvironmentLight(light);
    UnloadTexture(env);
    CloseWindow();
    std::printf("%s\n",failures==0?"SMOKE TEST PASS":"SMOKE TEST FAIL");
    return failures==0?0:1;
}