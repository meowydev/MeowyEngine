//
// Created by meowy on 9/16/26.
//

#include "window.hpp"
#include <string>

namespace mew {
    void CreateWindow(std::string title, int width, int height) {
        mr::InitWindow(width, height, title);
    }

    bool IsRunning() {
        return !mr::WindowShouldClose();
    }

    void CloseWindow() {
        mr::CloseWindow();
    }

    void ClearScreen(Color color) {
        mr::ClearBackground(color);
    }

    void StartDraw() {
        mr::BeginDrawing();
    }

    void StopDraw() {
        mr::EndDrawing();
    }
} // mew
