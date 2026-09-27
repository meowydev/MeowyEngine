//
// Created by meowy on 9/15/26.
//
#include <meowyengine.hpp>
#include <spdlog/spdlog.h>
#include <meowyrender/meowyrender.hpp>
#include <imgui.h>

bool UIMode = false;

using namespace meowyrender;

int main() {
    mew::CreateWindow("StormChasing", 800, 600);

    mew::init(true);

    DisableCursor();

    Camera3D cam;
    cam.fovy = 60;
    cam.position = {0.0f,0.0f,0.0f};
    cam.projection = CameraProjection::Perspective;
    cam.target = {0.0f, 0.0f, 1.0f};
    cam.up = {0.0f, 1.0f, 0.0f};

    // Variables


    while (mew::IsRunning()) {

        if (IsKeyPressed(KeyboardKey::F3))
        {
            if (UIMode)
            {
                DisableCursor();
                UIMode = !UIMode;
            }
            else
            {
                EnableCursor();
                UIMode = !UIMode;
            }
        }

        if (!UIMode)
            UpdateCamera(&cam,CameraMode::FirstPerson);

        mew::StartDraw();

            mew::ClearScreen(WHITE);

            BeginMode3D(cam);
                DrawGrid(5,5);
            EndMode3D();

            ImGui::Begin("StormChasing Debug");
            ImGui::Text("Version: Latest (GIT)");

            ImGui::End();

            //TODO: Make the actual game.

        mew::StopDraw();
    }

    ShutdownImGui();
    mew::CloseWindow();
}
