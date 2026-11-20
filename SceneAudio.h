#pragma once

#include <string>
#include <vector>

namespace SceneAudio
{
    enum class Cue { Bgm, Jump, Land, Punch, Slam, Charge, Count };

    struct Setting
    {
        std::string file;
        float volume = 1.0f;
        bool enabled = false;
    };
    struct Marker
    {
        float time = 0.0f;
        std::string file;
        float volume = 1.0f;
    };
    using Timeline = std::vector<Marker>;

    void init();
    void shutdown();
    void setEditing(bool editing);
    const Setting& setting(Cue cue);
    bool assign(Cue cue, const std::string& file);
    void setVolume(Cue cue, float volume);
    void setEnabled(Cue cue, bool enabled);
    void play(Cue cue);
    bool preview(Cue cue);
    void stopPreview();
    bool save();
    bool prepareMarker(const std::string& file);
    bool playMarker(const Marker& marker, bool preview = false);
    void playMarkers(const Timeline& timeline, float previous, float current, float wrapAt = 0.0f, bool preview = false);
    void stopMarkers();
}
