#include "AudioSystem.h"
#include <cmath>
#include <stdexcept>

AudioSystem::AudioSystem()
{
    HRESULT hr;

    hr = XAudio2Create(&xaudio, 0);

    if (FAILED(hr))
        throw std::runtime_error("Failed to create XAudio2");

    xaudio->CreateMasteringVoice(&masterVoice);

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 1;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    xaudio->CreateSourceVoice(&sourceVoice, &format);

    for (int i = 0; i < bufferCount; i++)
    {
        buffers[i].resize(bufferSamples);
    }

    sourceVoice->Start();
}

AudioSystem::~AudioSystem()
{
    if (sourceVoice) sourceVoice->DestroyVoice();
    if (masterVoice) masterVoice->DestroyVoice();
    if (xaudio) xaudio->Release();
}

void AudioSystem::Update()
{
    XAUDIO2_VOICE_STATE state;
    sourceVoice->GetState(&state);

    if (state.BuffersQueued < bufferCount)
    {
        float* bufferData = buffers[currentBuffer].data();

        GenerateSamples(bufferData, bufferSamples);

        XAUDIO2_BUFFER buffer = {};
        buffer.AudioBytes = bufferSamples * sizeof(float);
        buffer.pAudioData = (BYTE*)bufferData;

        sourceVoice->SubmitSourceBuffer(&buffer);

        currentBuffer = (currentBuffer + 1) % bufferCount;
    }
}

void AudioSystem::GenerateSamples(float* buffer, int count)
{
    float swingFreq = 0.0f;

    if (parameters.playerVelocity > 0.001f)
    {
        swingFreq = 100.0f + 100.0f * parameters.playerVelocity;
    }

    for (int i = 0; i < count; i++)
    {
        // Melody
        musicTime += 1.0f / sampleRate;

        if (musicTime > noteDuration)
        {
            musicTime -= noteDuration;
            currentChord = (currentChord + 1) % chords.size();

            musicPhase1 = 0.0f;
            musicPhase2 = 0.0f;
            musicPhase3 = 0.0f;
        }

        float f1 = NoteFreq(chords[currentChord][0]);
        float f2 = NoteFreq(chords[currentChord][1]);
        float f3 = NoteFreq(chords[currentChord][2]);

        musicPhase1 += f1 / sampleRate;
        musicPhase2 += f2 / sampleRate;
        musicPhase3 += f3 / sampleRate;

        if (musicPhase1 > 1.0f) musicPhase1 -= 1.0f;
        if (musicPhase2 > 1.0f) musicPhase2 -= 1.0f;
        if (musicPhase3 > 1.0f) musicPhase3 -= 1.0f;

        float v1 = sinf(musicPhase1 * 2.0f * PI);
        float v2 = sinf(musicPhase2 * 2.0f * PI);
        float v3 = sinf(musicPhase3 * 2.0f * PI);

        float musicSample = voice(musicPhase1) + voice(musicPhase2) + voice(musicPhase3);

        float t = musicTime / noteDuration;

        float env = 0.5f - 0.5f * cosf(t * 2.0f * PI);

        musicSample *= env;

        // Arpeggio
        arpTime += 1.0f / sampleRate;

        if (arpTime > arpSpeed)
        {
            arpTime -= arpSpeed;
            arpStep = (arpStep + 1) % arpPattern.size();
        }

        int noteIndex = arpPattern[arpStep];
        float arpFreq = NoteFreq(chords[currentChord][noteIndex]);

        arpPhase += arpFreq / sampleRate;
        if (arpPhase > 1.0f)
            arpPhase -= 1.0f;

        float s1 = sinf(arpPhase * 2.0f * PI);
        float s2 = sinf(arpPhase * 4.0f * PI);

        float tArp = arpTime / arpSpeed;
        float arpEnv = expf(-tArp * 6.0f);  // quick decay

        float arpOffset = 0.1f;

        float arpSample = (s1 + 0.3f * s2) * arpEnv * (env / 2.0f + arpOffset);

        // Bass note
        float bassFreq = NoteFreq(-12); // low A

        float bass = sinf(bassPhase * 2 * PI) * 0.2f;

        bassPhase += bassFreq / sampleRate;

        if (bassPhase > 1.0f)
            bassPhase -= 1.0f;


        // Swinging sound based on player velocity
        float swingSample = 0.0f;

        if (parameters.playerVelocity > 0.001f)
        {
            float s1 = sinf(phase * 2.0f * PI);
            float s2 = sinf(phase * 4.0f * PI);
            float s3 = sinf(phase * 6.0f * PI);

            float amp = min(0.3f, parameters.playerVelocity * 0.02f);

            swingSample = (s1 + 0.4f * s2 + 0.3f * s3) * amp;

            phase += swingFreq / sampleRate;

            if (phase > 1.0f)
                phase -= 1.0f;
        }

        buffer[i] = 0.4f * musicSample + 0.7f * arpSample + 0.7f * swingSample + 0.2f * bass;
    }
}

float AudioSystem::voice(float phase)
{
    float s1 = sinf(phase * 2.0f * PI);
    float s2 = sinf(phase * 2.01f * 2.0f * PI); // slight detune
    float s3 = sinf(phase * 0.5f * 2.0f * PI);  // lower harmonic

    return (s1 + 0.5f * s2 + 0.3f * s3) / 1.8f;
}
