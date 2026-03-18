#pragma once

#include <xaudio2.h>
#include <vector>

constexpr float PI = 3.14159265359f;

struct AudioParameters
{
    float playerVelocity = 0.0f;
};

class AudioSystem
{
public:

    AudioSystem();
    ~AudioSystem();

    void Update();

    void SetPlayerVelocity(float velocity) { parameters.playerVelocity = velocity; }

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

    // Melody
    float NoteFreq(int semitoneFromA4) { return 440.0f * powf(2.0f, semitoneFromA4 / 12.0f); }
    float voice(float phase);
    float musicTime = 0.0f;
    int currentChord = 0;
    float musicPhase1 = 0.0f;
    float musicPhase2 = 0.0f;
    float musicPhase3 = 0.0f;
    float bassPhase = 0.0f;
    const float noteDuration = 3.0f;
    std::vector<std::vector<int>> chords =
    { {
        {0, 3, 7},
        {-2, 3, 7},
        {-5, 0, 4},
        {-7, -2, 3}
    } };
};