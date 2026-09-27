//
// Created by meowy on 9/15/26.
//
#include <meowyengine.hpp>
#include <spdlog/spdlog.h>
#include <meowyrender/meowyrender.hpp>
#include <imgui.h>

int main() {
    mew::CreateWindow("StormChasing", 800, 600);

    mew::init(true);

    while (mew::IsRunning()) {
        mew::StartDraw();

            mew::ClearScreen(meowyrender::BLACK);

            // ImGui: raw widgets between StartDraw / StopDraw. Begin needs a
            // window name, and every Begin needs a matching End.
            ImGui::Begin("StormChasing Debug");

            ImGui::End();

            //TODO: Make the actual game.

        mew::StopDraw();
    }

    meowyrender::ShutdownImGui();
    mew::CloseWindow();
}
