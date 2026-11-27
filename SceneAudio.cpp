#include "main.h"
#include "audio.h"
#include "SceneAudio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <unordered_map>

using SceneAudio::Cue;
using SceneAudio::Setting;

namespace
{
    namespace fs = std::filesystem;
    constexpr size_t count = static_cast<size_t>(Cue::Count);
    constexpr char config[] = "asset/scene-audio.txt";
    constexpr char temporary[] = "asset/scene-audio.txt.tmp";
    const char* const cueNames[count] = { "BGM", "Jump", "Land", "Punch", "Slam", "Charge" };

    struct Track
    {
        Setting setting;
        std::unique_ptr<Audio> sound;
    };

    std::array<Track, count> tracks;
    std::unique_ptr<Audio> audition;
    Cue auditionCue = Cue::Count;
    bool editing = false;
    std::unordered_map<std::string, std::unique_ptr<Audio>> markerSounds;

    struct AmbientLoop
    {
        std::unique_ptr<Audio> sound;
        float gain = 0.0f;
        float target = 0.0f;
        bool playing = false;
    };
    std::unordered_map<std::string, AmbientLoop> ambientLoops;
    std::unique_ptr<Audio> ambientPreview;

    Track& track(Cue cue) { return tracks[static_cast<size_t>(cue)]; }

    std::unique_ptr<Audio> loadWav(const std::string& text, std::string& normalized)
    {
        try
        {
            std::error_code error;
            const auto path = fs::u8path(text);
            auto extension = path.extension().wstring();
            for (auto& c : extension) c = static_cast<wchar_t>(towlower(c));
            if (extension != L".wav" || !fs::is_regular_file(path, error) || error) return {};
            const auto relative = fs::relative(path, "asset/Audio", error);
            if (error || relative.empty() || relative.is_absolute() || *relative.begin() == L"..") return {};
            auto sound = std::make_unique<Audio>(nullptr);
            if (!sound->Load(path.c_str())) return {};
            normalized = (fs::path("asset/Audio") / relative).generic_u8string();
            return sound;
        }
        catch (const std::system_error&)
        {
            return {};
        }
    }

    bool loadSettings(std::array<Setting, count>& settings)
    {
        std::ifstream input(config);
        std::string magic;
        int version = 0;
        if (!(input >> magic >> version) || magic != "SceneAudio" || version != 1) return false;
        for (size_t i = 0; i < count; ++i)
        {
            int enabled = 0;
            std::string name;
            auto& item = settings[i];
            if (!(input >> name >> enabled >> item.volume >> std::quoted(item.file))
                || name != cueNames[i] || (enabled != 0 && enabled != 1)
                || !std::isfinite(item.volume) || item.volume < 0 || item.volume > 1) return false;
            item.enabled = enabled != 0;
        }
        return true;
    }

    void startBgm()
    {
        auto& bgm = track(Cue::Bgm);
        if (!bgm.sound) return;
        if (bgm.setting.enabled && !editing) bgm.sound->Play(true);
        else bgm.sound->Stop();
    }
}

void SceneAudio::init()
{
    shutdown();
    std::array<Setting, count> settings;
    if (!loadSettings(settings))
    {
        settings = {};
        settings[0] = { "asset/Audio/bgm.wav", .55f, false };
        settings[1] = { "asset/Audio/wan.wav", .8f, false };
    }
    for (size_t i = 0; i < count; ++i)
    {
        auto& item = tracks[i];
        item.setting = std::move(settings[i]);
        if (!item.setting.file.empty())
        {
            item.sound = loadWav(item.setting.file, item.setting.file);
            if (item.sound) item.sound->SetVolume(item.setting.volume);
        }
    }
    startBgm();
}

void SceneAudio::shutdown()
{
    stopPreview();
    stopAmbientPreview();
    ambientLoops.clear();
    for (auto& item : tracks) item = {};
    markerSounds.clear();
    editing = false;
}

void SceneAudio::setEditing(bool value)
{
    if (editing == value) return;
    editing = value;
    stopPreview();
    stopAmbientPreview();
    ambientLoops.clear();
    stopMarkers();
    if (editing)
        for (auto& item : tracks)
            if (item.sound) item.sound->Stop();
    startBgm();
}

const Setting& SceneAudio::setting(Cue cue) { return track(cue).setting; }

bool SceneAudio::assign(Cue cue, const std::string& file)
{
    auto& item = track(cue);
    std::string normalized;
    auto sound = file.empty() ? nullptr : loadWav(file, normalized);
    if (!file.empty() && !sound) return false;

    if (auditionCue == cue) stopPreview();
    item.sound = std::move(sound);
    item.setting.file = std::move(normalized);
    if (item.sound) item.sound->SetVolume(item.setting.volume);
    else item.setting.enabled = false;
    if (cue == Cue::Bgm) startBgm();
    return true;
}

void SceneAudio::setVolume(Cue cue, float volume)
{
    auto& item = track(cue);
    item.setting.volume = std::max(0.0f, std::min(1.0f, volume));
    if (item.sound) item.sound->SetVolume(item.setting.volume);
    if (auditionCue == cue && audition) audition->SetVolume(item.setting.volume);
}

void SceneAudio::setEnabled(Cue cue, bool enabled)
{
    auto& item = track(cue);
    item.setting.enabled = enabled;
    if (cue == Cue::Bgm) startBgm();
    else if (!enabled && item.sound) item.sound->Stop();
}

void SceneAudio::play(Cue cue)
{
    auto& item = track(cue);
    if (!editing && item.setting.enabled && item.sound) item.sound->Play();
}

bool SceneAudio::preview(Cue cue)
{
    stopPreview();
    auto& item = track(cue);
    if (item.setting.file.empty()) return false;
    std::string normalized;
    auto voice = loadWav(item.setting.file, normalized);
    if (!voice) return false;
    voice->SetVolume(item.setting.volume);
    if (!voice->Play(cue == Cue::Bgm)) return false;
    audition = std::move(voice);
    auditionCue = cue;
    return true;
}

void SceneAudio::stopPreview()
{
    audition.reset();
    auditionCue = Cue::Count;
}

bool SceneAudio::save()
{
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "SceneAudio 1\n" << std::setprecision(9);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& item = tracks[i].setting;
        output << cueNames[i] << ' ' << item.enabled << ' ' << item.volume << ' ' << std::quoted(item.file) << '\n';
    }
    output.close();
    return output && MoveFileExA(temporary, config, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

float SceneAudio::AmbientZone::gainAt(float listenerX, float listenerZ) const
{
    const float distance = std::hypot(listenerX - x, listenerZ - z);
    const float fade = std::clamp((1.0f - distance / radius) / 0.75f, 0.0f, 1.0f);
    return volume * fade * fade * (3.0f - 2.0f * fade);
}

void SceneAudio::updateAmbient(const std::vector<AmbientZone>& zones, float x, float z, float dt)
{
    if (editing || dt <= 0.0f) return;
    for (auto& item : ambientLoops) item.second.target = 0.0f;

    for (const auto& zone : zones)
    {
        const float gain = zone.gainAt(x, z);
        if (zone.file.empty() || gain <= 0.0f) continue;
        auto [found, created] = ambientLoops.try_emplace(zone.file);
        auto& loop = found->second;
        if (created)
        {
            std::string normalized;
            loop.sound = loadWav(zone.file, normalized);
        }
        // Use one voice for overlapping zones with the same recording.
        loop.target = std::max(loop.target, gain);
    }

    const float blend = 1.0f - std::exp(-8.0f * dt);
    for (auto& item : ambientLoops)
    {
        auto& loop = item.second;
        if (!loop.sound) continue;
        loop.gain += (loop.target - loop.gain) * blend;
        if (loop.target == 0.0f && loop.gain < 0.001f)
        {
            if (loop.playing) loop.sound->Stop();
            loop.playing = false;
            loop.gain = 0.0f;
            continue;
        }
        loop.sound->SetVolume(loop.gain);
        if (!loop.playing) loop.playing = loop.sound->Play(true);
    }
}

bool SceneAudio::previewAmbient(const AmbientZone& zone)
{
    stopAmbientPreview();
    std::string normalized;
    auto sound = loadWav(zone.file, normalized);
    if (!sound) return false;
    sound->SetVolume(zone.volume);
    if (!sound->Play(true)) return false;
    ambientPreview = std::move(sound);
    return true;
}

void SceneAudio::stopAmbientPreview()
{
    ambientPreview.reset();
}

bool SceneAudio::prepareMarker(const std::string& file)
{
    if (file.empty()) return false;
    if (markerSounds.find(file) != markerSounds.end()) return true;
    std::string normalized;
    auto sound = loadWav(file, normalized);
    if (!sound) return false;
    markerSounds.emplace(file, std::move(sound));
    return true;
}

bool SceneAudio::playMarker(const Marker& marker, bool preview)
{
    if (preview != editing) return false;
    const auto sound = markerSounds.find(marker.file);
    if (sound == markerSounds.end()) return false;
    sound->second->SetVolume(marker.volume);
    return sound->second->Play();
}

void SceneAudio::playMarkers(const Timeline& timeline, float previous, float current, float wrapAt, bool preview)
{
    if (timeline.empty() || (current == previous && wrapAt == 0.0f)) return;
    const auto playRange = [&](float start, float end)
    {
        std::vector<const Marker*> crossed;
        for (const auto& marker : timeline)
            if (marker.time > start && marker.time <= end) crossed.push_back(&marker);
        std::stable_sort(crossed.begin(), crossed.end(),
            [](const Marker* a, const Marker* b) { return a->time < b->time; });
        for (const auto* marker : crossed) playMarker(*marker, preview);
    };
    if (wrapAt > 0.0f)
    {
        playRange(previous, wrapAt);
        playRange(-1.0f, current);
    }
    else playRange(previous, current);
}

void SceneAudio::stopMarkers()
{
    for (auto& item : markerSounds) item.second->Stop();
}
