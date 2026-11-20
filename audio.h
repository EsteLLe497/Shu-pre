#pragma once

#include <xaudio2.h>
#include <vector>
#include "component.h"

class Audio : public Component
{
private:
    static IXAudio2* m_Xaudio;
    static IXAudio2MasteringVoice* m_MasteringVoice;
    static bool m_ComInitialized;

    IXAudio2SourceVoice* m_SourceVoice = nullptr;
    std::vector<BYTE> m_SoundData;

public:
    static void InitMaster();
    static void UninitMaster();

    using Component::Component;
    ~Audio() override;

    void Uninit() override;
    bool Load(const char* fileName);
    bool Load(const wchar_t* fileName);
    bool Play(bool loop = false);
    void Stop();
    void SetVolume(float volume);
};
