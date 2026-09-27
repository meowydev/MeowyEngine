//
// Created by meowy on 9/18/26.
//

#ifndef MEOWYENGINE_MEOWYENGINE_HPP
#define MEOWYENGINE_MEOWYENGINE_HPP

#pragma once
#include "render.hpp"

#include <meowyrender/meowyrender.hpp>
#include <spdlog/spdlog.h>
#include "window.hpp"
#include "graphics/draw.hpp"

// Entry point or smth

namespace mew
{
    void init(bool imgui)
    {
        spdlog::trace("MeowyEngine v3");
        spdlog::trace("Started initializing...");

        spdlog::set_pattern("[%H:%M:%S %z] [%n] [%^---%L---%$] [thread %t] %v");

        // Only bring up Dear ImGui when the caller asked for it.
        if (imgui)
            meowyrender::InitImGui();
    }
}

#endif //MEOWYENGINE_MEOWYENGINE_HPP
