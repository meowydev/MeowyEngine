// Behavioral tests for the audio DSP processor + stream-callback API and the
// touch-derived gesture pinch queries. These exercise REAL behavior (frames are
// actually generated/transformed and the mixed tap actually runs on the audio
// thread), not just that the symbols link.
//
// Audio paths require an output device; if none is available (headless CI) the
// audio assertions SKIP rather than fail, matching the rest of the suite.
#include <meowyrender/meowyrender.hpp>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
using namespace meowyrender;

static int failures = 0, skips = 0;
static void check(bool ok, const char* msg) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    if (!ok) ++failures;
}
static void skip(const char* msg) { std::printf("SKIP: %s\n", msg); ++skips; }

// --- observable processors -------------------------------------------------
// Stream processor: doubles amplitude in place AND records the last input it saw
// so the test can prove it ran over the real pushed frames.
static std::atomic<unsigned int> g_streamFrames{0};
static float g_streamFirstIn = 0.0f, g_streamFirstOut = 0.0f;
static void DoubleProcessor(void* buffer, unsigned int frames) {
    float* f = static_cast<float*>(buffer);
    g_streamFrames.store(frames);
    if (frames > 0) { g_streamFirstIn = f[0]; }
    for (unsigned int i = 0; i < frames; ++i) f[i] *= 2.0f;
    if (frames > 0) { g_streamFirstOut = f[0]; }
}
// A second stream processor to verify attach order + detach.
static std::atomic<int> g_secondCalls{0};
static void SecondProcessor(void* /*b*/, unsigned int /*f*/) { g_secondCalls.fetch_add(1); }

// Fill callback: writes a constant so we can prove the callback generated frames.
static void FillConst(void* buffer, unsigned int frames) {
    float* f = static_cast<float*>(buffer);
    for (unsigned int i = 0; i < frames; ++i) f[i] = 0.5f;
}

// Mixed processor: runs on the audio thread over the final mix; just flags that
// it was invoked at least once while audio was playing.
static std::atomic<int> g_mixedCalls{0};
static void MixedProcessor(void* /*b*/, unsigned int frames) { if (frames) g_mixedCalls.fetch_add(1); }

int main() {
    InitWindow(64, 48, "audio processor checks");

    // --- gesture pinch: honest zero on single-pointer desktop --------------
    // No touch points are active in a headless test, so pinch must report zero
    // (the implementation derives it from >=2 real touch points).
    Vector2 pv = GetGesturePinchVector();
    float pa = GetGesturePinchAngle();
    check(pv.x == 0.0f && pv.y == 0.0f && pa == 0.0f,
          "gesture pinch reports zero with fewer than two touch points");

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        skip("no audio output device; audio processor assertions skipped");
        CloseWindow();
        std::printf("%s (%d skipped)\n", failures ? "SMOKE TEST FAIL" : "SMOKE TEST PASS", skips);
        return failures ? 1 : 0;
    }

    // --- stream processor transforms the pushed frames (synchronous) -------
    AudioStream stream = LoadAudioStream(48000, 32, 1);
    check(stream.buffer != nullptr, "audio stream created");
    AttachAudioStreamProcessor(stream, DoubleProcessor);
    AttachAudioStreamProcessor(stream, DoubleProcessor); // duplicate: ignored
    AttachAudioStreamProcessor(stream, SecondProcessor);

    std::vector<float> frames(256, 0.25f);
    UpdateAudioStream(stream, frames.data(), static_cast<int>(frames.size()));
    check(g_streamFrames.load() == frames.size(), "stream processor ran over the pushed frame count");
    check(std::abs(g_streamFirstIn - 0.25f) < 1e-6f, "stream processor saw the real input samples");
    check(std::abs(g_streamFirstOut - 0.5f) < 1e-6f, "stream processor transformed samples in place (x2)");
    check(g_secondCalls.load() == 1, "second processor ran exactly once (duplicate attach ignored)");

    // Detach the doubling processor; only the second should run next time.
    DetachAudioStreamProcessor(stream, DoubleProcessor);
    g_streamFrames.store(0);
    g_secondCalls.store(0);
    UpdateAudioStream(stream, frames.data(), static_cast<int>(frames.size()));
    check(g_streamFrames.load() == 0, "detached stream processor no longer runs");
    check(g_secondCalls.load() == 1, "remaining processor still runs after detach");

    // --- stream fill callback generates frames -----------------------------
    DetachAudioStreamProcessor(stream, SecondProcessor);
    AttachAudioStreamProcessor(stream, DoubleProcessor);
    SetAudioStreamCallback(stream, FillConst); // overwrites pushed data with 0.5
    g_streamFirstOut = -1.0f;
    UpdateAudioStream(stream, frames.data(), static_cast<int>(frames.size()));
    // FillConst -> 0.5, then DoubleProcessor -> 1.0
    check(std::abs(g_streamFirstOut - 1.0f) < 1e-6f,
          "fill callback generated frames, then processor transformed them");
    SetAudioStreamCallback(stream, nullptr);
    UnloadAudioStream(stream);

    // --- mixed processor taps the final audio-thread mix -------------------
    g_mixedCalls.store(0);
    AttachAudioMixedProcessor(MixedProcessor);
    // Play a short tone so the engine mixes non-silence through the device.
    std::vector<float> tone(24000);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.2f * std::sin(i * 440.0 * 2 * PI / 48000);
    Wave w; w.data = tone.data(); w.frameCount = (unsigned)tone.size(); w.channels = 1; w.sampleSize = 32; w.sampleRate = 48000;
    Sound s = LoadSoundFromWave(w);
    if (s.stream) { SetSoundVolume(s, 0.0f); PlaySound(s); }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (s.stream) { StopSound(s); UnloadSound(s); }
    check(g_mixedCalls.load() > 0, "mixed processor invoked on the audio thread during playback");
    DetachAudioMixedProcessor(MixedProcessor);
    int callsAfterDetach = g_mixedCalls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(g_mixedCalls.load() == callsAfterDetach, "detached mixed processor no longer runs");

    CloseAudioDevice();
    CloseWindow();
    std::printf("%s (%d skipped)\n", failures ? "SMOKE TEST FAIL" : "SMOKE TEST PASS", skips);
    return failures ? 1 : 0;
}
