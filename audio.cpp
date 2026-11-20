#include "main.h"
#include "audio.h"

IXAudio2* Audio::m_Xaudio = nullptr;
IXAudio2MasteringVoice* Audio::m_MasteringVoice = nullptr;
bool Audio::m_ComInitialized = false;

void Audio::InitMaster()
{
    m_ComInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    if (SUCCEEDED(XAudio2Create(&m_Xaudio, 0)))
        m_Xaudio->CreateMasteringVoice(&m_MasteringVoice);
}

void Audio::UninitMaster()
{
    if (m_MasteringVoice) m_MasteringVoice->DestroyVoice();
    if (m_Xaudio) m_Xaudio->Release();
    m_MasteringVoice = nullptr;
    m_Xaudio = nullptr;
    if (m_ComInitialized) CoUninitialize();
    m_ComInitialized = false;
}

Audio::~Audio()
{
    Uninit();
}

bool Audio::Load(const char* fileName)
{
    const int count = MultiByteToWideChar(CP_ACP, 0, fileName, -1, nullptr, 0);
    if (count <= 0) return false;
    std::wstring path(count, L'\0');
    if (!MultiByteToWideChar(CP_ACP, 0, fileName, -1, &path[0], count)) return false;
    return Load(path.c_str());
}

bool Audio::Load(const wchar_t* fileName)
{
    if (!m_Xaudio || !m_MasteringVoice) return false;
    std::wstring path(fileName);
    HMMIO file = mmioOpenW(&path[0], nullptr, MMIO_READ | MMIO_ALLOCBUF);
    if (!file) return false;

    WAVEFORMATEXTENSIBLE format{};
    std::vector<BYTE> data;
    const bool read = [&]()
    {
        MMCKINFO riff{};
        riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
        if (mmioDescend(file, &riff, nullptr, MMIO_FINDRIFF) != MMSYSERR_NOERROR) return false;

        MMCKINFO fmt{};
        fmt.ckid = mmioFOURCC('f', 'm', 't', ' ');
        if (mmioDescend(file, &fmt, &riff, MMIO_FINDCHUNK) != MMSYSERR_NOERROR
            || fmt.cksize < 16 || fmt.cksize == 17) return false;
        const LONG bytes = static_cast<LONG>(std::min<DWORD>(fmt.cksize, sizeof(format)));
        if (mmioRead(file, reinterpret_cast<char*>(&format), bytes) != bytes) return false;
        auto& wave = format.Format;
        if (wave.wFormatTag == WAVE_FORMAT_PCM || wave.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            wave.cbSize = 0;
        else if (wave.wFormatTag != WAVE_FORMAT_EXTENSIBLE || bytes < sizeof(format) || wave.cbSize != 22)
            return false;
        if (wave.nChannels == 0 || wave.nBlockAlign == 0 || wave.nSamplesPerSec == 0
            || mmioAscend(file, &fmt, 0) != MMSYSERR_NOERROR) return false;

        // Search from the start of the WAVE contents, including files with data before fmt.
        if (mmioSeek(file, riff.dwDataOffset + sizeof(FOURCC), SEEK_SET) < 0) return false;
        MMCKINFO chunk{};
        chunk.ckid = mmioFOURCC('d', 'a', 't', 'a');
        constexpr DWORD limit = 128 * 1024 * 1024;
        if (mmioDescend(file, &chunk, &riff, MMIO_FINDCHUNK) != MMSYSERR_NOERROR
            || chunk.cksize == 0 || chunk.cksize > limit || chunk.cksize % wave.nBlockAlign) return false;
        data.resize(chunk.cksize);
        return mmioRead(file, reinterpret_cast<char*>(data.data()), chunk.cksize) == static_cast<LONG>(chunk.cksize);
    }();
    mmioClose(file, 0);
    if (!read) return false;

    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(m_Xaudio->CreateSourceVoice(&voice, &format.Format))) return false;
    Uninit();
    m_SourceVoice = voice;
    m_SoundData.swap(data);
    return true;
}

void Audio::Uninit()
{
    if (m_SourceVoice)
    {
        m_SourceVoice->DestroyVoice();
        m_SourceVoice = nullptr;
    }
    std::vector<BYTE>().swap(m_SoundData);
}

bool Audio::Play(bool loop)
{
    if (!m_SourceVoice) return false;
    Stop();
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(m_SoundData.size());
    buffer.pAudioData = m_SoundData.data();
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
    return SUCCEEDED(m_SourceVoice->SubmitSourceBuffer(&buffer)) && SUCCEEDED(m_SourceVoice->Start());
}

void Audio::Stop()
{
    if (!m_SourceVoice) return;
    m_SourceVoice->Stop();
    m_SourceVoice->FlushSourceBuffers();
}

void Audio::SetVolume(float volume)
{
    if (m_SourceVoice) m_SourceVoice->SetVolume(volume);
}
