// meowyrender - src/modules/rcore_events.cpp
// rcore logging/callbacks, misc window control, dropped files, and a functional
// automation event record/playback system. Backend-agnostic.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace meowyrender {

using detail::State;

// ===========================================================================
// Logging + level + callback (consumed by TraceLog in core.cpp via these hooks)
// ===========================================================================
namespace detail {
int g_traceLogLevel = 0;                       // LOG_ALL
TraceLogCallback g_traceLogCallback = nullptr;
LoadFileDataCallback g_loadFileDataCallback = nullptr;
SaveFileDataCallback g_saveFileDataCallback = nullptr;
LoadFileTextCallback g_loadFileTextCallback = nullptr;
SaveFileTextCallback g_saveFileTextCallback = nullptr;
} // namespace detail

void SetTraceLogLevel(int logLevel) { detail::g_traceLogLevel = logLevel; }
void SetTraceLogCallback(TraceLogCallback callback) { detail::g_traceLogCallback = callback; }
void SetLoadFileDataCallback(LoadFileDataCallback callback) { detail::g_loadFileDataCallback = callback; }
void SetSaveFileDataCallback(SaveFileDataCallback callback) { detail::g_saveFileDataCallback = callback; }
void SetLoadFileTextCallback(LoadFileTextCallback callback) { detail::g_loadFileTextCallback = callback; }
void SetSaveFileTextCallback(SaveFileTextCallback callback) { detail::g_saveFileTextCallback = callback; }

// ===========================================================================
// Misc window control
// ===========================================================================
void SwapScreenBuffer() {
    // Present without running the frame-timing bookkeeping (advanced control).
    auto& s = State();
    detail::FlushBatch();
    if (s.backend) s.backend->EndFrame();
}
void ToggleBorderlessWindowed() {
    // Reuse the fullscreen toggle path; on desktop this maps to a borderless
    // fullscreen window, which is what raylib's borderless mode provides.
    ToggleFullscreen();
}
#if defined(__APPLE__)
namespace detail { Image PlatformClipboardImage(); }  // clipboard_apple.mm
#endif
Image GetClipboardImage() {
    // Apple platforms read the real system pasteboard image (NSPasteboard /
    // UIPasteboard) via clipboard_apple.mm. GLFW itself exposes no image
    // clipboard, so on non-Apple desktops this returns an empty image (the
    // caller should check image.data), which is honestly reported as an
    // unavailable path rather than fabricating pixels.
#if defined(__APPLE__)
    return detail::PlatformClipboardImage();
#else
    return Image{};
#endif
}
int GetShaderLocationAttrib(Shader shader, const std::string& attribName) {
    // Vertex attributes are fixed in MeowyRender's batch layout; expose the
    // uniform-location lookup as a best-effort (returns -1 if not found).
    auto& s = State();
    return s.backend ? s.backend->GetShaderUniformLocation(shader.id, attribName.c_str()) : -1;
}
void SetShaderValueTexture(Shader shader, int locIndex, Texture2D texture) {
    // Bind a texture handle to a sampler uniform via the int-uniform path.
    auto& s = State();
    if (s.backend && locIndex >= 0) {
        int unit = static_cast<int>(texture.id);
        s.backend->SetShaderUniform(shader.id, locIndex, &unit, 4, 1);
    }
}

int GetDirectoryFileCountEx(const std::string& basePath, const std::string& filter, bool scanSubdirs) {
    FilePathList list = LoadDirectoryFilesEx(basePath, filter, scanSubdirs);
    const int count = static_cast<int>(list.count);
    UnloadDirectoryFiles(list);
    return count;
}
int FileTextFindIndex(const std::string& fileName, const std::string& find) {
    char* text = LoadFileText(fileName);
    if (!text) return -1;
    const char* p = std::strstr(text, find.c_str());
    const int idx = p ? static_cast<int>(p - text) : -1;
    UnloadFileText(text);
    return idx;
}
bool FileTextReplace(const std::string& fileName, const std::string& find, const std::string& by) {
    char* text = LoadFileText(fileName);
    if (!text) return false;
    char* replaced = TextReplace(text, find.c_str(), by.c_str());
    UnloadFileText(text);
    if (!replaced) return false;
    const bool ok = SaveFileText(fileName, replaced);
    MemFree(replaced);
    return ok;
}

// ===========================================================================
// Dropped files (populated by the platform layer's drop callback)
// ===========================================================================
namespace detail { std::vector<std::string> g_droppedFiles; }

bool IsFileDropped() { return !detail::g_droppedFiles.empty(); }
FilePathList LoadDroppedFiles() {
    FilePathList list{};
    auto& dropped = detail::g_droppedFiles;
    list.count = static_cast<unsigned int>(dropped.size());
    list.capacity = list.count;
    if (list.count == 0) return list;
    list.paths = static_cast<char**>(std::malloc(sizeof(char*) * list.count));
    for (unsigned int i = 0; i < list.count; ++i) {
        list.paths[i] = static_cast<char*>(std::malloc(dropped[i].size() + 1));
        std::memcpy(list.paths[i], dropped[i].c_str(), dropped[i].size() + 1);
    }
    return list;
}
void UnloadDroppedFiles(FilePathList files) {
    if (files.paths) {
        for (unsigned int i = 0; i < files.count; ++i) std::free(files.paths[i]);
        std::free(files.paths);
    }
    detail::g_droppedFiles.clear();
}

// ===========================================================================
// Automation events (record + playback of input events)
// ===========================================================================
namespace detail {
AutomationEventList* g_activeEventList = nullptr;
bool g_recording = false;
unsigned int g_eventBaseFrame = 0;

void RecordAutomationEvent(unsigned int type, int p0, int p1, int p2, int p3) {
    if (!g_recording || !g_activeEventList) return;
    auto* list = g_activeEventList;
    if (list->count >= list->capacity) {
        const unsigned int newCap = list->capacity ? list->capacity * 2 : 64;
        list->events = static_cast<AutomationEvent*>(
            std::realloc(list->events, sizeof(AutomationEvent) * newCap));
        list->capacity = newCap;
    }
    AutomationEvent& e = list->events[list->count++];
    e.frame = g_eventBaseFrame;   // caller updates base frame per logical step
    e.type = type;
    e.params[0] = p0; e.params[1] = p1; e.params[2] = p2; e.params[3] = p3;
}
} // namespace detail

AutomationEventList LoadAutomationEventList(const std::string& fileName) {
    AutomationEventList list{};
    if (fileName.empty()) {
        // Allocate an empty growable list for recording.
        list.capacity = 64;
        list.events = static_cast<AutomationEvent*>(std::malloc(sizeof(AutomationEvent) * list.capacity));
        return list;
    }
    std::ifstream f(fileName);
    if (!f) return list;
    std::vector<AutomationEvent> events;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        AutomationEvent e{};
        std::istringstream ss(line);
        ss >> e.frame >> e.type >> e.params[0] >> e.params[1] >> e.params[2] >> e.params[3];
        events.push_back(e);
    }
    list.count = static_cast<unsigned int>(events.size());
    list.capacity = list.count ? list.count : 64;
    list.events = static_cast<AutomationEvent*>(std::malloc(sizeof(AutomationEvent) * list.capacity));
    if (list.count) std::memcpy(list.events, events.data(), sizeof(AutomationEvent) * list.count);
    return list;
}
void UnloadAutomationEventList(AutomationEventList list) { std::free(list.events); }
bool ExportAutomationEventList(AutomationEventList list, const std::string& fileName) {
    std::ofstream f(fileName);
    if (!f) return false;
    f << "# MeowyRender automation events: frame type p0 p1 p2 p3\n";
    for (unsigned int i = 0; i < list.count; ++i) {
        const AutomationEvent& e = list.events[i];
        f << e.frame << ' ' << e.type << ' ' << e.params[0] << ' ' << e.params[1]
          << ' ' << e.params[2] << ' ' << e.params[3] << '\n';
    }
    return f.good();
}
void SetAutomationEventList(AutomationEventList* list) { detail::g_activeEventList = list; }
void SetAutomationEventBaseFrame(int frame) { detail::g_eventBaseFrame = static_cast<unsigned int>(frame); }
void StartAutomationEventRecording() { detail::g_recording = true; }
void StopAutomationEventRecording() { detail::g_recording = false; }
void PlayAutomationEvent(AutomationEvent event) {
    // Apply the recorded event to the live input state.
    auto& in = State().input;
    switch (event.type) {
        case 0: /* NONE */ break;
        case 1: if (event.params[0] >= 0 && event.params[0] < 512) in.keysCurrent[event.params[0]] = false; break; // KEY_UP
        case 2: if (event.params[0] >= 0 && event.params[0] < 512) { in.keysCurrent[event.params[0]] = true; in.lastKeyPressed = event.params[0]; } break; // KEY_DOWN
        case 3: if (event.params[0] >= 0 && event.params[0] < 8) in.mouseCurrent[event.params[0]] = false; break; // MOUSE_BUTTON_UP
        case 4: if (event.params[0] >= 0 && event.params[0] < 8) in.mouseCurrent[event.params[0]] = true; break;  // MOUSE_BUTTON_DOWN
        case 5: in.mousePosition = {static_cast<float>(event.params[0]), static_cast<float>(event.params[1])}; break; // MOUSE_POSITION
        default: break;
    }
}

} // namespace meowyrender