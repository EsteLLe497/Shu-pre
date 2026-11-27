#pragma once

#include "GameObject.h"
#include "animationModel.h"
#include "ToonShader.h"
#include "HeroineActionSet.h"
#include "SceneAudio.h"

class Heroine : public GameObject
{
private:
    AnimationModel* m_AnimationModel = nullptr;
    ToonShader m_toonShader;
    HeroineActionSet m_actions;
    std::array<SceneAudio::Timeline, static_cast<size_t>(HeroineActionKind::count)> m_motionSounds;
    HeroineActionKind m_playingKind = HeroineActionKind::idle;
    ActionSequence m_action;
    float m_time = 0.0f;
    float m_motionTime = 0.0f;
    bool m_singleAction = false;
    bool m_restart = true;
    bool m_ready = false;

    void samplePose(float time, float dt, bool loop);

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;
    bool playAction(HeroineActionKind kind);
    bool playAction(const ActionSequence& action);
    void setMotionSounds(HeroineActionKind kind, const SceneAudio::Timeline& markers);
    float motionDuration(HeroineActionKind kind) const;
    void previewMotion(HeroineActionKind kind, float time);
    void clearMotionPreview();
    ToonSettings& toonSettings() { return m_toonShader.settings(); }
};
