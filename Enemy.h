#pragma once

#include "GameObject.h"
#include <wrl/client.h>
#include <DirectXCollision.h>
#include "animationModel.h"
#include "SceneAudio.h"
#include <array>
#include <memory>
#include <cstdint>

class Player;

// Visual prototype built from simple meshes.
class Enemy : public GameObject
{
public:
    enum class Motion { Idle, Move, Punch, Slam, Charge, Count };
    static constexpr size_t MotionCount = static_cast<size_t>(Motion::Count);
    struct AttackSettings
    {
        float detectionDistance = 4.5f;
        float attackInterval = 3.0f;
        float punchRepeatInterval = 3.0f;
        float punchStartRange = 1.55f;
        float slamStartRange = 2.95f;
        float chargeStartRange = 5.2f;
        float chargeDistance = 8.4f * (1.12f - 0.55f);
        float shockwaveRadius = 3.25f;
    };
    struct AttackPhase { const char* name = nullptr; float start = 0.0f, end = 0.0f; };
    struct HurtboxAdjustment
    {
        Vector3 offset{ 0, 0, 0 };
        float radiusScale = 1.0f;
        float lengthScale = 1.0f;
    };
    struct HurtboxPose { Vector3 start, end; float radius; };

private:
    std::unique_ptr<AnimationModel> m_character;
    std::vector<BoneCapsule> m_capsules;
    std::vector<HurtboxAdjustment> m_adjustments;
    std::string m_characterPath;
    bool m_hasCharacterMotion = false;
    float m_characterTime = 0.0f;
    float m_characterGroundOffset = 0.0f;
    std::array<std::string, MotionCount> m_motionFiles;
    std::array<std::string, MotionCount> m_motionClips;
    std::array<SceneAudio::Timeline, MotionCount> m_motionSounds;
    Motion m_currentMotion = Motion::Count;
    Motion m_previewMotion = Motion::Count;
    float m_previewTime = 0.0f;
    XMMATRIX characterWorld();
    enum class Attack { None, Punch, Slam, Charge };
    Attack poseAttack() const;
    float poseTime() const;
    const char* motionClip(Motion motion) const;
    void sampleCharacterMotion(Motion motion, float time);
    bool loadCharacter(const std::string& path, Enemy* modelOwner);
    static constexpr float PunchWindupEnd = 0.30f;
    static constexpr float PunchStrikeEnd = 0.48f;
    static constexpr float ChaseSpeed = 2.4f;
    static constexpr float TurnSpeed = 7.0f;
    static constexpr float SwingDuration = 1.0f;
    static constexpr float SlamWindup = 0.30f;
    static constexpr float SlamImpact = 0.90f;
    static constexpr float ShockwaveDuration = 0.48f;
    static constexpr float ChargeWindup = 0.55f;
    static constexpr float ChargeEnd = 1.12f;
    static constexpr float ChargeRetreatSpeed = 2.7f;
    static constexpr UINT RingVertexCount = 48 * 6;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_VertexLayout;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_VertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_PixelShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_hitFlashPS;
    UINT m_VertexCount = 0;
    bool m_MidBoss = false;
    float m_health = 10.0f;    // 現在のHP
    float m_hitFlashRemaining = 0.0f;
    float m_hitFlashDuration = 0.0f;
    float m_maxHealth = 10.0f; // 最大HP（通常10、ボス30）
    AttackSettings m_attackSettings;
    bool m_MeshCollider = true;
    Attack m_Attack = Attack::None;
    Attack m_NextAttack = Attack::Punch;
    bool m_LeftHand = true;
    float m_AttackTime = 0.0f;
    std::uint64_t m_attackHitId = 0;
    void submitAttackHit(Player& player, float previousTime, const Vector3& previousPosition);
    float m_PunchCooldown = 0.0f;
    float m_attackCooldown = 0.0f;
    bool m_ChargeBlocked = false;
    float m_GaitPhase = 0.0f;
    float m_GaitWeight = 0.0f;

    float AttackPose(float ready, float strike) const;
    float JumpHeight() const;
    bool MoveOnGround(const Vector3& displacement, const Player* player);
    void DrawPart(const Vector3& scale, const DirectX::XMMATRIX& transform,
        const DirectX::XMFLOAT4& color, bool shockwave = false);
    void VisitBodyParts(const std::function<void(const Vector3&, const DirectX::XMMATRIX&,
        const DirectX::XMFLOAT4&, bool)>& visit,
        const std::function<void(const Vector3&, float)>& fist = {});

public:
    void PreviewPose(float time, int action)
    {
        m_Attack = action == 1 ? Attack::Punch : action == 2 ? Attack::Slam : action == 3 ? Attack::Charge : Attack::None;
        const float end = action == 2 ? SlamImpact + ShockwaveDuration : action == 3 ? ChargeEnd + 0.33f : SwingDuration;
        if (time > end) m_Attack = Attack::None;
        m_AttackTime = time;
        m_GaitPhase = time * 4.0f;
    }
    ~Enemy() override;
    static AttackSettings defaultAttackSettings(bool midBoss);
    void setAttackSettings(const AttackSettings& settings) { m_attackSettings = settings; }
    const AttackSettings& attackSettings() const { return m_attackSettings; }
    float attackStartRange(Motion motion) const;
    static AttackPhase attackPhase(Motion motion);
    float chargeOffsetAt(float time) const;
    float shockwaveRadiusAt(float time) const;
    bool assignMotion(Motion motion, const std::string& file);
    void setMotionSounds(Motion motion, const SceneAudio::Timeline& markers);
    float motionDuration(Motion motion) const;
    void previewMotion(Motion motion, float time);
    void clearMotionPreview();
    bool loadCharacter(const std::string& path);
    std::unique_ptr<Enemy> prepareCharacter(const std::string& path,
        const std::array<std::string, MotionCount>& clips);
    void adoptCharacter(Enemy& prepared);
    bool hasCharacter() const { return m_character != nullptr; }
    size_t hurtboxCount() const { return m_capsules.size(); }
    const std::string& hurtboxBone(size_t i) const { return m_capsules[i].bone; }
    void setHurtboxAdjustment(size_t i, const HurtboxAdjustment& value) { m_adjustments[i] = value; }
    HurtboxPose hurtboxPose(size_t i);
    bool queryHurtbox(const Vector3& center, float radius, size_t& part);
    void Configure(bool midBoss);
    void SetMeshCollider(bool enabled) { m_MeshCollider = enabled; }
    bool HasMeshCollider() const { return m_MeshCollider && !isDead(); }
    bool IntersectsMesh(const DirectX::BoundingSphere& sphere);
    bool RaycastMesh(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, float& distance);
    void VisitColliderParts(const std::function<void(const DirectX::XMMATRIX&)>& visit);
    float BodyRadius() const { return 0.7f * m_Scale.x; }
    bool IsAirborne() const { return JumpHeight() > 0.7f; }
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    // 既存の弾からの呼び出しも維持する。
    void Shake(Vector3) {}
    void AddDamage(float damage, float flashDuration = 0.20f);
    void updateHitFeedback(float realSeconds);
    bool hitFlashWhite() const;
    void resetHealth();
    bool isDead() const { return m_health <= 0.0f; }
    float health() const { return m_health; }
    float maxHealth() const { return m_maxHealth; }
    void drawHealthBar();
};
