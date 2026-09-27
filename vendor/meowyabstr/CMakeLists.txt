cmake_minimum_required(VERSION 3.25)

# Objective-C++ is needed for the Metal backend on Apple platforms; declare it
# up front so CMake configures the OBJCXX archive rules (required by Ninja).
if(APPLE)
    # OBJC maps .m files (GLFW's Cocoa sources) to the Objective-C compiler;
    # OBJCXX maps our .mm Metal backend to the Objective-C++ compiler. Declaring
    # both keeps the correct per-extension language mapping.
    set(MEOWY_PROJECT_LANGUAGES C CXX OBJC OBJCXX)
else()
    set(MEOWY_PROJECT_LANGUAGES C CXX)
endif()

project(meowyrender
    VERSION 0.1.0
    DESCRIPTION "A raylib-style rendering abstraction over Metal, OpenGL and Vulkan"
    LANGUAGES ${MEOWY_PROJECT_LANGUAGES})
set(MEOWY_VISIONOS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "visionOS")
    set(MEOWY_VISIONOS ON)
endif()

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Backend selection
# ---------------------------------------------------------------------------
# All backends supported on the current platform are compiled into the static
# library by default; the active backend is chosen at runtime during InitWindow
# (see SetPreferredBackend / automatic selection). Each backend can be disabled
# individually to shrink the binary or drop a dependency:
#     MEOWY_ENABLE_OPENGL / MEOWY_ENABLE_METAL / MEOWY_ENABLE_VULKAN
#
# For the legacy single-backend build, set MEOWY_BACKEND to one of OpenGL/Metal/
# Vulkan; that turns OFF the other two (smallest binary, one backend compiled).
# MEOWY_BACKEND left empty (the default) means "multi-backend automatic".
set(MEOWY_BACKEND "" CACHE STRING "Legacy single-backend build: OpenGL, Metal, Vulkan, or empty for multi-backend")
set_property(CACHE MEOWY_BACKEND PROPERTY STRINGS "" OpenGL Metal Vulkan)

# Per-backend enable options. Defaults are resolved by platform below.
option(MEOWY_ENABLE_OPENGL "Compile the OpenGL backend (if supported on this platform)" ON)
option(MEOWY_ENABLE_METAL  "Compile the Metal backend (Apple platforms only)" ON)
option(MEOWY_ENABLE_VULKAN "Compile the Vulkan backend (if Vulkan is available)" ON)
option(MEOWY_VULKAN_USE_MOLTENVK "Use MoltenVK for the Vulkan backend on Apple platforms" ON)
option(MEOWY_BUILD_SAMPLES "Build the sample applications" ON)
include(CTest)

# --- Resolve which backends are actually compiled for this platform ----------
# Start from the enable options, then apply hard platform constraints.
set(MEOWY_HAVE_OPENGL ${MEOWY_ENABLE_OPENGL})
set(MEOWY_HAVE_METAL  ${MEOWY_ENABLE_METAL})
set(MEOWY_HAVE_VULKAN ${MEOWY_ENABLE_VULKAN})

# Legacy single-backend selection: MEOWY_BACKEND=X compiles only X.
if(MEOWY_BACKEND STREQUAL "OpenGL")
    set(MEOWY_HAVE_OPENGL ON)
    set(MEOWY_HAVE_METAL OFF)
    set(MEOWY_HAVE_VULKAN OFF)
elseif(MEOWY_BACKEND STREQUAL "Metal")
    set(MEOWY_HAVE_OPENGL OFF)
    set(MEOWY_HAVE_METAL ON)
    set(MEOWY_HAVE_VULKAN OFF)
elseif(MEOWY_BACKEND STREQUAL "Vulkan")
    set(MEOWY_HAVE_OPENGL OFF)
    set(MEOWY_HAVE_METAL OFF)
    set(MEOWY_HAVE_VULKAN ON)
elseif(NOT MEOWY_BACKEND STREQUAL "")
    message(FATAL_ERROR "Unknown MEOWY_BACKEND: '${MEOWY_BACKEND}' (use OpenGL, Metal, Vulkan, or empty)")
endif()

# Metal is Apple-only.
if(NOT APPLE)
    set(MEOWY_HAVE_METAL OFF)
endif()

# visionOS: Metal only. It has no GLFW/desktop windowing, so OpenGL and desktop
# Vulkan/MoltenVK are unavailable there regardless of the enable options.
if(MEOWY_VISIONOS)
    set(MEOWY_HAVE_OPENGL OFF)
    set(MEOWY_HAVE_VULKAN OFF)
    if(NOT MEOWY_HAVE_METAL)
        message(FATAL_ERROR "The native visionOS host requires the Metal backend (MEOWY_ENABLE_METAL=ON)")
    endif()
endif()

# Vulkan must degrade cleanly when its dependencies are unavailable: probe for
# the loader + glslang, and if either is missing, disable Vulkan (keeping
# Metal/OpenGL) instead of failing the whole configure. A user who explicitly
# asked for the single-backend Vulkan build gets a hard error instead.
if(MEOWY_HAVE_VULKAN)
    find_package(Vulkan QUIET)
    find_package(glslang CONFIG QUIET)
    find_program(MEOWY_GLSLANG glslangValidator)
    if(NOT Vulkan_FOUND OR NOT glslang_FOUND OR NOT MEOWY_GLSLANG)
        if(MEOWY_BACKEND STREQUAL "Vulkan")
            message(FATAL_ERROR "MEOWY_BACKEND=Vulkan requested but Vulkan/glslang/glslangValidator not found")
        endif()
        message(STATUS "meowyrender: Vulkan dependencies not found -- Vulkan backend disabled (Metal/OpenGL retained)")
        set(MEOWY_HAVE_VULKAN OFF)
    endif()
endif()

# GLFW-based desktop windowing is required whenever OpenGL or Vulkan is compiled.
if(MEOWY_HAVE_OPENGL OR MEOWY_HAVE_VULKAN)
    set(MEOWY_NEED_GLFW ON)
else()
    set(MEOWY_NEED_GLFW OFF)
endif()

if(NOT (MEOWY_HAVE_OPENGL OR MEOWY_HAVE_METAL OR MEOWY_HAVE_VULKAN))
    message(FATAL_ERROR "No rendering backend is enabled/available. Enable at least one of MEOWY_ENABLE_OPENGL/METAL/VULKAN.")
endif()
message(STATUS "meowyrender backends compiled: OpenGL=${MEOWY_HAVE_OPENGL} Metal=${MEOWY_HAVE_METAL} Vulkan=${MEOWY_HAVE_VULKAN}")

message(STATUS "meowyrender backend: ${MEOWY_BACKEND}")

# ---------------------------------------------------------------------------
# Dependencies (GLFW for windowing + input on desktop backends)
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
if(MEOWY_NEED_GLFW)
    FetchContent_MakeAvailable(glfw)
endif()

# ---------------------------------------------------------------------------
# SDL (desktop gamepad rumble only). GLFW 3.4 exposes no haptics API, so the
# desktop backend uses SDL's gamepad subsystem purely for SetGamepadVibration
# while GLFW keeps ownership of windowing and all input. SDL is OPTIONAL: if it
# is not found, the library still builds and SetGamepadVibration degrades to a
# documented no-op. Never used on the visionOS host (that path uses the
# GameController framework, not GLFW).
# ---------------------------------------------------------------------------
option(MEOWY_USE_SDL_GAMEPAD "Use SDL for gamepad rumble on desktop (GLFW) backends" ON)
set(MEOWY_HAVE_SDL_GAMEPAD OFF)
if(MEOWY_USE_SDL_GAMEPAD AND MEOWY_NEED_GLFW)
    find_package(SDL3 QUIET)
    if(SDL3_FOUND)
        set(MEOWY_HAVE_SDL_GAMEPAD ON)
        message(STATUS "meowyrender: SDL3 found -- SetGamepadVibration uses SDL gamepad rumble")
    else()
        message(STATUS "meowyrender: SDL3 not found -- SetGamepadVibration is a no-op (build MEOWY_USE_SDL_GAMEPAD=ON with SDL3 available to enable rumble)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Dear ImGui (optional, OFF by default). When enabled, MeowyRender fetches Dear
# ImGui and compiles the renderer backend(s) for the compiled MeowyRender
# backends (OpenGL / Metal / Vulkan). Consumers then call InitImGui() + raw
# ImGui:: widgets between BeginDrawing/EndDrawing.
#
# Platform layer per target:
#   - Desktop (OpenGL/Vulkan/Metal over GLFW): imgui_impl_glfw supplies input.
#   - visionOS (Metal only, no GLFW): a custom platform layer in the Metal
#     backend feeds ImGui IO from the UIKit view + engine input ("by Metaling
#     the way"), so ImGui is available there too.
# ---------------------------------------------------------------------------
option(MEOWY_WITH_IMGUI "Build with Dear ImGui integration (desktop GLFW backends + visionOS Metal)" OFF)
set(MEOWY_HAVE_IMGUI OFF)
if(MEOWY_WITH_IMGUI)
    if(NOT MEOWY_NEED_GLFW AND NOT MEOWY_VISIONOS)
        message(STATUS "meowyrender: MEOWY_WITH_IMGUI requires a GLFW desktop backend (OpenGL/Vulkan) or visionOS (Metal); ImGui disabled")
    else()
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG        v1.91.5-docking
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(imgui)
        set(MEOWY_HAVE_IMGUI ON)
        set(MEOWY_IMGUI_DIR "${imgui_SOURCE_DIR}")
        message(STATUS "meowyrender: Dear ImGui enabled (${imgui_SOURCE_DIR})")
    endif()
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Samples
# ---------------------------------------------------------------------------
if(MEOWY_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()
