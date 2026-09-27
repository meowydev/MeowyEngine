// meowyrender sample - imgui_demo
//
// Shows Dear ImGui integration: InitImGui() once, then raw ImGui:: widgets
// between BeginDrawing()/EndDrawing(). BeginDrawing starts the ImGui frame and
// EndDrawing renders it automatically. Build the library with MEOWY_WITH_IMGUI=ON.
//
//   MEOWY_IMGUI_FRAMES=8 ./bin/imgui_demo   # headless: run 8 frames + screenshot
#include <meowyrender/meowyrender.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace mr = meowyrender;

#if defined(MEOWY_WITH_IMGUI)
#include <imgui.h>
#endif

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--backend" && i + 1 < argc) {
            mr::Backend b; if (mr::ParseBackend(argv[++i], b)) mr::SetPreferredBackend(b);
        }
    }

    mr::InitWindow(900, 600, "MeowyRender + Dear ImGui");
    mr::SetTargetFPS(60);

    const bool imgui = mr::InitImGui();
    std::printf("ImGui available: %s | InitImGui: %s | backend: %s\n",
                mr::IsImGuiAvailable() ? "yes" : "no",
                imgui ? "ok" : "failed/unsupported",
                mr::GetActiveBackendName());

    int frames = 0, frameLimit = 0;
    if (const char* f = std::getenv("MEOWY_IMGUI_FRAMES")) frameLimit = std::atoi(f);

    float slider = 0.5f;
    while (!mr::WindowShouldClose()) {
        mr::BeginDrawing();
        mr::ClearBackground(mr::Color{30, 30, 40, 255});

        // Some MeowyRender drawing underneath the UI, to prove they compose.
        mr::DrawCircle(200, 300, 60, mr::SKYBLUE);
        mr::DrawText("MeowyRender scene behind ImGui", 20, 560, 18, mr::RAYWHITE);

#if defined(MEOWY_WITH_IMGUI)
        if (imgui) {
            // A window pinned at a known spot with a bright solid background, so
            // headless verification can sample a deterministic ImGui pixel.
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(260, 150));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.9f, 0.1f, 0.1f, 1.0f)); // solid red
            ImGui::Begin("MeowyRender Debug", nullptr, ImGuiWindowFlags_NoTitleBar);
            ImGui::Text("Backend: %s", mr::GetActiveBackendName());
            ImGui::Text("FPS: %d", mr::GetFPS());
            ImGui::SliderFloat("value", &slider, 0.0f, 1.0f);
            if (ImGui::Button("Close")) mr::RequestWindowClose();
            ImGui::End();
            ImGui::PopStyleColor();

            // Interactive input test window: mirrors the visionOS sample so you
            // can validate the same IO fields the platform layer feeds. Poke the
            // widgets and watch the live mouse/touch state update.
            static int   clicks = 0;
            static bool  toggle = false;
            static char  textBuf[64] = "";
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(300, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_FirstUseEver);
            ImGui::Begin("Input Test");
            ImGui::SeparatorText("Live IO");
            ImGui::Text("Mouse: (%.0f, %.0f)", io.MousePos.x, io.MousePos.y);
            ImGui::Text("Delta: (%.1f, %.1f)", io.MouseDelta.x, io.MouseDelta.y);
            ImGui::Text("Down: %s   Dragging: %s",
                        io.MouseDown[0] ? "yes" : "no",
                        ImGui::IsMouseDragging(ImGuiMouseButton_Left) ? "yes" : "no");
            ImGui::Text("Display: %.0fx%.0f  scale %.1fx%.1f",
                        io.DisplaySize.x, io.DisplaySize.y,
                        io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "yes" : "no");
            ImGui::SeparatorText("Widgets");
            if (ImGui::Button("Tap me")) ++clicks;
            ImGui::SameLine(); ImGui::Text("clicks: %d", clicks);
            ImGui::SliderFloat("slider", &slider, 0.0f, 1.0f);
            ImGui::Checkbox("toggle", &toggle);
            ImGui::InputText("text", textBuf, sizeof(textBuf));
            ImGui::SeparatorText("Hit test");
            ImGui::ColorButton("##hit",
                ImGui::IsItemActive() ? ImVec4(0.2f, 0.8f, 0.3f, 1.0f)
                                      : ImVec4(0.25f, 0.25f, 0.3f, 1.0f),
                0, ImVec2(-1, 60));
            ImGui::Text("Press inside the bar above: %s",
                        ImGui::IsItemActive() ? "PRESSED" : "released");
            ImGui::End();

            ImGui::ShowDemoWindow();
        }
#endif

        // Headless capture: request the screenshot BEFORE EndDrawing so it is
        // taken mid-frame (after ImGui renders, before present). This works on
        // every backend, including Metal/Vulkan where the drawable/swapchain
        // image is only readable while the frame is in flight.
        if (frameLimit > 0 && frames + 1 >= frameLimit)
            mr::TakeScreenshot("imgui-demo.png");

        mr::EndDrawing();
        if (frameLimit > 0 && ++frames >= frameLimit) break;
    }

    mr::ShutdownImGui();
    mr::CloseWindow();
    return 0;
}
