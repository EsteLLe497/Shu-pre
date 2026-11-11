// =========================================================
// ファイルの情報[Player.h]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#pragma once

#include "GameObject.h"
#include "animationModel.h"
#include "ToonShader.h"
#include "PlayerActionSet.h"

class Player : public GameObject
{
private:
    Vector3 m_Velocity{ 0.0f, 0.0f, 0.0f }; //速度


    ToonShader m_toonShader;

    bool m_Ground = false;
    bool m_jumpedFromGround = false;
    float m_airborneTime = 0.0f;
    static constexpr float jumpSpeed = 15.0f;
    static constexpr float gravity = 35.0f;
    float m_MoveAnimation = 0.0f;
    class Audio* m_JumpSE;

    GameObject* m_Child;
    GameObject* m_Shadow;

    AnimationModel* m_AnimationModel = nullptr;
    float m_walkSpeed = 1.43f; // 歩行の移動速度
    float m_runSpeed = 4.47f;  // ダッシュの移動速度
    PlayerActionSet m_actions;
    PlayerActionKind m_locomotionKind = PlayerActionKind::idle;
    ActionSequence m_playingAction;
    PlayerActionKind m_playingActionKind = PlayerActionKind::count;
    ActionPlayback m_actionPlayback;
    float m_locomotionTime = 0.0f;
    double m_locomotionMotionTime = 0.0;
    bool m_locomotionEffectFired = false;
    bool m_faceApplied = false;
    bool m_actionMotionApplied = false;
    bool m_actionMotionRestart = false;
    bool m_yawning = false;

    void loadPlayerActions();
    void bindPlayerActions(AnimationModel& model);
    bool isLanding() const
    {
        return m_actionPlayback.active() && m_playingActionKind == PlayerActionKind::land;
    }
    void updateLocomotion(float dt, PlayerActionKind kind);
    void updateActionPose(float dt, float physicsDt);
    void spawnActionEffect(const ActionSequence& action);
    bool prepareAction(const ActionSequence& action);

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    bool loadAction(const char* path, ActionSequence& action);
    // 登録済みの単発アクションか、先読みした任意のアクションを再生する。
    bool playAction(PlayerActionKind kind);
    bool playAction(const ActionSequence& action);
    ToonSettings& toonSettings() { return m_toonShader.settings(); }
};

