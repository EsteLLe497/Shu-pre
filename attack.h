#pragma once

#include "component.h"
#include "Vector3.h"
#include <vector>

class Enemy;

// ダメージ設定と前方の球形判定を管理する、攻撃用コンポーネント。
class Attack : public Component
{
public:
    struct DamageParameters
    {
        float weaponDamage = 5.0f;    // 武器の基礎ダメージ
        float damageMultiplier = 1.0f; // 攻撃倍率
        float bonusDamage = 0.0f;      // 計算後に加えるダメージ
    };

    struct HitboxParameters
    {
        float forwardOffset = 1.2f; // プレイヤーから前方への距離
        float height = 1.0f;        // 判定の中心の高さ
        float radius = 0.8f;        // 球の半径
        float startTime = 0.2f;     // 判定開始（攻撃の再生時間・秒）
        float endTime = 0.4f;       // 判定終了（攻撃の再生時間・秒）
    };

    struct TargetParameters
    {
        float range = 3.0f;           // 敵の中心からの方向補正範囲
        float maxHeightDifference = 2.0f; // 上下に離れた敵は対象にしない
        bool autoAim = true;         // 攻撃開始時に近い敵の方を向く
        bool showCircle = true;      // 敵の周囲に範囲サークルを表示する
    };

    struct HitFeedbackParameters
    {
        float shakeStrength = 0.04f; // カメラ揺れの強さ
        float hitStopSeconds = 0.06f; // 命中時の停止時間
        float flashSeconds = 0.20f;   // 敵を白く点滅させる時間
    };

    explicit Attack(GameObject* owner) : Component(owner) {}
    void Init() override;   // 初期化
    void Uninit() override; // 終了処理
    void Update() override; // 毎フレームの更新
    void Draw() override;   // 判定とデバッグ画面の表示

    DamageParameters& parameters() { return m_parameters; }
    const DamageParameters& parameters() const { return m_parameters; }
    HitboxParameters& hitbox() { return m_hitbox; }
    TargetParameters& targeting() { return m_targeting; }
    HitFeedbackParameters& feedback() { return m_feedback; }
    void faceTarget(); // 攻撃開始時に呼び、範囲内の最寄りの敵へ向く
    float damage() const; // 基礎ダメージ × 倍率 ＋ 加算。最低0。

    // プレイヤーから呼ぶ。判定時間は武器と同じ再生時計に合わせる。
    void begin();
    void samplePlayback(float previousTime, float currentTime);
    void finish();
    bool enemiesPaused() const { return m_freezeEnemies; }

private:
    DamageParameters m_parameters;
    HitboxParameters m_hitbox;
    TargetParameters m_targeting;
    HitFeedbackParameters m_feedback;
    std::vector<Enemy*> m_hitEnemies; // 同じ一振りで二重に命中させない
    bool m_active = false;
    float m_hitFlash = 0.0f;
    bool m_showHitbox = false;
    bool m_showInactive = true;
    bool m_freezeEnemies = false;

    Vector3 hitboxCenter() const;
    void checkHits();
    Enemy* findTarget() const;
    void drawTargetCircles();
    void drawHitbox();
    void drawDebugPanel();
};
