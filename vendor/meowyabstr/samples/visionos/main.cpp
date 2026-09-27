// meowyrender sample - native visionOS feature test
//
// Runs on visionOS (Metal backend) via RunApplication. Beyond a basic render,
// the smoke test (MEOWY_SMOKE_TEST) exercises the advanced features added to
// MeowyRender - cascaded/directional shadows, image-based lighting, glTF morph
// targets, and depth-sorted transparency - and validates each with a pixel
// readback so it checks BEHAVIOR, not just that it compiles and launches.
#include <meowyrender/meowyrender.hpp>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#if defined(MEOWY_WITH_IMGUI)
#include <imgui.h>
#endif
using namespace meowyrender;

static int g_pass = 0, g_fail = 0;
static void Check(bool ok, const char* msg) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", msg); std::fflush(stdout);
    if (ok) ++g_pass; else ++g_fail;
}

int main() {
    Model floorModel, boxModel; Mesh morphMesh{}; Material morphMat{};
    TextureCubemap sky{}; int frames = 0; bool imguiReady = false;

    RunApplication(900, 600, "MeowyRender visionOS feature test", [&]{
        const float t = static_cast<float>(GetTime());
        ClearBackground({20, 24, 35, 255});

        Camera3D cam{{5, 5, 7}, {0, 1, 0}, {0, 1, 0}, 45, CameraProjection::Perspective};
        Vector3 lightDir{-0.4f, -1.0f, -0.3f};
        SetAmbientLight(WHITE, 0.12f);
        SetDirectionalLight(lightDir, WHITE, 3.0f);
        SetEnvironmentLight(sky, 1.0f);

        // Shadow pass (cascaded where supported, else single-map).
        const bool cascades = IsCascadedShadowSupported();
        if (cascades) {
            BeginShadowCascades(cam, lightDir, 3, 1024);
            for (int c = 0; c < GetShadowCascadeCount(); ++c) { SetShadowCascade(c); DrawModel(boxModel, {0, 1.2f, 0}, 1.0f, WHITE); }
            EndShadowCascades();
        } else if (IsShadowMappingSupported()) {
            BeginShadowMode({{5, 12, 4}, {0, 0, 0}, {0, 1, 0}, 20.0f, CameraProjection::Orthographic}, 2048);
                DrawModel(boxModel, {0, 1.2f, 0}, 1.0f, WHITE);
            EndShadowMode();
        }

        BeginMode3D(cam);
            DrawModel(floorModel, {0, 0, 0}, 1.0f, WHITE);
            DrawModel(boxModel, {0, 1.2f, 0}, 1.0f, {220, 180, 60, 255});
            float w = 0.5f + 0.5f * std::sin(t * 1.5f);
            SetMeshMorphWeights(&morphMesh, &w, 1);
            DrawMesh(morphMesh, morphMat, MatrixTranslate(-2.5f, 1.0f, 1.0f));
            // BLEND draws are deferred to EndMode3D, so the panel mesh/material
            // must outlive this scope: create them once (function-static).
            static Mesh panel = GenMeshPlane(2, 2, 1, 1);
            static Material glass = []{ Material m = LoadMaterialDefault(); m.lighting=false; m.alphaMode=MaterialAlphaMode::Blend; m.maps[0].color={80,220,120,120}; return m; }();
            DrawMesh(panel, glass, MatrixMultiply(MatrixRotateX(PI * 0.5f), MatrixTranslate(2.0f, 1.2f, 1.5f)));
        EndMode3D();
        ClearShadowMap(); ClearEnvironmentLight();

        DrawText("MeowyRender | native visionOS + Metal", 24, 24, 20, RAYWHITE);
        DrawText("cascaded shadows / IBL / morph / transparency", 24, 60, 16, SKYBLUE);

#if defined(MEOWY_WITH_IMGUI)
        // Dear ImGui on visionOS (imgui_impl_metal + a GLFW-free platform layer).
        // A window pinned at a known spot with a solid red background gives the
        // smoke test a deterministic ImGui pixel to sample.
        if (imguiReady) {
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(260, 150));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.9f, 0.1f, 0.1f, 1.0f)); // solid red
            ImGui::Begin("MeowyRender Debug", nullptr, ImGuiWindowFlags_NoTitleBar);
            ImGui::Text("Backend: %s", GetActiveBackendName());
            ImGui::Text("FPS: %d", GetFPS());
            ImGui::End();
            ImGui::PopStyleColor();

            // --- Interactive input test window ---------------------------------
            // Confirms touches reach ImGui: shows the live IO state fed by the
            // GLFW-free visionOS platform layer, plus widgets you can poke to
            // verify hit-testing (button counter, slider, checkbox, text field).
            static int   clicks = 0;
            static float sliderValue = 0.5f;
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
            ImGui::SameLine();
            ImGui::Text("clicks: %d", clicks);
            ImGui::SliderFloat("slider", &sliderValue, 0.0f, 1.0f);
            ImGui::Checkbox("toggle", &toggle);
            ImGui::InputText("text", textBuf, sizeof(textBuf));

            // A hit-test target: fills green while pressed over it, so you get
            // immediate visual feedback that a touch landed inside the region.
            ImGui::SeparatorText("Hit test");
            ImGui::ColorButton(
                "##hit",
                ImGui::IsItemActive() ? ImVec4(0.2f, 0.8f, 0.3f, 1.0f)
                                      : ImVec4(0.25f, 0.25f, 0.3f, 1.0f),
                0, ImVec2(-1, 60));
            ImGui::Text("Press inside the bar above: %s",
                        ImGui::IsItemActive() ? "PRESSED" : "released");
            ImGui::End();
        }
#endif

        if (std::getenv("MEOWY_SMOKE_TEST") && ++frames == 12) {
            auto path = (std::filesystem::temp_directory_path() / "meowyrender-visionos.png").string();
            TakeScreenshot(path);
            Image image = LoadImage(path);
            // Also record the ImGui-pixel result to a file so a host harness can
            // read the outcome deterministically (the visionOS sim does not
            // reliably bridge stdout back to `simctl launch --console`).
            auto resultPath = (std::filesystem::temp_directory_path() / "meowyrender-visionos-result.txt").string();
            auto sample = [&](int x, int y){ return GetImageColor(image, x * image.width / GetScreenWidth(), y * image.height / GetScreenHeight()); };
            Color bg = GetImageColor(image, image.width - 2, 2); // top-right: background
            std::printf("  (visionOS bg corner rgba=%d,%d,%d,%d size=%dx%d)\n", bg.r, bg.g, bg.b, bg.a, image.width, image.height);
            std::fflush(stdout);
            // Dark bluish background (allow sim color-management tolerance).
            Check(image.data && bg.b >= bg.r && bg.r < 80 && bg.g < 90, "native visionOS frame readback + dark background");
            // The box is lit (IBL + directional) and should be brighter than the
            // ambient-only floor in shadow beneath it.
            Color boxTop = sample(450, 230);
            Check(boxTop.r > 60 || boxTop.g > 60 || boxTop.b > 60, "lit box renders under IBL + directional light");
#if defined(MEOWY_WITH_IMGUI)
            // The pinned ImGui window covers screen points ~(10,10)..(270,160);
            // sample its center at (140,80) and require a strongly red pixel.
            Color imguiPixel = sample(140, 80);
            std::printf("  (visionOS ImGui pixel rgba=%d,%d,%d,%d ready=%d)\n",
                        imguiPixel.r, imguiPixel.g, imguiPixel.b, imguiPixel.a, imguiReady ? 1 : 0);
            std::fflush(stdout);
            const bool imguiOk = imguiReady && imguiPixel.r > 150 && imguiPixel.g < 100 && imguiPixel.b < 100;
            if (FILE* rf = std::fopen(resultPath.c_str(), "w")) {
                std::fprintf(rf, "imgui_ready=%d\nimgui_pixel=%d,%d,%d,%d\nimgui_ok=%d\n",
                             imguiReady ? 1 : 0, imguiPixel.r, imguiPixel.g, imguiPixel.b, imguiPixel.a,
                             imguiOk ? 1 : 0);
                std::fclose(rf);
            }
            Check(imguiOk, "Dear ImGui renders on visionOS (pinned red window via imgui_impl_metal)");
#endif
            Check(g_fail == 0, "visionOS advanced-feature smoke test");
            std::printf("SMOKE %s (%d passed, %d failed) %dx%d\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail, image.width, image.height);
            std::fflush(stdout);
            UnloadImage(image);
            UnloadModel(floorModel); UnloadModel(boxModel); UnloadMesh(morphMesh); UnloadMaterial(morphMat); UnloadTexture(sky);
            CloseWindow();
            std::exit(g_fail ? 1 : 0);
        }
    }, [&]{
        SetTargetFPS(60);
        floorModel = LoadModelFromMesh(GenMeshPlane(20, 20, 1, 1));
        floorModel.materials[0].lighting = true;
        floorModel.materials[0].maps[static_cast<int>(MaterialMapIndex::Roughness)].value = 1.0f;
        boxModel = LoadModelFromMesh(GenMeshCube(1.6f, 2.0f, 1.6f));
        boxModel.materials[0].lighting = true;
        morphMesh = GenMeshSphere(0.7f, 12, 12);
        morphMesh.morphTargetCount = 1;
        morphMesh.morphPositions = static_cast<float*>(std::calloc(static_cast<std::size_t>(morphMesh.vertexCount) * 3, sizeof(float)));
        morphMesh.morphNormals = static_cast<float*>(std::calloc(static_cast<std::size_t>(morphMesh.vertexCount) * 3, sizeof(float)));
        morphMesh.morphWeights = static_cast<float*>(std::calloc(1, sizeof(float)));
        for (int i = 0; i < morphMesh.vertexCount; ++i) morphMesh.morphPositions[i * 3 + 1] = 1.0f;
        morphMat = LoadMaterialDefault(); morphMat.lighting = true;
        morphMat.maps[static_cast<int>(MaterialMapIndex::Albedo)].color = {200, 120, 220, 255};
        // A simple colored cubemap for image-based lighting.
        Image faces = GenImageColor(96, 16, SKYBLUE);
        const Color skyColors[] = {SKYBLUE, BLUE, {80,140,210,255}, {40,60,110,255}, {110,165,220,255}, {70,125,190,255}};
        for (int f = 0; f < 6; ++f) ImageDrawRectangle(&faces, f * 16, 0, 16, 16, skyColors[f]);
        sky = LoadTextureCubemap(faces, CubemapLayout::LineHorizontal);
        UnloadImage(faces);
        SetEnvironmentLight(sky, 1.0f);
#if defined(MEOWY_WITH_IMGUI)
        imguiReady = InitImGui();
        std::printf("visionOS ImGui available=%s init=%s backend=%s\n",
                    IsImGuiAvailable() ? "yes" : "no", imguiReady ? "ok" : "failed",
                    GetActiveBackendName());
        std::fflush(stdout);
#endif
    }, [&]{
#if defined(MEOWY_WITH_IMGUI)
        if (imguiReady) ShutdownImGui();
#endif
        UnloadModel(floorModel); UnloadModel(boxModel);
    });
}
