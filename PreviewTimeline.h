// =========================================================
// ファイルの情報[PreviewTimeline.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

struct PreviewTimeline
{
    static constexpr float frameStep = 1.0f / 60.0f;

    float time = 0.0f;
    float duration = 3.0f;
    float speed = 1.0f;
    bool playing = false;
    bool loop = true;
    bool motionEnabled = true;
    bool faceEnabled = true;
    bool effectEnabled = true;
    int soloTrack = -1; // -1 のときは各レイヤーのオン・オフを使う。

    float faceStart = 0.65f;
    float facePeak = 0.72f;
    float faceEnd = 0.84f;
    float faceStrength = 1.0f;
    bool smoothFaceBlend = true;
    float effectStart = 1.0f;

    float weaponVisualEnd = 0.0f;
    float playbackEnd() const;

    void advance(float dt);
    void seek(float seconds);
    bool trackEnabled(int track) const;
    float faceWeight() const;
    float effectAge() const;
};
