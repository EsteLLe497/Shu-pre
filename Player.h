// =========================================================
// ファイルの情報[Player.h]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#pragma once

#include "GameObject.h"
#include "animationModel.h"
#include "ToonShader.h"

class Player : public GameObject
{
private:
    Vector3 m_Velocity{ 0.0f, 0.0f, 0.0f }; //速度


    ToonShader m_toonShader;

    bool m_Ground = true;
    float m_MoveAnimation = 0.0f;
    class Audio* m_JumpSE;

    GameObject* m_Child;
    GameObject* m_Shadow;

    class AnimationModel* m_AnimationModel;
    double m_animationTime = 0.0;
    std::string m_animationName;

    double m_nextAnimationTime = 0.0;
    std::string m_nextAnimationName;

    float m_blend = 0.0f;
    float m_walkSpeed = 1.5f; // Fallback when a clip has no root travel.
    float m_runSpeed = 7.5f;

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    void SetAnimation(const char* animationName);
    ToonSettings& toonSettings() { return m_toonShader.settings(); }
};

