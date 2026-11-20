#pragma once

#include "GameObject.h"
#include <wrl/client.h>

class Player;

// Visual prototype built from simple meshes.
class Enemy : public GameObject
{
private:
    enum class Attack { None, Punch, Slam, Charge };
    static constexpr float DetectionRadius = 4.5f;
    static constexpr float AttackRange = 1.8f;
    static constexpr float ChaseSpeed = 2.4f;
    static constexpr float TurnSpeed = 7.0f;
    static constexpr float SwingDuration = 1.0f;
    static constexpr float PunchInterval = 3.0f;
    static constexpr float SlamWindup = 0.30f;
    static constexpr float SlamImpact = 0.90f;
    static constexpr float ShockwaveDuration = 0.48f;
    static constexpr float ChargeWindup = 0.55f;
    static constexpr float ChargeEnd = 1.12f;
    static constexpr UINT RingVertexCount = 48 * 6;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_VertexLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader;
    UINT m_VertexCount = 0;
    bool m_MidBoss = false;
    Attack m_Attack = Attack::None;
    Attack m_NextAttack = Attack::Punch;
    bool m_LeftHand = true;
    float m_AttackTime = 0.0f;
    float m_PunchCooldown = 0.0f;
    bool m_ChargeBlocked = false;
    float m_GaitPhase = 0.0f;
    float m_GaitWeight = 0.0f;

    float AttackPose(float ready, float strike) const;
    float JumpHeight() const;
    bool MoveOnGround(const Vector3& displacement, const Player* player);
    void DrawPart(const Vector3& scale, const DirectX::XMMATRIX& transform,
        const DirectX::XMFLOAT4& color, bool shockwave = false);

public:
    void PreviewPose(float time, int action)
    {
        m_Attack = action == 1 ? Attack::Punch : action == 2 ? Attack::Slam : action == 3 ? Attack::Charge : Attack::None;
        const float end = action == 2 ? SlamImpact + ShockwaveDuration : action == 3 ? ChargeEnd + 0.33f : SwingDuration;
        if (time > end) m_Attack = Attack::None;
        m_AttackTime = time;
        m_GaitPhase = time * 4.0f;
    }
    void Configure(bool midBoss);
    float BodyRadius() const { return 0.7f * m_Scale.x; }
    bool IsAirborne() const { return JumpHeight() > 0.7f; }
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    // Keep existing projectile callers compatible; combat is deferred.
    void Shake(Vector3) {}
    void AddDamage(int) {}
};
