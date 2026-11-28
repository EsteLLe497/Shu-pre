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

    struct AmbientZone
    {
        std::string file;
        float x = 0.0f;
        float z = 0.0f;
        float radius = 18.0f;
        float volume = 0.55f;

        float gainAt(float listenerX, float listenerZ) const;
    };

    void updateAmbient(const std::vector<AmbientZone>& zones, float x, float z, float dt);
    bool previewAmbient(const AmbientZone& zone);
    void stopAmbientPreview();

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
