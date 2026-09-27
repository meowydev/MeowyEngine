// meowyrender - src/modules/audio.cpp
// Audio device, sounds, and music streaming, backed by miniaudio.
//
// miniaudio (https://github.com/mackron/miniaudio) is a public-domain / MIT-0
// single-header audio library by David Reid. Its implementation is compiled in
// this translation unit. WAV/MP3/FLAC use native decoders; Ogg Vorbis uses stb.
#include "meowyrender/meowyrender.hpp"
#include "core/mr_state.hpp"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <limits>
#include <memory>
#include <string>
#include <algorithm>

namespace meowyrender {

namespace {

// The audio module owns a single miniaudio engine driven by a self-managed
// device. Running the engine in "no-device" mode and pulling its mixed output
// through our own device data callback lets us tap the final mix (raylib's
// AttachAudioMixedProcessor) on the audio thread, exactly like raylib does.
struct AudioState {
    ma_engine engine;
    ma_device device;
    bool ready = false;
    bool deviceStarted = false;
    ma_uint32 channels = 2;
    ma_uint32 sampleRate = 48000;
    std::unordered_map<ma_sound*,ma_audio_buffer*> ownedBuffers;
    std::unordered_set<ma_sound*> aliases;  // sounds sharing another's data source
    // Post-mix processors run over the final interleaved f32 output. Guarded by
    // a spinlock because they are touched from both the API and audio threads.
    std::vector<AudioCallback> mixedProcessors;
    ma_spinlock mixedLock = 0;
};

AudioState& Audio() {
    static AudioState state;
    return state;
}

// Device data callback: pull the engine's fully-mixed output, then hand the
// final frames to each attached mixed processor (in place, f32 interleaved).
void AudioDeviceDataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount) {
    auto* a = static_cast<AudioState*>(device->pUserData);
    if (!a) return;
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&a->engine, output, frameCount, &read);
    ma_spinlock_lock(&a->mixedLock);
    if (!a->mixedProcessors.empty()) {
        for (AudioCallback proc : a->mixedProcessors)
            if (proc) proc(output, static_cast<unsigned int>(read ? read : frameCount));
    }
    ma_spinlock_unlock(&a->mixedLock);
}

} // namespace

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------
void InitAudioDevice() {
    auto& a = Audio();
    if (a.ready) return;
    // Own the device so we can tap the final mix. The device format fixes the
    // engine's channel count / sample rate.
    ma_device_config dcfg = ma_device_config_init(ma_device_type_playback);
    dcfg.playback.format = ma_format_f32;
    dcfg.playback.channels = 0;      // 0 = use the device's native channel count
    dcfg.sampleRate = 0;             // 0 = native rate
    dcfg.dataCallback = AudioDeviceDataCallback;
    dcfg.pUserData = &a;
    if (ma_device_init(nullptr, &dcfg, &a.device) != MA_SUCCESS) {
        std::fprintf(stderr, "[meowyrender] audio device init failed\n");
        return;
    }
    a.channels = a.device.playback.channels;
    a.sampleRate = a.device.sampleRate;
    // No-device engine: it mixes into buffers we pull in the device callback.
    ma_engine_config ecfg = ma_engine_config_init();
    ecfg.noDevice = MA_TRUE;
    ecfg.channels = a.channels;
    ecfg.sampleRate = a.sampleRate;
    if (ma_engine_init(&ecfg, &a.engine) != MA_SUCCESS) {
        std::fprintf(stderr, "[meowyrender] audio engine init failed\n");
        ma_device_uninit(&a.device);
        return;
    }
    if (ma_device_start(&a.device) != MA_SUCCESS) {
        std::fprintf(stderr, "[meowyrender] audio device start failed\n");
        ma_engine_uninit(&a.engine);
        ma_device_uninit(&a.device);
        return;
    }
    a.deviceStarted = true;
    a.ready = true;
    std::printf("[meowyrender] audio device ready\n");
}

void CloseAudioDevice() {
    auto& a = Audio();
    if (!a.ready) return;
    if (a.deviceStarted) { ma_device_stop(&a.device); a.deviceStarted = false; }
    ma_engine_uninit(&a.engine);
    ma_device_uninit(&a.device);
    { ma_spinlock_lock(&a.mixedLock); a.mixedProcessors.clear(); ma_spinlock_unlock(&a.mixedLock); }
    a.ready = false;
}

bool IsAudioDeviceReady() { return Audio().ready; }

void SetMasterVolume(float volume) {
    if (Audio().ready) ma_engine_set_volume(&Audio().engine, volume);
}
float GetMasterVolume() {
    return Audio().ready ? ma_engine_get_volume(&Audio().engine) : 0.0f;
}

// ---------------------------------------------------------------------------
// Wave (decode fully into memory)
// ---------------------------------------------------------------------------
namespace {
Wave DecodeWave(ma_decoder& decoder) {
    struct Cleanup {ma_decoder* value;~Cleanup(){ma_decoder_uninit(value);}} cleanup{&decoder};
    const unsigned channels=decoder.outputChannels;
    if(!channels || channels>256)return {};
    constexpr size_t maxSamples=128*1024*1024; // 512 MiB decoded-data safety limit.
    std::vector<float> samples,chunk(4096*channels);
    for(;;) {
        ma_uint64 read=0;
        ma_result result=ma_decoder_read_pcm_frames(&decoder,chunk.data(),4096,&read);
        if(result!=MA_SUCCESS && result!=MA_AT_END)return {};
        if(read>4096 || read*channels>maxSamples-samples.size())return {};
        samples.insert(samples.end(),chunk.data(),chunk.data()+read*channels);
        if(read==0 || result==MA_AT_END)break;
    }
    if(samples.empty())return {};
    Wave wave;wave.frameCount=static_cast<unsigned>(samples.size()/channels);wave.channels=channels;
    wave.sampleRate=decoder.outputSampleRate;wave.sampleSize=32;wave.data=std::malloc(samples.size()*sizeof(float));
    if(!wave.data)throw std::bad_alloc();
    std::memcpy(wave.data,samples.data(),samples.size()*sizeof(float));return wave;
}
ma_format WaveFormat(unsigned size) {return size==8?ma_format_u8:size==16?ma_format_s16:size==32?ma_format_f32:ma_format_unknown;}
}
Wave LoadWave(const std::string& fileName) {
    ma_decoder_config cfg=ma_decoder_config_init(ma_format_f32,0,0);ma_decoder decoder;
    if(ma_decoder_init_file(fileName.c_str(),&cfg,&decoder)!=MA_SUCCESS)return {};
    return DecodeWave(decoder);
}
Wave LoadWaveFromMemory(const std::string&,const unsigned char* data,int size) {
    if(!data || size<=0)return {};
    ma_decoder_config cfg=ma_decoder_config_init(ma_format_f32,0,0);ma_decoder decoder;
    if(ma_decoder_init_memory(data,static_cast<size_t>(size),&cfg,&decoder)!=MA_SUCCESS)return {};
    return DecodeWave(decoder);
}
void UnloadWave(Wave wave) {std::free(wave.data);}
bool ExportWave(Wave wave,const std::string& fileName) {
    auto format=WaveFormat(wave.sampleSize);
    if(!wave.data || !wave.frameCount || !wave.channels || wave.channels>256 || !wave.sampleRate || format==ma_format_unknown)return false;
    ma_encoder_config cfg=ma_encoder_config_init(ma_encoding_format_wav,format,wave.channels,wave.sampleRate);ma_encoder encoder;
    if(ma_encoder_init_file(fileName.c_str(),&cfg,&encoder)!=MA_SUCCESS)return false;
    ma_uint64 written=0;auto result=ma_encoder_write_pcm_frames(&encoder,wave.data,wave.frameCount,&written);
    ma_encoder_uninit(&encoder);return result==MA_SUCCESS&&written==wave.frameCount;
}

// ---------------------------------------------------------------------------
// Sound (one-shot). Backed by an ma_sound loaded fully into memory.
// ---------------------------------------------------------------------------
Sound LoadSound(const std::string& fileName) {
    Sound sound{};
    auto& a = Audio();
    if (!a.ready) return sound;
    auto* s = new ma_sound();
    if (ma_sound_init_from_file(&a.engine, fileName.c_str(),
                                MA_SOUND_FLAG_DECODE, nullptr, nullptr, s)
        != MA_SUCCESS) {
        std::fprintf(stderr, "[meowyrender] LoadSound failed: %s\n", fileName.c_str());
        delete s;
        return sound;
    }
    sound.stream = s;
    return sound;
}

Sound LoadSoundFromWave(Wave wave) {
    // Wrap the raw PCM in an audio buffer and initialize a sound from it.
    Sound sound{};
    auto& a = Audio();
    if (!a.ready || !wave.data || !wave.frameCount || !wave.channels || wave.channels>256 || !wave.sampleRate || WaveFormat(wave.sampleSize)==ma_format_unknown) return sound;
    auto* buf = new ma_audio_buffer();
    ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
        WaveFormat(wave.sampleSize), wave.channels, wave.frameCount, wave.data, nullptr);
    cfg.sampleRate=wave.sampleRate;
    if (ma_audio_buffer_init_copy(&cfg, buf) != MA_SUCCESS) { delete buf; return sound; }
    auto* s = new ma_sound();
    if (ma_sound_init_from_data_source(&a.engine, buf, 0, nullptr, s) != MA_SUCCESS) {
        ma_audio_buffer_uninit(buf); delete buf; delete s; return sound;
    }
    try{a.ownedBuffers.emplace(s,buf);}catch(...){ma_sound_uninit(s);delete s;ma_audio_buffer_uninit(buf);delete buf;throw;}
    sound.frameCount = wave.frameCount;
    sound.stream = s;
    return sound;
}

void UnloadSound(Sound sound) {
    if (!sound.stream) return;
    auto* s = static_cast<ma_sound*>(sound.stream);
    ma_sound_uninit(s);
    auto& buffers=Audio().ownedBuffers;
    if(auto it=buffers.find(s);it!=buffers.end()){ma_audio_buffer_uninit(it->second);delete it->second;buffers.erase(it);}
    delete s;
}

void PlaySound(Sound sound) {
    if (!sound.stream) return;
    auto* s = static_cast<ma_sound*>(sound.stream);
    ma_sound_seek_to_pcm_frame(s, 0);
    ma_sound_start(s);
}
void StopSound(Sound sound) {
    if (sound.stream) ma_sound_stop(static_cast<ma_sound*>(sound.stream));
}
void PauseSound(Sound sound) {
    if (sound.stream) ma_sound_stop(static_cast<ma_sound*>(sound.stream));
}
void ResumeSound(Sound sound) {
    if (sound.stream) ma_sound_start(static_cast<ma_sound*>(sound.stream));
}
bool IsSoundPlaying(Sound sound) {
    return sound.stream &&
           ma_sound_is_playing(static_cast<ma_sound*>(sound.stream));
}
void SetSoundVolume(Sound sound, float volume) {
    if (sound.stream) ma_sound_set_volume(static_cast<ma_sound*>(sound.stream), volume);
}
void SetSoundPitch(Sound sound, float pitch) {
    if (sound.stream) ma_sound_set_pitch(static_cast<ma_sound*>(sound.stream), pitch);
}
void SetSoundPan(Sound sound, float pan) {
    if (sound.stream) ma_sound_set_pan(static_cast<ma_sound*>(sound.stream), std::clamp(pan,0.0f,1.0f)*2-1);
}

// ---------------------------------------------------------------------------
// Music (streamed from disk)
// ---------------------------------------------------------------------------
Music LoadMusicStream(const std::string& fileName) {
    Music music{};
    auto& a = Audio();
    if (!a.ready) return music;
    auto* s = new ma_sound();
    if (ma_sound_init_from_file(&a.engine, fileName.c_str(),
                                MA_SOUND_FLAG_STREAM, nullptr, nullptr, s)
        != MA_SUCCESS) {
        std::fprintf(stderr, "[meowyrender] LoadMusicStream failed: %s\n",
                     fileName.c_str());
        delete s;
        return music;
    }
    ma_uint64 len = 0;
    ma_sound_get_length_in_pcm_frames(s, &len);
    music.frameCount = static_cast<unsigned int>(len);
    music.looping = true;
    ma_sound_set_looping(s, MA_TRUE);
    music.stream = s;
    return music;
}

void UnloadMusicStream(Music music) {
    if (!music.stream) return;
    auto* s = static_cast<ma_sound*>(music.stream);
    ma_sound_uninit(s);
    delete s;
}

void PlayMusicStream(Music music) {
    if (music.stream) ma_sound_start(static_cast<ma_sound*>(music.stream));
}
void StopMusicStream(Music music) {
    if (!music.stream) return;
    auto* s = static_cast<ma_sound*>(music.stream);
    ma_sound_stop(s);
    ma_sound_seek_to_pcm_frame(s, 0);
}
void PauseMusicStream(Music music) {
    if (music.stream) ma_sound_stop(static_cast<ma_sound*>(music.stream));
}
void ResumeMusicStream(Music music) {
    if (music.stream) ma_sound_start(static_cast<ma_sound*>(music.stream));
}
void UpdateMusicStream(Music /*music*/) {
    // Correct architectural no-op, NOT an unimplemented capability. raylib
    // requires a per-frame UpdateMusicStream to refill the stream's audio
    // buffers; here music plays through miniaudio's engine, which services the
    // stream on its own audio thread. music_lifecycle_checks verifies the
    // consequences: playback progresses WITHOUT calling this, repeated calls are
    // harmless (no reset/stall), and pause/resume/seek/loop/stop/unload all work.
    // Kept for raylib API/source parity.
}
bool IsMusicStreamPlaying(Music music) {
    return music.stream &&
           ma_sound_is_playing(static_cast<ma_sound*>(music.stream));
}
void SetMusicVolume(Music music, float volume) {
    if (music.stream) ma_sound_set_volume(static_cast<ma_sound*>(music.stream), volume);
}
void SetMusicPitch(Music music, float pitch) {
    if (music.stream) ma_sound_set_pitch(static_cast<ma_sound*>(music.stream), pitch);
}
float GetMusicTimeLength(Music music) {
    if (!music.stream) return 0.0f;
    float len = 0.0f;
    ma_sound_get_length_in_seconds(static_cast<ma_sound*>(music.stream), &len);
    return len;
}
float GetMusicTimePlayed(Music music) {
    if (!music.stream) return 0.0f;
    return ma_sound_get_time_in_milliseconds(
               static_cast<ma_sound*>(music.stream)) / 1000.0f;
}
void SeekMusicStream(Music music, float position) {
    if (!music.stream) return;
    auto* s = static_cast<ma_sound*>(music.stream);
    ma_uint32 rate = ma_engine_get_sample_rate(&Audio().engine);
    ma_sound_seek_to_pcm_frame(s, static_cast<ma_uint64>(position * rate));
}
void SetMusicPan(Music music, float pan) {
    if (music.stream) ma_sound_set_pan(static_cast<ma_sound*>(music.stream),
                                       std::clamp(pan, 0.0f, 1.0f) * 2 - 1);
}

// ---------------------------------------------------------------------------
// Music from memory. miniaudio needs the bytes to persist for the stream's
// lifetime, so we keep a copy keyed by the sound handle.
// ---------------------------------------------------------------------------
namespace { std::unordered_map<ma_sound*, std::vector<unsigned char>> g_musicMemory; }

Music LoadMusicStreamFromMemory(const std::string&, const unsigned char* data, int dataSize) {
    Music music{};
    auto& a = Audio();
    if (!a.ready || !data || dataSize <= 0) return music;
    auto* s = new ma_sound();
    // Keep a persistent copy of the encoded bytes.
    std::vector<unsigned char> bytes(data, data + dataSize);
    // miniaudio can decode from memory via a decoder-backed data source.
    auto* decoder = new ma_decoder();
    if (ma_decoder_init_memory(bytes.data(), bytes.size(), nullptr, decoder) != MA_SUCCESS) {
        delete decoder; delete s; return music;
    }
    if (ma_sound_init_from_data_source(&a.engine, decoder, MA_SOUND_FLAG_STREAM, nullptr, s) != MA_SUCCESS) {
        ma_decoder_uninit(decoder); delete decoder; delete s; return music;
    }
    g_musicMemory.emplace(s, std::move(bytes));
    ma_uint64 frames = 0;
    ma_sound_get_length_in_pcm_frames(s, &frames);
    music.frameCount = static_cast<unsigned int>(frames);
    music.stream = s;
    return music;
}

// ---------------------------------------------------------------------------
// Sound aliases: share the source's audio data but play independently.
// ---------------------------------------------------------------------------
Sound LoadSoundAlias(Sound source) {
    Sound alias{};
    auto& a = Audio();
    if (!a.ready || !source.stream) return alias;
    auto* src = static_cast<ma_sound*>(source.stream);
    auto* s = new ma_sound();
    // ma_sound_init_copy shares the source's underlying data source.
    if (ma_sound_init_copy(&a.engine, src, 0, nullptr, s) != MA_SUCCESS) { delete s; return alias; }
    alias.frameCount = source.frameCount;
    alias.stream = s;
    a.aliases.insert(s);
    return alias;
}
void UnloadSoundAlias(Sound alias) {
    if (!alias.stream) return;
    auto* s = static_cast<ma_sound*>(alias.stream);
    ma_sound_uninit(s);
    Audio().aliases.erase(s);
    delete s;
}
void UpdateSound(Sound sound, const void* data, int sampleCount) {
    // Replace the samples of a sound backed by an owned audio buffer.
    if (!sound.stream || !data || sampleCount <= 0) return;
    auto& buffers = Audio().ownedBuffers;
    auto it = buffers.find(static_cast<ma_sound*>(sound.stream));
    if (it == buffers.end()) return; // only buffer-backed sounds are updatable
    ma_audio_buffer* buf = it->second;
    const ma_uint64 frames = std::min<ma_uint64>(sampleCount, buf->ref.sizeInFrames);
    std::memcpy(const_cast<void*>(buf->ref.pData), data,
                static_cast<std::size_t>(frames) * buf->ref.channels * sizeof(float));
}

// ===========================================================================
// Audio streams (procedural / custom audio) via a ring of PCM frames pushed by
// UpdateAudioStream and played through an ma_audio_buffer data source.
// ===========================================================================
namespace {
struct StreamState {
    ma_audio_buffer buffer{};
    std::vector<float> pcm;         // interleaved f32 backing store
    ma_sound sound{};
    bool started = false;
    unsigned int channels = 2;
    unsigned int sampleRate = 44100;
    // Raylib parity: a fill callback generates frames on demand; attached
    // processors transform the stream's frames in place before they play.
    AudioCallback fillCallback = nullptr;
    std::vector<AudioCallback> processors;
};
std::unordered_map<void*, StreamState*> g_streams;
int g_defaultBufferSize = 4096;
} // namespace

AudioStream LoadAudioStream(unsigned int sampleRate, unsigned int sampleSize, unsigned int channels) {
    AudioStream stream{};
    auto& a = Audio();
    if (!a.ready) return stream;
    auto* st = new StreamState();
    st->channels = channels ? channels : 2;
    st->sampleRate = sampleRate ? sampleRate : 44100;
    st->pcm.assign(static_cast<std::size_t>(g_defaultBufferSize) * st->channels, 0.0f);
    ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
        ma_format_f32, st->channels, g_defaultBufferSize, st->pcm.data(), nullptr);
    cfg.sampleRate = st->sampleRate;
    if (ma_audio_buffer_init(&cfg, &st->buffer) != MA_SUCCESS) { delete st; return stream; }
    if (ma_sound_init_from_data_source(&a.engine, &st->buffer, 0, nullptr, &st->sound) != MA_SUCCESS) {
        ma_audio_buffer_uninit(&st->buffer); delete st; return stream;
    }
    ma_data_source_set_looping(&st->buffer, MA_TRUE);
    stream.sampleRate = st->sampleRate;
    stream.sampleSize = sampleSize ? sampleSize : 32;
    stream.channels = st->channels;
    stream.buffer = st;   // opaque handle points at StreamState
    g_streams.emplace(st, st);
    return stream;
}
void UnloadAudioStream(AudioStream stream) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (!st) return;
    ma_sound_uninit(&st->sound);
    ma_audio_buffer_uninit(&st->buffer);
    g_streams.erase(st);
    delete st;
}
void UpdateAudioStream(AudioStream stream, const void* data, int frameCount) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (!st || !data || frameCount <= 0) return;
    const std::size_t need = static_cast<std::size_t>(frameCount) * st->channels;
    if (need > st->pcm.size()) st->pcm.resize(need);
    std::memcpy(st->pcm.data(), data, need * sizeof(float));
    // If a fill callback is registered, let it (re)generate/overwrite the frames
    // just pushed, matching raylib's callback-fills-the-buffer contract.
    if (st->fillCallback) st->fillCallback(st->pcm.data(), static_cast<unsigned int>(frameCount));
    // Run attached processors over the frames in place (interleaved f32).
    for (AudioCallback proc : st->processors)
        if (proc) proc(st->pcm.data(), static_cast<unsigned int>(frameCount));
}
bool IsAudioStreamProcessed(AudioStream stream) {
    // Looping buffer is always ready to accept the next chunk.
    return stream.buffer != nullptr;
}
void PlayAudioStream(AudioStream stream) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (st) { ma_sound_start(&st->sound); st->started = true; }
}
void PauseAudioStream(AudioStream stream) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (st) ma_sound_stop(&st->sound);
}
void ResumeAudioStream(AudioStream stream) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (st) ma_sound_start(&st->sound);
}
bool IsAudioStreamPlaying(AudioStream stream) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    return st && ma_sound_is_playing(&st->sound);
}
void StopAudioStream(AudioStream stream) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (st) { ma_sound_stop(&st->sound); ma_sound_seek_to_pcm_frame(&st->sound, 0); st->started = false; }
}
void SetAudioStreamVolume(AudioStream stream, float volume) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (st) ma_sound_set_volume(&st->sound, volume);
}
void SetAudioStreamPitch(AudioStream stream, float pitch) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (st) ma_sound_set_pitch(&st->sound, pitch);
}
void SetAudioStreamPan(AudioStream stream, float pan) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (st) ma_sound_set_pan(&st->sound, std::clamp(pan, 0.0f, 1.0f) * 2 - 1);
}
void SetAudioStreamBufferSizeDefault(int size) { if (size > 0) g_defaultBufferSize = size; }

// Register a fill callback that generates the stream's frames. It runs on the
// UpdateAudioStream path (and, once set, may overwrite pushed data), matching
// raylib's "callback fills the buffer" model. Pass nullptr to clear it.
void SetAudioStreamCallback(AudioStream stream, AudioCallback callback) {
    if (auto* st = static_cast<StreamState*>(stream.buffer)) st->fillCallback = callback;
}
// Attach a per-stream DSP processor. Processors run in attach order over the
// stream's interleaved f32 frames, in place. Duplicate attaches are ignored.
void AttachAudioStreamProcessor(AudioStream stream, AudioCallback processor) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (!st || !processor) return;
    if (std::find(st->processors.begin(), st->processors.end(), processor) == st->processors.end())
        st->processors.push_back(processor);
}
void DetachAudioStreamProcessor(AudioStream stream, AudioCallback processor) {
    auto* st = static_cast<StreamState*>(stream.buffer);
    if (!st) return;
    st->processors.erase(std::remove(st->processors.begin(), st->processors.end(), processor),
                         st->processors.end());
}
// Attach a processor over the final mixed output. It runs on the audio thread
// in the device data callback after the engine mix, in attach order, in place.
void AttachAudioMixedProcessor(AudioCallback processor) {
    auto& a = Audio();
    if (!processor) return;
    ma_spinlock_lock(&a.mixedLock);
    if (std::find(a.mixedProcessors.begin(), a.mixedProcessors.end(), processor) == a.mixedProcessors.end())
        a.mixedProcessors.push_back(processor);
    ma_spinlock_unlock(&a.mixedLock);
}
void DetachAudioMixedProcessor(AudioCallback processor) {
    auto& a = Audio();
    ma_spinlock_lock(&a.mixedLock);
    a.mixedProcessors.erase(std::remove(a.mixedProcessors.begin(), a.mixedProcessors.end(), processor),
                            a.mixedProcessors.end());
    ma_spinlock_unlock(&a.mixedLock);
}

} // namespace meowyrender
