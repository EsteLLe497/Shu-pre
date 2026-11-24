// =========================================================
// ファイルの情報[Player.h]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#pragma once

#include "GameObject.h"
#include "animationModel.h"
#include "ToonShader.h"
#include "PlayerActionSet.h"
#include "SceneAudio.h"
#include "AttackWeapon.h"
#include "attack.h"
#include "Explosion.h"
#include <algorithm>
#include <cstdint>

class Player : public GameObject
{
public:
    // 攻撃開始時に一度だけ発行し、同じ攻撃の全フレーム・全判定で共有する。
    using AttackId = std::uint64_t;

    enum class HitResult { miss, hit, evaded, perfectDodge, alreadyHandled };

    struct DamageHit
    {
        AttackId id = 0;
        Vector3 start{ 0, 0, 0 };
        Vector3 end{ 0, 0, 0 };
        float radius = 0.0f; // ワールド座標のカプセル。始点と終点が同じなら球になる。
    };

    struct HitEvent
    {
        HitResult result = HitResult::miss;
        AttackId attackId = 0;
    };

    struct DodgeSettings
    {
        float distance = 3.0f;         // 移動距離 [m]
        float moveTime = 0.20f;        // 移動にかける時間 [秒]
        float cooldown = 0.65f;        // 再使用までの時間 [秒]
        float invincibleTime = 0.24f;  // 無敵時間 [秒]
        float perfectWindow = 0.12f;   // 開始直後のジャスト回避受付 [秒]
        float afterimageTime = 0.24f;  // 残像が消えるまでの時間 [秒]
    };

    static AttackId newAttackId();
    bool tryDodge(Vector3 direction);
    void setDodgeSettings(const DodgeSettings& settings);

    bool isDodging() const
    {
        return m_dodgeAge < std::max(m_dodgeSettings.moveTime, m_dodgeSettings.invincibleTime);
    }

    bool isDodgeInvincible() const
    {
        return m_dodgeAge < m_dodgeSettings.invincibleTime;
    }

    const DodgeSettings& dodgeSettings() const
    {
        return m_dodgeSettings;
    }

    // 敵・罠から届いた判定は、プレイヤーの回避入力を処理した後で解決する。
    void submitIncomingHit(const DamageHit& hit)
    {
        m_incomingHits.push_back(hit);
    }

    HitResult resolveIncomingHit(const DamageHit& hit);
    bool consumeHitEvent(HitEvent& event);

    // 成功フラグは取得するまで保持する。取得時に一度だけ true を返す。
    bool consumePerfectDodge();

    bool hasPerfectDodge() const
    {
        return m_perfectDodgePending;
    }

    AttackId lastPerfectDodgeAttackId() const
    {
        return m_lastPerfectAttack;
    }

private:
    struct DodgeAfterimage
    {
        XMFLOAT4X4 world{};
        Vector3 position{ 0, 0, 0 };
        float age = 1000.0f;
    };

    // 回避の移動状態。直前位置は高速移動中のすり抜け判定にも使う。
    DodgeSettings m_dodgeSettings;
    float m_dodgeAge = 1000.0f;
    float m_dodgeCooldown = 0.0f;
    Vector3 m_dodgeDirection{ 0, 0, 1 };
    Vector3 m_dodgePreviousPosition{ 0, 0, 0 };
    Vector3 m_dodgeTrailHead{ 0, 0, 0 };
    bool m_dodgeBlocked = false;

    // 同一攻撃の重複通知と、同一回避のジャスト成功重複を防ぐ。
    bool m_dodgeAwarded = false;
    bool m_perfectDodgePending = false;
    AttackId m_lastPerfectAttack = 0;
    std::array<AttackId, 64> m_handledAttacks{};
    size_t m_handledCursor = 0;
    std::vector<DamageHit> m_incomingHits;

    // 通知を消費しない場合でもメモリが増え続けないよう、最新64件を保持する。
    std::array<HitEvent, 64> m_hitEvents{};
    size_t m_hitEventBegin = 0;
    size_t m_hitEventCount = 0;

    // 残像・トレイルの履歴と、専用の描画リソース。
    float m_perfectFlash = 0.0f;
    std::array<DodgeAfterimage, 8> m_afterimages{};
    size_t m_afterimageCount = 0;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_afterimageBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_afterimageDepth;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_dodgeTrailVertices;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_dodgeTrailRasterizer;

    void updateDodge(float dt);
    bool intersectsIncomingHit(const DamageHit& hit) const;
    void captureDodgeAfterimage();
    bool prepareDodgeEffects();
    void drawDodgeAfterimages();
    void drawDodgeTrail();

    Vector3 m_Velocity{ 0.0f, 0.0f, 0.0f }; //速度


    ToonShader m_toonShader;
    AttackWeapon m_weapon;
    Attack* m_attack = nullptr;
    ActionSequence m_weaponAction;
    ActionPlayback m_weaponPlayback;
    AttackCombo m_attackCombo;
    XMMATRIX m_weaponAnchor = XMMatrixIdentity();
    bool startWeaponAttack();
    bool startWeaponAttack(const ActionSequence& action);
    std::unique_ptr<Explosion> m_attackEffect;
    Vector3 m_attackEffectPosition{ 0.0f, 0.0f, 0.0f };
    bool m_attackEffectTriggered = false;
    float m_sampledActionTime = -1.0f;
    float m_weaponTime = -1.0f;
    XMMATRIX m_weaponEndAnchor = XMMatrixIdentity();

    bool m_Ground = false;
    bool m_jumpedFromGround = false;
    float m_airborneTime = 0.0f;
    static constexpr float jumpSpeed = 12.5f;
    static constexpr float gravity = 25.0f;
    float m_MoveAnimation = 0.0f;

    GameObject* m_Child;
    GameObject* m_Shadow;

    AnimationModel* m_AnimationModel = nullptr;
    float m_walkSpeed = 1.43f; // 歩行の移動速度
    float m_runSpeed = 4.47f;  // ダッシュの移動速度
    PlayerActionSet m_actions;
    std::array<SceneAudio::Timeline, static_cast<size_t>(PlayerActionKind::count)> m_motionSounds;
    PlayerActionKind m_soundKind = PlayerActionKind::count;
    float m_soundTime = -1.0f;
    void playMotionSounds(PlayerActionKind kind, float time, bool loop);
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
    bool isAttacking() const;
    void updateLocomotion(float dt, PlayerActionKind kind);
    void updateActionPose(float dt, float physicsDt);
    void spawnActionEffect(const ActionSequence& action);
    bool prepareAction(const ActionSequence& action);

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;
    void drawEffects();
    Attack* attack() { return m_attack; }
    const Attack* attack() const { return m_attack; }

    static constexpr float BodyRadius = 0.52f;
    static constexpr float BodyLowerCenter = 0.57f;
    static constexpr float BodyUpperCenter = 1.24f;

    void setMotionSounds(PlayerActionKind kind, const SceneAudio::Timeline& markers);
    bool hasMotionSounds(PlayerActionKind kind) const;
    float motionDuration(PlayerActionKind kind) const;
    void previewMotion(PlayerActionKind kind, float time);
    void clearMotionPreview();
    bool loadAction(const char* path, ActionSequence& action);
    // 登録済みの単発アクションか、先読みした任意のアクションを再生する。
    bool playAction(PlayerActionKind kind);
    bool playAction(const ActionSequence& action);
    ToonSettings& toonSettings() { return m_toonShader.settings(); }
};

