//
// Created by meowy on 9/16/26.
//

#ifndef MEOWYENGINE_WINDOW_HPP
#define MEOWYENGINE_WINDOW_HPP

#pragma once
#include "render.hpp"
#include <string>

namespace mew {
    void CreateWindow(std::string title, int width, int height);
    bool IsRunning();
    void CloseWindow();

    void ClearScreen(Color color);
    void StartDraw();
    void StopDraw();
} // mew

#endif //MEOWYENGINE_WINDOW_HPP
