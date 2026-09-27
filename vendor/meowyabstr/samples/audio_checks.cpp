#include <meowyrender/meowyrender.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace meowyrender;
int main(int argc,char** argv){
    int failures=0;auto check=[&](bool ok,const char* message){std::printf("%s: %s\n",ok?"PASS":"FAIL",message);if(!ok)++failures;};
    std::vector<float> samples(4800);for(size_t i=0;i<samples.size();++i)samples[i]=0.2f*std::sin(i*440.0f*2*PI/48000);
    Wave wave;wave.data=samples.data();wave.frameCount=static_cast<unsigned>(samples.size());wave.channels=1;wave.sampleSize=32;wave.sampleRate=48000;
    check(ExportWave(wave,"audio-check.wav"),"WAV export");
    auto decoded=LoadWave("audio-check.wav");check(decoded.data&&decoded.frameCount==wave.frameCount&&decoded.sampleRate==48000&&decoded.channels==1,"WAV file decode");
    if(decoded.data)check(std::abs(static_cast<float*>(decoded.data)[100]-samples[100])<0.00001f,"WAV sample fidelity");UnloadWave(decoded);
    int bytes=0;auto* data=LoadFileData("audio-check.wav",&bytes);decoded=LoadWaveFromMemory(".wav",data,bytes);
    check(decoded.data&&decoded.frameCount==4800,"WAV memory decode");UnloadWave(decoded);UnloadFileData(data);
    check(!LoadWaveFromMemory("",nullptr,-1).data,"invalid audio input");
    if(argc>1&&std::strcmp(argv[1],"device")==0){
        InitAudioDevice();check(IsAudioDeviceReady(),"audio output device");
        if(IsAudioDeviceReady()){
            auto sound=LoadSoundFromWave(wave);check(sound.stream!=nullptr,"sound copies its PCM source");
            if(sound.stream){SetSoundPan(sound,0.5f);SetSoundVolume(sound,0);PlaySound(sound);StopSound(sound);UnloadSound(sound);}
            SetMasterVolume(0.5f);check(std::abs(GetMasterVolume()-0.5f)<0.01f,"master volume");CloseAudioDevice();
        }
    }
    for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"device")!=0){
        decoded=LoadWave(argv[i]);check(decoded.data&&decoded.frameCount>4000&&decoded.channels>=1&&decoded.channels<=2,"compressed file decode");UnloadWave(decoded);
        data=LoadFileData(argv[i],&bytes);decoded=LoadWaveFromMemory("",data,bytes);
        check(decoded.data&&decoded.frameCount>4000,"compressed memory decode");UnloadWave(decoded);UnloadFileData(data);
    }
    return failures?1:0;
}
