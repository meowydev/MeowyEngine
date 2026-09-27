// meowyrender - src/core/input.cpp
// Keyboard / mouse / gamepad query API over the shared InputState.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <cmath>

namespace meowyrender {

using detail::State;

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
bool IsKeyDown(KeyboardKey key) {
    const auto k = static_cast<std::size_t>(key);
    return k < 512 && State().input.keysCurrent[k];
}
bool IsKeyUp(KeyboardKey key) { return !IsKeyDown(key); }

bool IsKeyPressed(KeyboardKey key) {
    const auto k = static_cast<std::size_t>(key);
    const auto& in = State().input;
    return k < 512 && in.keysCurrent[k] && !in.keysPrevious[k];
}
bool IsKeyReleased(KeyboardKey key) {
    const auto k = static_cast<std::size_t>(key);
    const auto& in = State().input;
    return k < 512 && !in.keysCurrent[k] && in.keysPrevious[k];
}
int GetKeyPressed() { return State().input.lastKeyPressed; }
int GetCharPressed() { return State().input.lastCharPressed; }

bool IsKeyPressedRepeat(KeyboardKey key) {
    const auto k = static_cast<std::size_t>(key);
    return k < 512 && State().input.keysRepeat[k];
}
void SetExitKey(KeyboardKey key) { State().input.exitKey = static_cast<int>(key); }

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------
bool IsMouseButtonDown(MouseButton button) {
    const auto b = static_cast<std::size_t>(button);
    return b < 8 && State().input.mouseCurrent[b];
}
bool IsMouseButtonUp(MouseButton button) { return !IsMouseButtonDown(button); }

bool IsMouseButtonPressed(MouseButton button) {
    const auto b = static_cast<std::size_t>(button);
    const auto& in = State().input;
    return b < 8 && in.mouseCurrent[b] && !in.mousePrevious[b];
}
bool IsMouseButtonReleased(MouseButton button) {
    const auto b = static_cast<std::size_t>(button);
    const auto& in = State().input;
    return b < 8 && !in.mouseCurrent[b] && in.mousePrevious[b];
}

int GetMouseX() { return static_cast<int>(State().input.mousePosition.x); }
int GetMouseY() { return static_cast<int>(State().input.mousePosition.y); }
Vector2 GetMousePosition() {
    const auto& in = State().input;
    return {in.mousePosition.x * in.mouseScale.x + in.mouseOffset.x,
            in.mousePosition.y * in.mouseScale.y + in.mouseOffset.y};
}
Vector2 GetMouseDelta() { return State().input.mouseDelta; }
float GetMouseWheelMove() {
    // raylib returns the larger-magnitude axis of the wheel movement.
    const auto& v = State().input.mouseWheelV;
    if (std::fabs(v.x) > std::fabs(v.y)) return v.x;
    return v.y != 0.0f ? v.y : State().input.mouseWheel;
}
Vector2 GetMouseWheelMoveV() {
    const auto& in = State().input;
    Vector2 v = in.mouseWheelV;
    if (v.x == 0.0f && v.y == 0.0f) v.y = in.mouseWheel;
    return v;
}
void SetMouseOffset(int offsetX, int offsetY) {
    State().input.mouseOffset = {static_cast<float>(offsetX), static_cast<float>(offsetY)};
}
void SetMouseScale(float scaleX, float scaleY) {
    State().input.mouseScale = {scaleX, scaleY};
}

// ---------------------------------------------------------------------------
// Touch (on desktop, touch point 0 mirrors the mouse while a button is held)
// ---------------------------------------------------------------------------
int GetTouchX() { return static_cast<int>(GetTouchPosition(0).x); }
int GetTouchY() { return static_cast<int>(GetTouchPosition(0).y); }
Vector2 GetTouchPosition(int index) {
    const auto& in = State().input;
    if (index < 0 || index >= detail::InputState::kMaxTouch) return {0, 0};
    if (index < in.touchCount) return in.touchPositions[index];
    // Fall back to the mouse for index 0 so single-touch games work on desktop.
    if (index == 0) return GetMousePosition();
    return {0, 0};
}
int GetTouchPointId(int index) {
    const auto& in = State().input;
    if (index < 0 || index >= in.touchCount) return 0;
    return in.touchIds[index];
}
int GetTouchPointCount() { return State().input.touchCount; }


bool IsGamepadAvailable(int gamepad) {return gamepad>=0&&gamepad<16&&State().input.gamepads[gamepad].available;}
bool IsGamepadButtonDown(int gamepad,GamepadButton button) {
    int index=static_cast<int>(button);
    return IsGamepadAvailable(gamepad)&&index>0&&index<18&&State().input.gamepads[gamepad].buttons[index];
}
bool IsGamepadButtonPressed(int gamepad,GamepadButton button) {
    int index=static_cast<int>(button);
    return IsGamepadButtonDown(gamepad,button)&&!State().input.previousGamepads[gamepad].buttons[index];
}
bool IsGamepadButtonUp(int gamepad,GamepadButton button) {
    return !IsGamepadButtonDown(gamepad,button);
}
bool IsGamepadButtonReleased(int gamepad,GamepadButton button) {
    int index=static_cast<int>(button);
    return IsGamepadAvailable(gamepad)&&index>0&&index<18&&
           !State().input.gamepads[gamepad].buttons[index]&&
           State().input.previousGamepads[gamepad].buttons[index];
}
int GetGamepadButtonPressed() {
    // Return the first newly-pressed button across all connected gamepads.
    const auto& in = State().input;
    for (int g = 0; g < 16; ++g) {
        if (!in.gamepads[g].available) continue;
        for (int b = 1; b < 18; ++b)
            if (in.gamepads[g].buttons[b] && !in.previousGamepads[g].buttons[b]) return b;
    }
    return 0; // GAMEPAD_BUTTON_UNKNOWN
}
int GetGamepadAxisCount(int gamepad) {
    return IsGamepadAvailable(gamepad) ? State().input.gamepads[gamepad].axisCount : 0;
}
float GetGamepadAxisMovement(int gamepad,GamepadAxis axis) {
    int index=static_cast<int>(axis);
    return IsGamepadAvailable(gamepad)&&index>=0&&index<6?State().input.gamepads[gamepad].axes[index]:0.0f;
}
const char* GetGamepadName(int gamepad) {
    return IsGamepadAvailable(gamepad) ? State().input.gamepads[gamepad].name.c_str() : "";
}
}
