// =========================================================
// ファイルの情報[ActionSequence.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <array>
#include <string>
#include <vector>
#include "MorphTarget.h"

struct WeaponPose
{
    std::array<float, 3> position{};
    std::array<float, 3> angles{};
};

struct WeaponTrack
{
    std::string model;
    bool enabled = false;
    float appear = 0.15f;
    float middle = 0.40f;
    float disappear = 0.75f;
    float scale = 1.0f;
    std::array<float, 3> pivot{};
    std::array<WeaponPose, 3> keys{};
    bool trail = true;
    float trailLength = 0.12f;
    std::array<float, 3> trailBase{};
    std::array<float, 3> trailTip{ 0.0f, 0.0f, -0.5f };
    std::array<float, 3> trailColor{ 0.2f, 0.65f, 1.0f };
    bool dissolve = true;
    float dissolveDuration = 0.32f;
    float dissolveGlow = 1.25f;
    std::array<float, 3> dissolveColor{ 0.28f, 0.67f, 1.0f };
    bool dissolveParticles = true;
    int particleCount = 64;
    std::array<float, 3> particleColor{ 0.48f, 0.78f, 1.0f };
    float visualEnd() const
    {
        const float tail = dissolve ? dissolveDuration + (dissolveParticles ? 0.38f : 0.0f) : 0.0f;
        return disappear + (trail && trailLength > tail ? trailLength : tail);
    }

    bool effectOnWeapon = false;
    bool cameraEnabled = false;
    std::array<float, 3> cameraTarget{};

    bool visible(float time) const;
    WeaponPose sample(float time) const;
    float cameraWeight(float time, float duration) const;
};

struct ActionSequence
{
    float duration = 3.0f;
    float speed = 1.0f;
    WeaponTrack weapon;
    bool motionEnabled = true;
    std::string clip = "Walk";
    bool faceEnabled = false;
    std::string baseMorph;
    std::string targetMorph;
    std::vector<MorphTarget> extraMorphs;
    float faceStart = 0.65f;
    float facePeak = 0.72f;
    float faceEnd = 0.84f;
    float faceStrength = 1.0f;
    bool smoothFaceBlend = true;
    bool effectEnabled = true;
    float effectStart = 1.0f;
    float effectX = 0.75f;
    float effectHeight = 0.20f;
    static constexpr float effectForward = 0.40f;
    static constexpr float effectScale = 0.12f;
};

bool hasExpressionMorphs(const ActionSequence& action, const std::vector<std::string>& names);
float sampleExpression(float time, float start, float peak, float end, float strength, bool smooth);

class ActionPlayback
{
private:
    bool m_active = false;
    bool m_effectFired = false;
    float m_time = 0.0f;

public:
    void start(float time = 0.0f);
    void stop() { m_active = false; }
    bool takeEffect(const ActionSequence& action);
    void advance(const ActionSequence& action, float dt);
    void advanceTo(float endTime, float dt, bool holdAtEnd);
    bool active() const { return m_active; }
    float time() const { return m_time; }
};
