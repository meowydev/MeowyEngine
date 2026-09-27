//
// Created by meowy on 9/16/26.
//

#include "draw.hpp"

namespace mew {
    void DrawCircle(int x, int y, float radius, Color colr) {
        mr::DrawCircle(x, y, radius, colr);
    }

    void DrawRect(float x, float y, float width, float height, Color color) {
        mr::DrawRectangle(static_cast<int>(x), static_cast<int>(y),
                          static_cast<int>(width), static_cast<int>(height), color);
    }

    void DrawModel(Object obj) {
        mr::DrawModelEx(obj.mdl, obj.pos, obj.rotAxis, obj.angle, obj.scale, mr::WHITE);
    }
} // mew
