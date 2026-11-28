#pragma once

#include "GameObject.h"
#include "animationModel.h"
#include "ToonShader.h"
#include "HeroineActionSet.h"
#include "SceneAudio.h"
#include "HeroineMovement.h"
#include "HeroineLookAt.h"
#include "HeroineExpression.h"

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
    std::string m_modelPath;
    std::string m_modelStatus;
    HeroineMovement m_movement;
    HeroineLookAt m_lookAt;
    HeroineExpression m_expression;
    float m_actionEyeClosure = 0;
    std::array<std::string, 4> m_lookBones;
    std::string m_closeEyeMorph;
    bool m_fixedLookTarget = false;
    Vector3 m_lookTarget{ 0, 1.6f, 0 };
    Vector3 m_lookOrigin{ 0, 1.4f, 3 };
    Vector3 m_lookDirection{ 0, 0, 1 };
    float m_poseDeltaSeconds = 0;

    void samplePose(float time, float dt, bool loop);
    bool startAction(HeroineActionKind kind);
    bool resolveMovement(Vector3& position) const;
    void updateMovement(float dt);
    void initLookAt();
    void updateLookAt(AnimationModel& model);

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;
    bool setModel(const std::string& path);
    const std::string& modelPath() const { return m_modelPath; }
    const std::string& modelStatus() const { return m_modelStatus; }
    bool playAction(HeroineActionKind kind);
    bool playAction(const ActionSequence& action);
    void setMotionSounds(HeroineActionKind kind, const SceneAudio::Timeline& markers);
    float motionDuration(HeroineActionKind kind) const;
    void previewMotion(HeroineActionKind kind, float time);
    void clearMotionPreview();
    bool moveTo(const Vector3& target, float speed = 1.2f, float arrivalRadius = 0.05f, bool repeat = false);
    bool resetMovement(const Vector3& position);
    void stopMovement();
    void pauseMovement(bool paused);
    bool movementReady() const;
    const HeroineMovement& movement() const { return m_movement; }
    HeroineExpression& expression() { return m_expression; }
    HeroineLookAt& lookAt() { return m_lookAt; }
    const std::array<std::string, 4>& lookBones() const { return m_lookBones; }
    bool eyesReady() const { return !m_lookBones[2].empty() && !m_lookBones[3].empty(); }
    bool eyelidsReady() const { return !m_closeEyeMorph.empty(); }
    bool usesFixedLookTarget() const { return m_fixedLookTarget; }
    bool setLookTarget(const Vector3& target);
    void lookAtPlayer() { m_fixedLookTarget = false; }
    const Vector3& lookTarget() const { return m_lookTarget; }
    const Vector3& lookOrigin() const { return m_lookOrigin; }
    const Vector3& lookDirection() const { return m_lookDirection; }
    ToonSettings& toonSettings() { return m_toonShader.settings(); }
};
