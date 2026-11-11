// =========================================================
// ファイルの情報[ActionSequence.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include <string>
#include <vector>
#include "MorphTarget.h"

struct ActionSequence
{
    float duration = 3.0f;
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
