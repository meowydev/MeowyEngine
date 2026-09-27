// meowyrender - src/modules/raudio_util.cpp
// raudio parity helpers that operate on CPU-side Wave data or plain handle
// validity, with no miniaudio dependency. Stream/device-bound functions live
// in audio.cpp where the miniaudio engine is owned.
#include "meowyrender/meowyrender.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

namespace meowyrender {

// ===========================================================================
// Validity checks
// ===========================================================================
bool IsWaveValid(Wave wave) {
    return wave.data != nullptr && wave.frameCount > 0 && wave.sampleRate > 0 &&
           wave.sampleSize > 0 && wave.channels > 0;
}
bool IsSoundValid(Sound sound) { return sound.stream != nullptr && sound.frameCount > 0; }
bool IsMusicValid(Music music) { return music.stream != nullptr; }
bool IsAudioStreamValid(AudioStream stream) {
    return stream.buffer != nullptr && stream.sampleRate > 0 && stream.channels > 0;
}

// ===========================================================================
// Wave utilities (all operate on the CPU-side sample buffer)
// ===========================================================================
namespace {
std::size_t WaveByteSize(const Wave& w) {
    return static_cast<std::size_t>(w.frameCount) * w.channels * (w.sampleSize / 8);
}
} // namespace

Wave WaveCopy(Wave wave) {
    Wave out{};
    if (!IsWaveValid(wave)) return out;
    out = wave;
    const std::size_t bytes = WaveByteSize(wave);
    out.data = std::malloc(bytes);
    std::memcpy(out.data, wave.data, bytes);
    return out;
}
void WaveCrop(Wave* wave, int initFrame, int finalFrame) {
    if (!wave || !IsWaveValid(*wave)) return;
    if (initFrame < 0) initFrame = 0;
    if (finalFrame > (int)wave->frameCount) finalFrame = wave->frameCount;
    if (finalFrame <= initFrame) return;
    const int frames = finalFrame - initFrame;
    const int frameBytes = wave->channels * (wave->sampleSize / 8);
    auto* out = static_cast<unsigned char*>(std::malloc(static_cast<std::size_t>(frames) * frameBytes));
    std::memcpy(out, static_cast<unsigned char*>(wave->data) + static_cast<std::size_t>(initFrame) * frameBytes,
                static_cast<std::size_t>(frames) * frameBytes);
    std::free(wave->data);
    wave->data = out;
    wave->frameCount = static_cast<unsigned int>(frames);
}
void WaveFormat(Wave* wave, int sampleRate, int sampleSize, int channels) {
    if (!wave || !IsWaveValid(*wave)) return;
    // Decode current samples to float, then re-encode to the target format.
    // Resampling is linear; channel changes duplicate/average as needed.
    const int srcChannels = wave->channels;
    const int srcBits = wave->sampleSize;
    const unsigned srcFrames = wave->frameCount;
    auto readSample = [&](std::size_t idx) -> float {
        const auto* d = static_cast<const unsigned char*>(wave->data);
        if (srcBits == 8)  return d[idx] / 127.5f - 1.0f;
        if (srcBits == 16) { short v; std::memcpy(&v, d + idx * 2, 2); return v / 32768.0f; }
        float v; std::memcpy(&v, d + idx * 4, 4); return v;
    };
    // Build float frames in source channel count.
    std::vector<float> mono(srcFrames * srcChannels);
    for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = readSample(i);

    const int dstChannels = channels > 0 ? channels : srcChannels;
    const int dstRate = sampleRate > 0 ? sampleRate : (int)wave->sampleRate;
    const double ratio = static_cast<double>(dstRate) / wave->sampleRate;
    const unsigned dstFrames = static_cast<unsigned>(srcFrames * ratio);
    std::vector<float> resampled(static_cast<std::size_t>(dstFrames) * dstChannels);
    for (unsigned f = 0; f < dstFrames; ++f) {
        const double srcPos = f / ratio;
        const unsigned s0 = static_cast<unsigned>(srcPos);
        const unsigned s1 = std::min(s0 + 1, srcFrames - 1);
        const float t = static_cast<float>(srcPos - s0);
        for (int c = 0; c < dstChannels; ++c) {
            const int sc = c < srcChannels ? c : srcChannels - 1;
            const float a = mono[static_cast<std::size_t>(s0) * srcChannels + sc];
            const float b = mono[static_cast<std::size_t>(s1) * srcChannels + sc];
            resampled[static_cast<std::size_t>(f) * dstChannels + c] = a + (b - a) * t;
        }
    }
    // Encode to target sampleSize.
    const int dstBits = sampleSize > 0 ? sampleSize : (int)wave->sampleSize;
    const std::size_t dstBytes = static_cast<std::size_t>(dstFrames) * dstChannels * (dstBits / 8);
    auto* out = static_cast<unsigned char*>(std::malloc(dstBytes));
    for (std::size_t i = 0; i < resampled.size(); ++i) {
        float v = resampled[i];
        if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
        if (dstBits == 8)  out[i] = static_cast<unsigned char>((v + 1.0f) * 127.5f);
        else if (dstBits == 16) { short s = static_cast<short>(v * 32767.0f); std::memcpy(out + i * 2, &s, 2); }
        else { std::memcpy(out + i * 4, &v, 4); }
    }
    std::free(wave->data);
    wave->data = out;
    wave->frameCount = dstFrames;
    wave->sampleRate = dstRate;
    wave->sampleSize = dstBits;
    wave->channels = dstChannels;
}
float* LoadWaveSamples(Wave wave) {
    if (!IsWaveValid(wave)) return nullptr;
    const std::size_t count = static_cast<std::size_t>(wave.frameCount) * wave.channels;
    auto* out = static_cast<float*>(std::malloc(count * sizeof(float)));
    const auto* d = static_cast<const unsigned char*>(wave.data);
    for (std::size_t i = 0; i < count; ++i) {
        if (wave.sampleSize == 8) out[i] = d[i] / 127.5f - 1.0f;
        else if (wave.sampleSize == 16) { short v; std::memcpy(&v, d + i * 2, 2); out[i] = v / 32768.0f; }
        else { std::memcpy(&out[i], d + i * 4, 4); }
    }
    return out;
}
void UnloadWaveSamples(float* samples) { std::free(samples); }
bool ExportWaveAsCode(Wave wave, const std::string& fileName) {
    if (!IsWaveValid(wave)) return false;
    std::ofstream f(fileName);
    if (!f) return false;
    const std::size_t bytes = WaveByteSize(wave);
    f << "// MeowyRender ExportWaveAsCode\n";
    f << "#define WAVE_FRAME_COUNT " << wave.frameCount << "\n";
    f << "#define WAVE_SAMPLE_RATE " << wave.sampleRate << "\n";
    f << "#define WAVE_SAMPLE_SIZE " << wave.sampleSize << "\n";
    f << "#define WAVE_CHANNELS " << wave.channels << "\n\n";
    f << "static const unsigned char WAVE_DATA[" << bytes << "] = {";
    const auto* d = static_cast<const unsigned char*>(wave.data);
    for (std::size_t i = 0; i < bytes; ++i) {
        if (i % 20 == 0) f << "\n    ";
        f << (int)d[i];
        if (i + 1 < bytes) f << ",";
    }
    f << "\n};\n";
    return f.good();
}

} // namespace meowyrender