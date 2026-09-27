//
// Created by meowy on 9/16/26.
//

#ifndef MEOWYENGINE_DRAW_HPP
#define MEOWYENGINE_DRAW_HPP

#pragma once
#include "render.hpp"

namespace mew {
    void DrawCircle(int x, int y, float radius, Color color);
    void DrawRect(float x, float y, float width, float height, Color color);
    struct Object {
        mr::Model mdl;
        Vector3 pos;
        Vector3 rotAxis;
        float angle;
        Vector3 scale;

        Object(Model modl, Vector3 posi, Vector3 rotAxise, float anglex, Vector3 scl) {
            mdl = modl;
            pos = posi;
            rotAxis = rotAxise;
            angle = anglex;
            scale = scl;
        }
    };
    void DrawModel(Object obj);
} // mew

#endif //MEOWYENGINE_DRAW_HPP
