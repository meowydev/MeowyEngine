# meowyrender static library
# Sources common to all backends.
set(MEOWY_SOURCES
    core/core.cpp
    core/input.cpp
    modules/shapes.cpp
    modules/textures.cpp
    modules/text.cpp
    modules/audio.cpp
    modules/models3d.cpp
    modules/models.cpp
    modules/model_loader.cpp
    modules/animation.cpp
    modules/stereo.cpp
    modules/cubemap.cpp
    modules/shaders.cpp
    modules/system.cpp
    modules/rcore_util.cpp
    modules/rcore_events.cpp
    modules/rtext_util.cpp
    modules/rtextures_util.cpp
    modules/raudio_util.cpp
    modules/ibl_precompute.cpp
    backend/backend_factory.cpp
    thirdparty/stb_impl.cpp
    thirdparty/cgltf_impl.cpp
)
if(MEOWY_VISIONOS)
    list(APPEND MEOWY_SOURCES core/platform_visionos.mm)
else()
    list(APPEND MEOWY_SOURCES core/platform.cpp)
endif()
# Real clipboard-image read (NSPasteboard/UIPasteboard) on Apple platforms.
if(APPLE)
    list(APPEND MEOWY_SOURCES core/clipboard_apple.mm)
endif()

# Backend-specific sources. Every backend compiled for this platform (resolved
# in the root CMakeLists as MEOWY_HAVE_OPENGL/METAL/VULKAN) is added, and a
# per-backend MEOWY_HAVE_BACKEND_* compile define lets the code enumerate and
# construct backends at runtime. These can coexist in one static library.
set(MEOWY_BACKEND_DEFINES "")
if(MEOWY_HAVE_OPENGL)
    list(APPEND MEOWY_SOURCES backend/opengl/gl_backend.cpp)
    list(APPEND MEOWY_BACKEND_DEFINES MEOWY_HAVE_BACKEND_OPENGL)
endif()
if(MEOWY_HAVE_METAL)
    list(APPEND MEOWY_SOURCES backend/metal/metal_backend.mm)
    list(APPEND MEOWY_BACKEND_DEFINES MEOWY_HAVE_BACKEND_METAL)
endif()
if(MEOWY_HAVE_VULKAN)
    list(APPEND MEOWY_SOURCES backend/vulkan/vk_backend.cpp)
    list(APPEND MEOWY_BACKEND_DEFINES MEOWY_HAVE_BACKEND_VULKAN)
endif()

add_library(meowyrender STATIC ${MEOWY_SOURCES})
add_library(meowyrender::meowyrender ALIAS meowyrender)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/backend/pbr.glsl" MEOWY_PBR_SOURCE)
configure_file(backend/pbr_shader.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/backend/pbr_shader.hpp" @ONLY)
target_include_directories(meowyrender PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(meowyrender PUBLIC cxx_std_26)

target_include_directories(meowyrender
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/third_party/stb
        ${PROJECT_SOURCE_DIR}/third_party/miniaudio
        ${PROJECT_SOURCE_DIR}/third_party/cgltf
)

# Per-backend availability defines (private: implementation detail, the public
# API exposes runtime queries instead of compile macros).
target_compile_definitions(meowyrender PRIVATE ${MEOWY_BACKEND_DEFINES})

# GLFW provides windowing + input for the desktop (OpenGL/Vulkan) backends.
if(MEOWY_VISIONOS)
    target_compile_definitions(meowyrender PUBLIC MEOWY_PLATFORM_VISIONOS)
    set_source_files_properties(core/platform_visionos.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(modules/audio.cpp PROPERTIES LANGUAGE OBJCXX)
    find_library(AVFOUNDATION_LIB AVFoundation REQUIRED)
    target_link_libraries(meowyrender PUBLIC ${AVFOUNDATION_LIB})
else()
    if(MEOWY_NEED_GLFW)
        target_link_libraries(meowyrender PUBLIC glfw)
        # SDL gamepad rumble (desktop only, optional). Linked PRIVATE so consumers
        # don't inherit an SDL dependency; the macro gates the real implementation.
        if(MEOWY_HAVE_SDL_GAMEPAD)
            target_link_libraries(meowyrender PRIVATE SDL3::SDL3)
            target_compile_definitions(meowyrender PRIVATE MEOWY_HAVE_SDL_GAMEPAD=1)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Audio (miniaudio) platform dependencies
# ---------------------------------------------------------------------------
if(APPLE)
    find_library(COREAUDIO_LIB CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_LIB AudioToolbox REQUIRED)
    find_library(AUDIOUNIT_LIB AudioUnit)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
    target_link_libraries(meowyrender PUBLIC
        ${COREAUDIO_LIB} ${AUDIOTOOLBOX_LIB} ${COREFOUNDATION_LIB})
    if(AUDIOUNIT_LIB AND NOT MEOWY_VISIONOS)
        target_link_libraries(meowyrender PUBLIC ${AUDIOUNIT_LIB})
    endif()
    # Clipboard-image read (clipboard_apple.mm) needs the UI toolkit + CoreGraphics.
    set_source_files_properties(core/clipboard_apple.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    find_library(COREGRAPHICS_LIB CoreGraphics REQUIRED)
    find_library(IMAGEIO_LIB ImageIO REQUIRED)
    target_link_libraries(meowyrender PUBLIC ${COREGRAPHICS_LIB} ${IMAGEIO_LIB})
    if(MEOWY_VISIONOS)
        find_library(UIKIT_LIB UIKit REQUIRED)
        target_link_libraries(meowyrender PUBLIC ${UIKIT_LIB})
    else()
        find_library(APPKIT_LIB AppKit REQUIRED)
        target_link_libraries(meowyrender PUBLIC ${APPKIT_LIB})
    endif()
elseif(WIN32)
    target_link_libraries(meowyrender PUBLIC shell32)
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(meowyrender PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ---------------------------------------------------------------------------
# Backend-specific dependencies
# ---------------------------------------------------------------------------
if(MEOWY_HAVE_OPENGL)
    find_package(OpenGL REQUIRED)
    target_link_libraries(meowyrender PUBLIC OpenGL::GL)
endif()

if(MEOWY_HAVE_METAL)
    find_library(METAL_LIB Metal REQUIRED)
    find_library(QUARTZCORE_LIB QuartzCore REQUIRED)
    if(MEOWY_VISIONOS)
        find_library(COCOA_LIB UIKit REQUIRED)
        find_library(GAMECONTROLLER_LIB GameController REQUIRED)
        target_link_libraries(meowyrender PUBLIC ${GAMECONTROLLER_LIB})
    else()
        find_library(COCOA_LIB Cocoa REQUIRED)
    endif()
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    target_link_libraries(meowyrender PUBLIC
        ${METAL_LIB} ${QUARTZCORE_LIB} ${COCOA_LIB} ${FOUNDATION_LIB})
    set_source_files_properties(backend/metal/metal_backend.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

if(MEOWY_HAVE_VULKAN)
    find_package(Vulkan REQUIRED)
    find_package(glslang CONFIG REQUIRED)
    target_link_libraries(meowyrender PRIVATE glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
    find_program(MEOWY_GLSLANG glslangValidator REQUIRED)
    foreach(stage vert frag)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/batch_${stage}.h
            COMMAND ${MEOWY_GLSLANG} -V --vn batch_${stage}
                -o ${CMAKE_CURRENT_BINARY_DIR}/batch_${stage}.h
                ${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan/shaders/batch.${stage}
            DEPENDS backend/vulkan/shaders/batch.${stage} backend/pbr.glsl VERBATIM)
        target_sources(meowyrender PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/batch_${stage}.h)
    endforeach()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/decode_comp.h
        COMMAND ${MEOWY_GLSLANG} -V --vn decode_comp -o ${CMAKE_CURRENT_BINARY_DIR}/decode_comp.h
            ${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan/shaders/decode.comp
        DEPENDS backend/vulkan/shaders/decode.comp VERBATIM)
    target_sources(meowyrender PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/decode_comp.h)
    # Shadow map shaders: vertex writes light-space depth, fragment stores it to
    # an R32F color target (portable across MoltenVK's depth-sampling quirks).
    foreach(sstage vert frag)
        add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shadow_${sstage}.h
            COMMAND ${MEOWY_GLSLANG} -V --vn shadow_${sstage} -o ${CMAKE_CURRENT_BINARY_DIR}/shadow_${sstage}.h
                ${CMAKE_CURRENT_SOURCE_DIR}/backend/vulkan/shaders/shadow.${sstage}
            DEPENDS backend/vulkan/shaders/shadow.${sstage} VERBATIM)
        target_sources(meowyrender PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shadow_${sstage}.h)
    endforeach()
    target_include_directories(meowyrender PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(meowyrender PUBLIC Vulkan::Vulkan)
    if(APPLE AND MEOWY_VULKAN_USE_MOLTENVK)
        target_compile_definitions(meowyrender PRIVATE MEOWY_VULKAN_MOLTENVK)
    endif()
endif()

# ---------------------------------------------------------------------------
# Dear ImGui integration (optional). Compiles ImGui core + the GLFW platform
# backend + the renderer backend(s) matching the compiled MeowyRender backends.
# The ImGui headers are exposed PUBLIC so consumers can #include <imgui.h> and
# call raw ImGui:: widgets. The per-backend ImGui hooks live inside each backend
# source, guarded by MEOWY_WITH_IMGUI.
# ---------------------------------------------------------------------------
if(MEOWY_HAVE_IMGUI)
    set(MEOWY_IMGUI_SOURCES
        ${MEOWY_IMGUI_DIR}/imgui.cpp
        ${MEOWY_IMGUI_DIR}/imgui_draw.cpp
        ${MEOWY_IMGUI_DIR}/imgui_tables.cpp
        ${MEOWY_IMGUI_DIR}/imgui_widgets.cpp
        ${MEOWY_IMGUI_DIR}/imgui_demo.cpp
    )
    if(MEOWY_NEED_GLFW)
        # Desktop platform/input backend. visionOS has no GLFW; its Metal path
        # feeds ImGui IO from the UIKit view + engine input instead.
        list(APPEND MEOWY_IMGUI_SOURCES ${MEOWY_IMGUI_DIR}/backends/imgui_impl_glfw.cpp)
    endif()
    if(MEOWY_HAVE_OPENGL)
        list(APPEND MEOWY_IMGUI_SOURCES ${MEOWY_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    endif()
    if(MEOWY_HAVE_METAL)
        # imgui_impl_metal.mm is Objective-C++; ARC required to match our .mm files.
        list(APPEND MEOWY_IMGUI_SOURCES ${MEOWY_IMGUI_DIR}/backends/imgui_impl_metal.mm)
        set_source_files_properties(${MEOWY_IMGUI_DIR}/backends/imgui_impl_metal.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif()
    if(MEOWY_HAVE_VULKAN)
        # imgui_impl_vulkan.cpp uses the linked Vulkan loader prototypes; the
        # meowyrender target already links Vulkan::Vulkan (PUBLIC), which puts
        # <vulkan/vulkan.h> on the include path for this source too.
        list(APPEND MEOWY_IMGUI_SOURCES ${MEOWY_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
    endif()
    target_sources(meowyrender PRIVATE ${MEOWY_IMGUI_SOURCES})
    target_include_directories(meowyrender PUBLIC
        $<BUILD_INTERFACE:${MEOWY_IMGUI_DIR}>
        $<BUILD_INTERFACE:${MEOWY_IMGUI_DIR}/backends>)
    target_compile_definitions(meowyrender PUBLIC MEOWY_WITH_IMGUI=1)
endif()
