#include <meowyrender/meowyrender.hpp>
namespace mr=meowyrender;

int main() {
    mr::Model cube;
    mr::Camera3D camera{{4,3,5},{0,1,0},{0,1,0},45,mr::CameraProjection::Perspective};

    mr::RunApplication(960,540,"Hello MeowyRender",[&] {
        mr::ClearBackground(mr::RAYWHITE);
        mr::BeginMode3D(camera);
        mr::DrawModel(cube,{0,1,0},1,mr::SKYBLUE);
        mr::DrawGrid(10,1);
        mr::EndMode3D();
        mr::DrawText("One API. OpenGL, Metal, or Vulkan.",20,20,20,mr::DARKGRAY);
    },[&] {
        mr::SetTargetFPS(60);
        cube=mr::LoadModelFromMesh(mr::GenMeshCube(2,2,2));
        cube.materials[0].lighting=true;
    },[&] {
        mr::UnloadModel(cube);
    });
}
