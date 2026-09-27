// meowyrender - mr_input.hpp
// Keyboard / mouse / gamepad enums mirroring raylib's key codes.
#pragma once

namespace meowyrender {

enum class KeyboardKey : int {
    Null = 0,
    // Alphanumeric
    Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
    Zero = 48, One = 49, Two = 50, Three = 51, Four = 52,
    Five = 53, Six = 54, Seven = 55, Eight = 56, Nine = 57,
    Semicolon = 59, Equal = 61,
    A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72,
    I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80,
    Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88,
    Y = 89, Z = 90,
    LeftBracket = 91, Backslash = 92, RightBracket = 93, Grave = 96,
    // Function keys
    Space = 32, Escape = 256, Enter = 257, Tab = 258, Backspace = 259,
    Insert = 260, Delete = 261, Right = 262, Left = 263, Down = 264,
    Up = 265, PageUp = 266, PageDown = 267, Home = 268, End = 269,
    CapsLock = 280, ScrollLock = 281, NumLock = 282, PrintScreen = 283,
    Pause = 284,
    F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295,
    F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,
    LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
    RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347,
    KbMenu = 348,
    // Keypad
    Kp0 = 320, Kp1 = 321, Kp2 = 322, Kp3 = 323, Kp4 = 324,
    Kp5 = 325, Kp6 = 326, Kp7 = 327, Kp8 = 328, Kp9 = 329,
    KpDecimal = 330, KpDivide = 331, KpMultiply = 332,
    KpSubtract = 333, KpAdd = 334, KpEnter = 335, KpEqual = 336,
};

enum class MouseButton : int {
    Left = 0, Right = 1, Middle = 2,
    Side = 3, Extra = 4, Forward = 5, Back = 6,
};

enum class MouseCursor : int {
    Default = 0, Arrow, IBeam, Crosshair, PointingHand,
    ResizeEW, ResizeNS, ResizeNWSE, ResizeNESW, ResizeAll, NotAllowed,
};

enum class GamepadButton : int {
    Unknown = 0,
    LeftFaceUp, LeftFaceRight, LeftFaceDown, LeftFaceLeft,
    RightFaceUp, RightFaceRight, RightFaceDown, RightFaceLeft,
    LeftTrigger1, LeftTrigger2, RightTrigger1, RightTrigger2,
    MiddleLeft, Middle, MiddleRight,
    LeftThumb, RightThumb,
};

enum class GamepadAxis : int {
    LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger,
};

} // namespace meowyrender
