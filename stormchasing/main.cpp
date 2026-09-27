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

            ImGui::Begin("StormChasing Debug");
            ImGui::Text("Version: Latest (GIT)");
            if (ImGui::Button("Action"))
                ImGui::OpenPopup("Sample");

            if (ImGui::BeginPopup("Sample"))
            {
                ImGui::Text("Hello, World");
                if (ImGui::Button("Close"))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            ImGui::End();

            //TODO: Make the actual game.

        mew::StopDraw();
    }

    meowyrender::ShutdownImGui();
    mew::CloseWindow();
}
