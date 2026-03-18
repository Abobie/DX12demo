#pragma once

#include <xaudio2.h>
#include <vector>

struct AudioParameters
{
    float ropeLength = 0.0f;
    float playerVelocity = 0.0f;
};

class AudioSystem
{
public:

    AudioSystem();
    ~AudioSystem();

    void Update();

    void SetRopeLength(float length) { parameters.ropeLength = length; }
    void SetPlayerVelocity(float velocity) { parameters.playerVelocity = velocity; }

    void PluckString(float freq);

private:

    static const int sampleRate = 48000;
    static const int bufferSamples = 1024;
    static const int bufferCount = 3;

    IXAudio2* xaudio = nullptr;
    IXAudio2MasteringVoice* masterVoice = nullptr;
    IXAudio2SourceVoice* sourceVoice = nullptr;

    std::vector<float> buffers[bufferCount];
    int currentBuffer = 0;

    float phase = 0.0f;

    AudioParameters parameters;

    void GenerateSamples(float* buffer, int count);

    // Karplus-Strong
    std::vector<float> ksBuffer;
    int ksIndex = 0;

    bool stringActive = false;

    // Melody
    //float NoteFreq(int semitoneFromA4) { return 440.0f * powf(2.0f, semitoneFromA4 / 12.0f); }
};