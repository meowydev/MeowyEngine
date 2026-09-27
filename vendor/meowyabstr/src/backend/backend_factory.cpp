// meowyrender - src/backend/backend_factory.cpp  (internal)
// Runtime backend factory. Every backend compiled into this build (gated by the
// MEOWY_HAVE_BACKEND_* defines set in CMake) can be enumerated and constructed
// here at runtime; the active backend is chosen during InitWindow. Constructing
// a backend only allocates the object -- it does not create a device/context --
// so a failed CreateBackend never leaves GPU state behind (Init() does that and
// is torn down separately by the caller on failure).
#include "backend/render_backend.hpp"

#if defined(MEOWY_HAVE_BACKEND_OPENGL)
#  include "backend/opengl/gl_backend.hpp"
#endif
#if defined(MEOWY_HAVE_BACKEND_METAL)
#  include "backend/metal/metal_backend.hpp"
#endif
#if defined(MEOWY_HAVE_BACKEND_VULKAN)
#  include "backend/vulkan/vk_backend.hpp"
#endif

namespace meowyrender::backend {

bool IsBackendCompiled(BackendKind kind) {
    switch (kind) {
        case BackendKind::OpenGL:
#if defined(MEOWY_HAVE_BACKEND_OPENGL)
            return true;
#else
            return false;
#endif
        case BackendKind::Metal:
#if defined(MEOWY_HAVE_BACKEND_METAL)
            return true;
#else
            return false;
#endif
        case BackendKind::Vulkan:
#if defined(MEOWY_HAVE_BACKEND_VULKAN)
            return true;
#else
            return false;
#endif
    }
    return false;
}

std::unique_ptr<RenderBackend> CreateBackend(BackendKind kind) {
    switch (kind) {
        case BackendKind::OpenGL:
#if defined(MEOWY_HAVE_BACKEND_OPENGL)
            return std::make_unique<gl::GLBackend>();
#else
            return nullptr;
#endif
        case BackendKind::Metal:
#if defined(MEOWY_HAVE_BACKEND_METAL)
            return std::make_unique<metal::MetalBackend>();
#else
            return nullptr;
#endif
        case BackendKind::Vulkan:
#if defined(MEOWY_HAVE_BACKEND_VULKAN)
            return std::make_unique<vulkan::VulkanBackend>();
#else
            return nullptr;
#endif
    }
    return nullptr;
}

} // namespace meowyrender::backend
