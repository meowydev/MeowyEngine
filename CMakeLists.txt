cmake_minimum_required(VERSION 4.3)
project(MeowyEngine)

set(CMAKE_CXX_STANDARD 26)

# MeowyRender lives inside MeowyEngine as a vendored, independently buildable
# CMake project. Disable its standalone samples when it is consumed here.
set(MEOWY_BUILD_SAMPLES OFF CACHE BOOL "Build MeowyRender samples" FORCE)
set(MEOWY_WITH_IMGUI    ON  CACHE BOOL "Build MeowyRender with Dear ImGui" FORCE)
add_subdirectory(vendor/meowyabstr)

add_library(MeowyEngine STATIC
        src/window.cpp
        src/window.hpp
        src/render.hpp
        src/graphics/draw.cpp
        src/graphics/draw.hpp
        src/meowyengine.hpp
        src/graphics/input.cpp
        src/graphics/input.hpp

)

target_precompile_headers(MeowyEngine PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/render.hpp>"
)

target_include_directories(MeowyEngine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/meowyabstr
)

add_executable(StormChasing
        stormchasing/main.cpp
)

target_precompile_headers(StormChasing PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/render.hpp>"
)

target_link_libraries(StormChasing PRIVATE MeowyEngine)

target_link_libraries(MeowyEngine
        PUBLIC
        meowyrender::meowyrender
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(MeowyEngine PUBLIC  # Replace with your project name
            -Wall
            -Wextra
            -Wpedantic
    )
endif()
