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
    float freq = 0.0f;

    if (parameters.playerVelocity > 0.01f)
    {
        freq = 100.0f + 100.0f * parameters.playerVelocity;
    }

    for (int i = 0; i < count; i++)
    {
        float sample = 0.0f;

        if (freq > 0.0f)
        {
            float s1 = sinf(phase * 2.0f * 3.14159265f);
            float s2 = sinf(phase * 4.0f * 3.14159265f);
            float s3 = sinf(phase * 6.0f * 3.14159265f);

            float amp = min(0.3f, parameters.playerVelocity * 0.02f);

            sample = (s1 + 0.4f * s2 + 0.3f * s3) * amp;

            phase += freq / sampleRate;

            if (phase > 1.0f)
                phase -= 1.0f;
        }

        buffer[i] = sample;

        /*float ksSample = 0.0f;

        if (stringActive && !ksBuffer.empty())
        {
            int next = (ksIndex + 1) % ksBuffer.size();

            ksSample = ksBuffer[ksIndex];

            float damping = 0.996f;

            ksBuffer[ksIndex] = damping * 0.5f * (ksBuffer[ksIndex] + ksBuffer[next]);

            ksIndex = next;
        }

        buffer[i] = ksSample * 0.3f;*/
    }
}

void AudioSystem::PluckString(float ropeLength)
{
    if (ropeLength <= 0.0f)
        return;

    float freq = 60.0f + 600.0f / ropeLength;

    int size = max(2, (int)(sampleRate / freq));

    ksBuffer.resize(size);

    for (int i = 0; i < size; i++)
    {
        ksBuffer[i] = ((rand() / (float)RAND_MAX) * 2.0f - 1.0f);
    }

    ksIndex = 0;
    stringActive = true;
}