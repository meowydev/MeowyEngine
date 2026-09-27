// meowyrender - samples/transparency_checks.cpp
// Verifies transparent (BLEND) rendering: independent depth-write (a near
// transparent quad does not depth-occlude a farther transparent quad) and
// back-to-front sorting (submission order does not change the composite).
#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cmath>
#include <stdexcept>
namespace mr = meowyrender;
static int failures = 0;
static void Expect(bool ok, const char* msg){ std::printf("%s: %s\n", ok?"PASS":"FAIL", msg); if(!ok) ++failures; }

// A camera-facing quad centered at z, size 4, with a BLEND material of `color`.
static mr::Mesh Quad() {
    static float v[]={-2,-2,0, 2,-2,0, 2,2,0, -2,2,0};
    static float n[]={0,0,1, 0,0,1, 0,0,1, 0,0,1};
    static float uv[]={0,0,1,0,1,1,0,1};
    static unsigned short idx[]={0,1,2,0,2,3};
    mr::Mesh m{}; m.vertices=v; m.normals=n; m.texcoords=uv; m.indices=idx; m.vertexCount=4; m.triangleCount=2;
    return m;
}

int main(){
    mr::InitWindow(128,128,"transparency checks");
    mr::Camera3D cam{{0,0,10},{0,0,0},{0,1,0},45,mr::CameraProjection::Perspective};

    mr::Material red = mr::LoadMaterialDefault();  red.lighting=false;  red.alphaMode=mr::MaterialAlphaMode::Blend;
    red.maps[0].color={255,0,0,128};
    mr::Material blue = mr::LoadMaterialDefault(); blue.lighting=false; blue.alphaMode=mr::MaterialAlphaMode::Blend;
    blue.maps[0].color={0,0,255,128};
    mr::Mesh quad = Quad();

    // Near red quad (z=+2), far blue quad (z=-2). Both cover screen center.
    mr::Matrix nearXf = mr::MatrixTranslate(0,0,2);
    mr::Matrix farXf  = mr::MatrixTranslate(0,0,-2);

    auto centerAfter = [&](bool nearFirst)->mr::Color {
        mr::BeginDrawing(); mr::ClearBackground(mr::BLACK);
        mr::BeginMode3D(cam);
        // Submit in the requested order; the transparent queue sorts back-to-front.
        if(nearFirst){ mr::DrawMesh(quad,red,nearXf); mr::DrawMesh(quad,blue,farXf); }
        else         { mr::DrawMesh(quad,blue,farXf); mr::DrawMesh(quad,red,nearXf); }
        mr::EndMode3D();
        mr::TakeScreenshot("transparency-check.png"); mr::EndDrawing();
        mr::Image img=mr::LoadImage("transparency-check.png");
        mr::Color c=mr::GetImageColor(img,img.width/2,img.height/2);
        mr::UnloadImage(img);
        return c;
    };

    mr::Color a = centerAfter(true);   // near submitted first
    mr::Color b = centerAfter(false);  // far submitted first
    std::printf("  (center nearFirst=%d,%d,%d farFirst=%d,%d,%d)\n",a.r,a.g,a.b,b.r,b.g,b.b);

    // Both quads must contribute at the overlap: red channel present (near red
    // over far blue) AND blue channel present (far blue shows through). If the
    // near quad had written depth and occluded the far one, blue would vanish
    // when the near quad was drawn first.
    Expect(a.r>40 && a.b>40, "both transparent layers composite (near-first submission)");
    Expect(b.r>40 && b.b>40, "both transparent layers composite (far-first submission)");
    // Sorting makes the composite order-independent: results should match.
    Expect(std::abs(a.r-b.r)<=12 && std::abs(a.g-b.g)<=12 && std::abs(a.b-b.b)<=12,
           "back-to-front sort makes the composite submission-order independent");

    mr::UnloadMaterial(red); mr::UnloadMaterial(blue);
    mr::CloseWindow();
    return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
