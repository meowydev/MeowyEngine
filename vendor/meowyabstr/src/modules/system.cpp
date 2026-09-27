// meowyrender - src/modules/system.cpp
// rcore-style system utilities: monitors, clipboard, gestures/touch, file
// system helpers, config flags. All backend-agnostic (GLFW + std::filesystem).
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"



#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <vector>

namespace fs = std::filesystem;

namespace meowyrender {

using detail::State;

// ===========================================================================
// Gestures / touch
// ===========================================================================
// A pragmatic implementation mapping mouse/touch to tap + drag gestures.
// GLFW on desktop exposes touch as mouse events; full multitouch is limited.
namespace {
unsigned int g_enabledGestures = 0xFFFFFFFF;
Gesture g_currentGesture = Gesture::None;
Vector2 g_dragStart{};
Vector2 g_dragVector{};
double g_holdStart = 0.0;
float g_holdDuration = 0.0f;

// Recompute the current gesture from the live mouse/touch state. Called by any
// gesture query so a single poll per frame keeps everything consistent.
void UpdateGestureState() {
    if (IsMouseButtonPressed(MouseButton::Left)) {
        g_currentGesture = Gesture::Tap;
        g_dragStart = GetMousePosition();
        g_dragVector = {0, 0};
        g_holdStart = GetTime();
        g_holdDuration = 0.0f;
    } else if (IsMouseButtonDown(MouseButton::Left)) {
        Vector2 now = GetMousePosition();
        g_dragVector = {now.x - g_dragStart.x, now.y - g_dragStart.y};
        g_holdDuration = static_cast<float>(GetTime() - g_holdStart);
        if (g_dragVector.x * g_dragVector.x + g_dragVector.y * g_dragVector.y > 4.0f)
            g_currentGesture = Gesture::Drag;
        else if (g_holdDuration > 0.4f)
            g_currentGesture = Gesture::Hold;
    } else {
        g_currentGesture = Gesture::None;
        g_holdDuration = 0.0f;
    }
}
} // namespace

void SetGesturesEnabled(unsigned int flags) { g_enabledGestures = flags; }

bool IsGestureDetected(Gesture gesture) {
    UpdateGestureState();
    return g_currentGesture == gesture &&
           (g_enabledGestures & static_cast<unsigned int>(gesture));
}
int GetGestureDetected() {
    UpdateGestureState();
    return (g_enabledGestures & static_cast<unsigned int>(g_currentGesture))
               ? static_cast<int>(g_currentGesture) : 0;
}
float GetGestureHoldDuration() { UpdateGestureState(); return g_holdDuration; }
Vector2 GetGestureDragVector() { UpdateGestureState(); return g_dragVector; }
float GetGestureDragAngle() {
    UpdateGestureState();
    return std::atan2(g_dragVector.y, g_dragVector.x) * RAD2DEG;
}
// Pinch is computed from the two active touch points. On single-pointer desktop
// (GLFW delivers at most one touch point, mirrored from the mouse) fewer than
// two points exist, so this correctly reports a zero vector / angle; on a
// multitouch platform that populates >=2 touch points it returns the real
// separation vector and its angle.
Vector2 GetGesturePinchVector() {
    if (GetTouchPointCount() < 2) return {0, 0};
    Vector2 a = GetTouchPosition(0), b = GetTouchPosition(1);
    return {b.x - a.x, b.y - a.y};
}
float GetGesturePinchAngle() {
    if (GetTouchPointCount() < 2) return 0.0f;
    Vector2 v = GetGesturePinchVector();
    return std::atan2(v.y, v.x) * (180.0f / 3.14159265358979323846f);
}

// GetTouchPointCount / GetTouchPosition / GetTouchX / GetTouchY / GetTouchPointId
// now live in core/input.cpp (backed by real touch state populated per frame).

// ===========================================================================
// File system
// ===========================================================================
bool FileExists(const std::string& fileName) {
    std::error_code ec;
    return fs::is_regular_file(fileName, ec);
}
bool DirectoryExists(const std::string& dirPath) {
    std::error_code ec;
    return fs::is_directory(dirPath, ec);
}
std::string GetFileExtension(const std::string& fileName) {
    return fs::path(fileName).extension().string();
}
std::string GetFileName(const std::string& filePath) {
    return fs::path(filePath).filename().string();
}
std::string GetFileNameWithoutExt(const std::string& filePath) {
    return fs::path(filePath).stem().string();
}
std::string GetDirectoryPath(const std::string& filePath) {
    return fs::path(filePath).parent_path().string();
}
std::string GetWorkingDirectory() {
    std::error_code ec;
    return fs::current_path(ec).string();
}
bool ChangeDirectory(const std::string& dir) {
    std::error_code ec;
    fs::current_path(dir, ec);
    return !ec;
}
long GetFileModTime(const std::string& fileName) {
    std::error_code ec;
    auto t = fs::last_write_time(fileName, ec);
    if (ec) return 0;
    return static_cast<long>(t.time_since_epoch().count());
}

unsigned char* LoadFileData(const std::string& fileName, int* bytesRead) {
    std::ifstream f(fileName, std::ios::binary | std::ios::ate);
    if (!f) { if (bytesRead) *bytesRead = 0; return nullptr; }
    const std::streamsize size = f.tellg();
    f.seekg(0);
    auto* data = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(size)));
    f.read(reinterpret_cast<char*>(data), size);
    if (bytesRead) *bytesRead = static_cast<int>(size);
    return data;
}
void UnloadFileData(unsigned char* data) { std::free(data); }

bool SaveFileData(const std::string& fileName, const void* data, int bytesToWrite) {
    std::ofstream f(fileName, std::ios::binary);
    if (!f) return false;
    f.write(static_cast<const char*>(data), bytesToWrite);
    return f.good();
}

char* LoadFileText(const std::string& fileName) {
    std::ifstream f(fileName, std::ios::binary | std::ios::ate);
    if (!f) return nullptr;
    const std::streamsize size = f.tellg();
    f.seekg(0);
    auto* text = static_cast<char*>(std::malloc(static_cast<std::size_t>(size) + 1));
    f.read(text, size);
    text[size] = '\0';
    return text;
}
void UnloadFileText(char* text) { std::free(text); }

bool SaveFileText(const std::string& fileName, const std::string& text) {
    std::ofstream f(fileName);
    if (!f) return false;
    f << text;
    return f.good();
}

// ===========================================================================
// Additional path / file helpers (raylib parity)
// ===========================================================================
int GetFileLength(const std::string& fileName) {
    std::error_code ec;
    auto sz = fs::file_size(fileName, ec);
    return ec ? 0 : static_cast<int>(sz);
}
bool IsFileExtension(const std::string& fileName, const std::string& ext) {
    // raylib allows a ';'-separated list of extensions, case-insensitive.
    std::string actual = GetFileExtension(fileName);
    auto lower = [](std::string s){ for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    actual = lower(actual);
    std::string list = ext;
    std::size_t start = 0;
    while (start <= list.size()) {
        std::size_t sep = list.find(';', start);
        std::string one = list.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        if (!one.empty() && lower(one) == actual) return true;
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return false;
}
bool IsPathFile(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}
bool IsFileNameValid(const std::string& fileName) {
    if (fileName.empty()) return false;
    // Reject characters disallowed on common filesystems.
    static const std::string invalid = "<>:\"/\\|?*";
    for (char c : fileName)
        if (invalid.find(c) != std::string::npos || static_cast<unsigned char>(c) < 32) return false;
    return true;
}
std::string GetPrevDirectoryPath(const std::string& dirPath) {
    fs::path p(dirPath);
    return p.parent_path().string();
}
std::string GetApplicationDirectory() {
    std::error_code ec;
    // Best-effort: current path. Platform-specific exe path resolution is left
    // to the caller; documented as returning the working directory here.
    return fs::current_path(ec).string() + "/";
}
bool MakeDirectory(const std::string& dirPath) {
    std::error_code ec;
    if (fs::exists(dirPath, ec)) return true;
    return fs::create_directories(dirPath, ec) && !ec;
}
int GetDirectoryFileCount(const std::string& dirPath) {
    std::error_code ec;
    int count = 0;
    for (auto it = fs::directory_iterator(dirPath, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) ++count;
    return count;
}
namespace {
FilePathList MakePathList(const std::vector<std::string>& paths) {
    FilePathList list{};
    list.count = static_cast<unsigned int>(paths.size());
    list.capacity = list.count;
    if (list.count == 0) return list;
    list.paths = static_cast<char**>(std::malloc(sizeof(char*) * list.count));
    for (unsigned int i = 0; i < list.count; ++i) {
        list.paths[i] = static_cast<char*>(std::malloc(paths[i].size() + 1));
        std::memcpy(list.paths[i], paths[i].c_str(), paths[i].size() + 1);
    }
    return list;
}
} // namespace
FilePathList LoadDirectoryFiles(const std::string& dirPath) {
    std::error_code ec;
    std::vector<std::string> paths;
    for (auto it = fs::directory_iterator(dirPath, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        paths.push_back(it->path().string());
    return MakePathList(paths);
}
FilePathList LoadDirectoryFilesEx(const std::string& basePath, const std::string& filter, bool scanSubdirs) {
    std::error_code ec;
    std::vector<std::string> paths;
    auto matches = [&](const fs::path& p) {
        if (filter.empty() || filter == "DIR") return true;
        return IsFileExtension(p.string(), filter);
    };
    if (scanSubdirs) {
        for (auto it = fs::recursive_directory_iterator(basePath, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec) && matches(it->path())) paths.push_back(it->path().string());
    } else {
        for (auto it = fs::directory_iterator(basePath, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec) && matches(it->path())) paths.push_back(it->path().string());
    }
    return MakePathList(paths);
}
void UnloadDirectoryFiles(FilePathList files) {
    if (!files.paths) return;
    for (unsigned int i = 0; i < files.count; ++i) std::free(files.paths[i]);
    std::free(files.paths);
}
bool ExportDataAsCode(const unsigned char* data, int dataSize, const std::string& fileName) {
    if (!data || dataSize <= 0) return false;
    std::ofstream f(fileName);
    if (!f) return false;
    std::string var = GetFileNameWithoutExt(fileName);
    for (auto& c : var) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    f << "// Exported by MeowyRender ExportDataAsCode\n";
    f << "#define " << var << "_DATA_SIZE " << dataSize << "\n\n";
    f << "static const unsigned char " << var << "_DATA[" << dataSize << "] = {";
    for (int i = 0; i < dataSize; ++i) {
        if (i % 20 == 0) f << "\n    ";
        f << "0x" << std::hex << static_cast<int>(data[i]) << std::dec;
        if (i + 1 < dataSize) f << ", ";
    }
    f << "\n};\n";
    return f.good();
}

bool FileCopy(const std::string& srcPath, const std::string& dstPath) {
    std::error_code ec;
    fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
    return !ec;
}
bool FileMove(const std::string& srcPath, const std::string& dstPath) {
    std::error_code ec;
    fs::rename(srcPath, dstPath, ec);
    if (!ec) return true;
    // Cross-device fallback: copy then remove.
    fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::remove(srcPath, ec);
    return !ec;
}
bool FileRemove(const std::string& fileName) {
    std::error_code ec;
    return fs::remove(fileName, ec) && !ec;
}
bool FileRename(const std::string& srcPath, const std::string& dstPath) {
    std::error_code ec;
    fs::rename(srcPath, dstPath, ec);
    return !ec;
}

// ===========================================================================
// Misc
// ===========================================================================
void SetRandomSeed(unsigned int seed) { std::srand(seed); }

} // namespace meowyrender
