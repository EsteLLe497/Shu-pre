// =========================================================
// ファイルの情報[Player.cpp]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#include "main.h"
#include "renderer.h"
#include "Player.h"
//#include "modelRenderer.h"
#include "animationModel.h"
#include "input.h"
#include "Collision.h"

#include "Bullet.h"

#include "Manager.h"
#include "Camera.h"
#include "SceneAudio.h"

#include "Enemy.h"
#include "Tree.h"
#include "Box.h"
#include "Shadow.h"
#include "MeshField.h"
#include "SceneEditor.h"
#include "planetarium.h"
#include <cmath>

namespace
{
    float clampValue(float value, float lo, float hi)
    {
        return std::max(lo, std::min(value, hi));
    }

    constexpr float groundTolerance = 0.001f;
    constexpr float maxGroundDrop = 0.15f;
    constexpr float minLandingAirTime = 0.08f;
    constexpr float shadowOffset = 0.01f;
    constexpr float shadowSize = 1.5f;

    constexpr float dodgeSweepStep = 0.08f;        // 壁抜けを防ぐ衝突確認の間隔 [m]
    constexpr float dodgeImageSpacing = 0.38f;     // 残像を残す移動間隔 [m]
    constexpr size_t dodgeTrailVerticesPerSegment = 12;

    struct DodgeTrailStream
    {
        float height;
        float offset;
        float width;
    };

    // 足元から肩まで、それぞれ異なる細さの軌跡を残す。
    constexpr std::array<DodgeTrailStream, 4> dodgeTrailStreams{{
        { 0.25f, -0.15f, 0.026f },
        { 0.62f,  0.18f, 0.045f },
        { 1.00f, -0.17f, 0.060f },
        { 1.34f,  0.15f, 0.032f },
    }};

    float afterimageOpacity(float age, float lifetime)
    {
        const float remaining = std::max(0.0f, 1.0f - age / lifetime);
        return remaining * remaining;
    }

    // 一歩分の移動先を補正し、壁・大きな段差・木に阻まれたら false を返す。
    bool resolveDodgeStep(const Vector3& previous, Vector3& next,
        const MeshField* field, const std::vector<Tree*>& trees)
    {
        const Vector3 desired = next;
        Vector3 velocity{ 0, 0, 0 };
        bool grounded = false;

        const auto blocked = [&]()
        {
            return hypotf(next.x - desired.x, next.z - desired.z) > 0.001f;
        };

        SceneEditor::resolvePlayerCollision(previous, next, velocity, grounded);
        if (blocked())
        {
            return false;
        }

        // 配置物の床に乗っている間は高さを維持し、それ以外は地形に追従する。
        if (!grounded)
        {
            float height = 0.0f;
            if ((field && !field->tryGetHeight(next.x, next.z, height))
                || height - previous.y > 0.20f || previous.y - height > 0.25f)
            {
                return false;
            }

            next.y = height;
            SceneEditor::resolvePlayerCollision(previous, next, velocity, grounded);
            if (blocked())
            {
                return false;
            }
        }

        for (const auto* tree : trees)
        {
            const Vector3 delta = next - tree->GetPosition();
            if (hypotf(delta.x, delta.z) < 1.4f)
            {
                return false;
            }
        }

        return true;
    }

    // カプセル同士の判定に使う、二つの線分の最短距離の二乗。
    float segmentDistanceSquared(const Vector3& a, const Vector3& b,
        const Vector3& c, const Vector3& d)
    {
        const Vector3 u = b - a, v = d - c, w = a - c;
        const float uu = u * u, vv = v * v, uv = u * v, uw = u * w, vw = v * w;
        float s = 0.0f, t = 0.0f;

        // どちらかの線分が点になっている場合も扱う。
        if (uu <= 0.000001f && vv <= 0.000001f)
        {
            return w * w;
        }

        if (uu <= 0.000001f)
        {
            t = clampValue(vw / vv, 0.0f, 1.0f);
        }
        else
        {
            if (vv > 0.000001f)
            {
                const float denom = uu * vv - uv * uv;
                if (denom > 0.000001f)
                {
                    s = clampValue((uv * vw - uw * vv) / denom, 0.0f, 1.0f);
                }

                t = (uv * s + vw) / vv;
                if (t < 0.0f)
                {
                    t = 0.0f;
                    s = clampValue(-uw / uu, 0.0f, 1.0f);
                }
                else if (t > 1.0f)
                {
                    t = 1.0f;
                    s = clampValue((uv - uw) / uu, 0.0f, 1.0f);
                }
            }
            else
            {
                s = clampValue(-uw / uu, 0.0f, 1.0f);
            }
        }

        const Vector3 delta = w + u * s - v * t;
        return delta * delta;
    }
}

// =========================================================
// Initialize the player and match travel speed to each FBX stride.
// =========================================================
void Player::Init()
{
    m_Layer = 1;
    m_Position = { 0.0f, 0.0f, 0.0f };
    m_Scale = { 1.0f, 1.0f, 1.0f };



    //ModelRenderer* m_ModelRenderer = AddGameComponent<ModelRenderer>(this);
    //m_ModelRenderer->Load("asset\\model\\player.obj");
    m_AnimationModel = AddGameComponent<AnimationModel>(this);
    m_attack = AddGameComponent<Attack>(this);
    loadPlayerActions();
    updateLocomotion(0.0f, PlayerActionKind::idle);

    // シェーダー読込
    m_toonShader.init();

    m_Child = Manager::AddGameObj<GameObject>();
    m_Child->SetParent(this);
    m_Child->SetPosition({ 0.0f,2.0f,0.0f });

    //ModelRenderer* childModel = m_Child->AddGameComponent<ModelRenderer>(m_Child);
    //childModel->Load("asset\\model\\Rabbit\\rabbit_1.obj");
    m_Shadow = Manager::AddGameObj<Shadow>();
    m_Shadow->SetScale({ shadowSize, shadowSize, shadowSize });
}

void Player::Uninit()
{
    if (m_attackEffect)
    {
        m_attackEffect->Uninit();
        m_attackEffect.reset();
    }
    m_dodgeTrailVertices.Reset();
    m_dodgeTrailRasterizer.Reset();
    m_afterimageDepth.Reset();
    m_afterimageBlend.Reset();
    m_weapon.clear();
    m_toonShader.uninit();

    GameObject::Uninit();
    m_attack = nullptr;
}

// =========================================================
// プレイヤーの移動、衝突、モーションを更新
// =========================================================
void Player::Update()
{
    const float timeScale = planetarium::updatePlayer();

    Vector3 oldPosition = m_Position;
    const float dt = timeScale / 60.0f;
    const float dodgeDt = Manager::animationDeltaSeconds() * timeScale;
    m_dodgePreviousPosition = m_Position;
    m_dodgeCooldown = std::max(0.0f, m_dodgeCooldown - dodgeDt);
    for (auto& image : m_afterimages)
    {
        image.age += dodgeDt;
    }

    m_perfectFlash = std::max(0.0f, m_perfectFlash - dodgeDt);

    Vector3 rot = m_Child->GetRotation();
    rot.y += 1.0f * dt;
    m_Child->SetRotation(rot);

    const bool oldGround = m_Ground;

    Camera* camera = Manager::GetGameObj<Camera>();
    Vector3 forward = camera->GetFoward();
    Vector3 right = camera->GetRight();

    forward.y = 0.0f;
    forward.normalize();

    right.y = 0.0f;
    right.normalize();

    // Build one camera-relative direction so diagonal movement has equal speed.
    Vector3 moveDirection{ 0.0f, 0.0f, 0.0f };
    if (Input::GetKeyPress('D')) moveDirection += right;
    if (Input::GetKeyPress('A')) moveDirection -= right;
    if (Input::GetKeyPress('W')) moveDirection += forward;
    if (Input::GetKeyPress('S')) moveDirection -= forward;

    if (Input::GetKeyTrigger(VK_RETURN) && Input::isGameplayActive())
    {
        tryDodge(moveDirection);
    }
    updateDodge(dodgeDt);
    // 回避中の経路は確認済み。通常移動と重力は、到達できた位置から処理する。
    oldPosition = m_Position;
    for (const auto& hit : m_incomingHits)
    {
        resolveIncomingHit(hit);
    }

    m_incomingHits.clear();
    m_Ground = false;

    const bool isMoving = VectorMag(moveDirection) > 0.0f;
    const bool isDashing = isMoving && Input::GetKeyPress(VK_SHIFT);

    // 踏ん張り中は移動入力で加速しない。
    const bool attacking = isAttacking();
    const bool canMove = isMoving && !isLanding() && !attacking && !isDodging();
    if (attacking)
        m_Velocity.x = m_Velocity.z = 0.0f;

    // Explicit target speeds keep walk and dash distinct from the first steps.
    if (isMoving) moveDirection.normalize();

    const float targetSpeed = canMove ? (isDashing ? m_runSpeed : m_walkSpeed) : 0.0f;
    const Vector3 targetVelocity = moveDirection * targetSpeed;
    const float response = canMove ? 16.0f : 35.0f;
    const float velocityBlend = 1.0f - expf(-response * dt);

    m_Velocity.x += (targetVelocity.x - m_Velocity.x) * velocityBlend;
    m_Velocity.z += (targetVelocity.z - m_Velocity.z) * velocityBlend;

    if (!canMove && m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z < 0.0025f)
    {
        m_Velocity.x = 0.0f;
        m_Velocity.z = 0.0f;
    }
    // Match the locomotion clip to the current input state.

    // Face the travel direction while preserving the last facing when stopped.
    if (m_Velocity.x * m_Velocity.x + m_Velocity.z * m_Velocity.z > 0.0001f)
    {
        m_Rotation.y = atan2f(m_Velocity.x, m_Velocity.z);
    }
    // 接地中の新しい入力だけで踏み切り、空中での押し直しは受け付けない。
    if (oldGround && !isLanding() && !attacking && !isDodging() && Input::GetKeyTrigger(VK_SPACE))
    {
        m_Velocity.y = jumpSpeed;
        m_jumpedFromGround = true;
        const auto jump = isDashing ? PlayerActionKind::runJump : PlayerActionKind::jump;
        if (!playAction(jump) || !hasMotionSounds(jump)) SceneAudio::play(SceneAudio::Cue::Jump);
    }

    m_Velocity.y -= gravity * dt;

    m_Position += m_Velocity * dt;
   
    auto trees = Manager::GetGameObjs<Tree>();
    for(auto tree : trees)
    {
        Vector3 treePos = tree->GetPosition();
        Vector3 playerPos = m_Position;

        treePos.y = 0.0f;
        playerPos.y = 0.0f;

        Vector3 dir = playerPos - treePos;
        float length = VectorMag(dir);
        if (length < 1.4f)
        {
            dir /= length;
            dir *= 1.4f - length;

            m_Position += dir;
        }
    }

    SceneEditor::resolvePlayerCollision(oldPosition, m_Position, m_Velocity, m_Ground);

    //if (!oldGround && m_Ground)
    //{
    //    m_Scale.y = 0.5f;
    //    m_Scale.x = 2.0f;
    //    m_Scale.z = 2.0f;
    //}

    // 押し戻し後の足元を調べ、箱の上と地形の上を区別する。
    const MeshField* meshField = Manager::GetGameObj<MeshField>();
    float groundHeight = 0.0f;
    const bool hasGround = !meshField || meshField->tryGetHeight(m_Position.x, m_Position.z, groundHeight);

    bool followSlope = false;
    if (meshField && hasGround && oldGround && !m_Ground && m_Velocity.y <= 0.0f)
    {
        float oldHeight;
        followSlope = meshField->tryGetHeight(oldPosition.x, oldPosition.z, oldHeight)
            && fabsf(oldPosition.y - oldHeight) <= groundTolerance
            && oldHeight - groundHeight <= maxGroundDrop;
    }

    // 下り坂は接地中だけ追従し、ジャンプの上昇速度は維持する。
    if (hasGround && (followSlope || m_Position.y <= groundHeight))
    {
        m_Position.y = groundHeight;
        m_Velocity.y = fmaxf(0.0f, m_Velocity.y);
        m_Ground = m_Velocity.y == 0.0f;
    }

    // ジャンプの着地は移動姿勢へ直接つなぎ、段差からの落下だけ踏ん張る。
    if (m_Ground)
    {
        const bool landingMotion = !m_jumpedFromGround && !oldGround && m_airborneTime >= minLandingAirTime
            && playAction(PlayerActionKind::land);
        if (!oldGround && (!landingMotion || !hasMotionSounds(PlayerActionKind::land)))
            SceneAudio::play(SceneAudio::Cue::Land);
        if (m_jumpedFromGround)
        {
            if (m_playingActionKind == PlayerActionKind::jump || m_playingActionKind == PlayerActionKind::runJump)
                m_actionPlayback.stop();
        }
        else if (landingMotion)
        {
            // 踏ん張りに入る瞬間に水平速度を止め、接地点から滑らないようにする。
            m_Velocity.x = 0.0f;
            m_Velocity.z = 0.0f;
        }
        m_jumpedFromGround = false;
        m_airborneTime = 0.0f;
    }
    else
    {
        m_airborneTime += dt;
    }

    // 地上で操作がなければ、10秒後にあくびを1回再生する。
    const bool isIdle = m_Ground && !isMoving && !isDodging() && Input::isGameplayActive() && !Input::hasGameplayActivity();
    if (m_yawning && !isIdle)
        m_actionPlayback.stop();
    if (!m_actionPlayback.active())
        m_yawning = false;

    if (m_actions.updateIdleTimer(isIdle && (!m_actionPlayback.active() || m_yawning), dt))
        m_yawning = playAction(PlayerActionKind::yawn);

    m_attackCombo.advance(Manager::animationDeltaSeconds() * timeScale);
    if ((Input::GetKeyTrigger('J') || Input::GetKeyTrigger(VK_LBUTTON))
        && m_Ground && !isLanding() && !isDodging())
        startWeaponAttack();


    if (m_Ground)
    {
        //m_MoveAnimation += VectorMag(m_Velocity) * dt;
        //m_Scale.y += sinf(m_MoveAnimation * 3.0f) * 0.03f;
    }

    // 影を地表に置き、地形の外では表示しない。
    const float visibleShadowSize = hasGround ? shadowSize : 0.0f;
    m_Shadow->SetScale({ visibleShadowSize, visibleShadowSize, visibleShadowSize });
    m_Shadow->SetPosition({ m_Position.x, groundHeight + shadowOffset, m_Position.z });

    const bool dodgeMoving = m_dodgeAge < m_dodgeSettings.moveTime && !m_dodgeBlocked;
    const auto kind = dodgeMoving ? PlayerActionKind::run
        : canMove ? (isDashing ? PlayerActionKind::run : PlayerActionKind::walk) : PlayerActionKind::idle;
    // モーションと表情は実時間で進め、移動や衝突判定は固定間隔で更新する。
    const float animationDt = Manager::animationDeltaSeconds() * timeScale;
    updateLocomotion(animationDt * (dodgeMoving ? 1.8f : 1.0f), kind);
    updateActionPose(animationDt, dt);
    GameObject::Update();
}

void Player::Draw()
{
    // 表示モデルの向きだけを調整し、本体と輪郭で同じポーズを使う。
    m_AnimationModel->setModelYawOffset(XM_PI + m_toonShader.settings().modelYaw);
    m_toonShader.bindSurface();
    GameObject::Draw();
    m_toonShader.drawOutline(*m_AnimationModel);
}

// Draw translucent effects after world geometry so later objects cannot erase them.
void Player::drawEffects()
{
    drawDodgeAfterimages();
    if (m_weaponAction.weapon.enabled && m_weaponTime >= 0.0f && m_weaponTime < m_weaponAction.weapon.visualEnd())
    {
        m_toonShader.bindSurface();
        const auto anchor = m_weaponAnchor;
        m_weapon.draw(m_weaponAction.weapon, m_weaponTime, anchor, &m_weaponEndAnchor);
    }
    if (m_weaponTime >= 0.0f && m_weaponAction.weapon.enabled)
    {
        const float age = m_weaponTime - m_weaponAction.effectStart;
        if (m_attackEffectTriggered && age >= 0.0f && age < Explosion::frameCount * Explosion::frameDuration)
        {
            if (auto* camera = Manager::GetGameObj<Camera>())
            {
                m_attackEffect->SetPosition(m_attackEffectPosition);
                Renderer::SetDepthEnable(false);
                m_attackEffect->drawFrame(static_cast<int>(age / Explosion::frameDuration), camera->GetViewMatrix());
                Renderer::SetDepthEnable(true);
            }
        }
    }
}

Player::AttackId Player::newAttackId()
{
    static AttackId next = 0;
    return ++next;
}

// 回避設定の単位・範囲を揃え、ジャスト受付が無敵時間を超えないようにする。
void Player::setDodgeSettings(const DodgeSettings& settings)
{
    const auto bounded = [](float value, float fallback, float lo, float hi)
    {
        return std::isfinite(value) ? clampValue(value, lo, hi) : fallback;
    };

    m_dodgeSettings.distance = bounded(settings.distance, 3.0f, 0.1f, 10.0f);
    m_dodgeSettings.moveTime = bounded(settings.moveTime, 0.20f, 0.08f, 0.6f);
    m_dodgeSettings.invincibleTime = bounded(settings.invincibleTime, 0.24f, 0.01f, 2.0f);
    m_dodgeSettings.perfectWindow = bounded(settings.perfectWindow, 0.12f, 0.0f, m_dodgeSettings.invincibleTime);

    const float minimumCooldown = std::max(m_dodgeSettings.moveTime, m_dodgeSettings.invincibleTime);
    m_dodgeSettings.cooldown = bounded(settings.cooldown, 0.65f, minimumCooldown, 10.0f);
    m_dodgeSettings.afterimageTime = bounded(settings.afterimageTime, 0.24f, 0.05f, 2.0f);
}

// 入力方向を固定して回避を開始する。ここではまだ座標を動かさない。
bool Player::tryDodge(Vector3 direction)
{
    if (!m_Ground || m_dodgeCooldown > 0.0f || isDodging() || isAttacking())
    {
        return false;
    }

    if (!std::isfinite(direction.x) || !std::isfinite(direction.z))
    {
        return false;
    }

    // 無入力なら現在の正面へ回避する。斜め入力でも距離は同じにする。
    direction.y = 0.0f;
    if (VectorMag(direction) < 0.001f)
    {
        direction = { sinf(m_Rotation.y), 0.0f, cosf(m_Rotation.y) };
    }
    direction.normalize();

    m_dodgeDirection = direction;
    m_dodgePreviousPosition = m_Position;
    m_dodgeTrailHead = m_Position;
    m_dodgeAge = 0.0f;
    m_dodgeCooldown = m_dodgeSettings.cooldown;
    m_dodgeBlocked = false;
    m_dodgeAwarded = false;
    m_perfectFlash = 0.0f;

    // 攻撃の予約や武器の表示を引き継がず、回避を優先する。
    m_Velocity.x = m_Velocity.z = 0.0f;
    m_Rotation.y = atan2f(direction.x, direction.z);
    m_actionPlayback.stop();
    m_weaponPlayback.stop();
    if (m_attack) m_attack->finish();
    m_attackCombo.reset();
    m_weaponTime = -1.0f;
    m_yawning = false;
    m_attackEffectTriggered = false;

    m_afterimageCount = 0;
    captureDodgeAfterimage();
    return true;
}

// 実際に通過した位置だけを記録する。満杯なら最も古い残像から捨てる。
void Player::captureDodgeAfterimage()
{
    if (m_afterimageCount == m_afterimages.size())
    {
        for (size_t i = 1; i < m_afterimageCount; ++i)
        {
            m_afterimages[i - 1] = m_afterimages[i];
        }
        --m_afterimageCount;
    }

    auto& image = m_afterimages[m_afterimageCount++];
    XMStoreFloat4x4(&image.world, XMMatrixRotationY(XM_PI + m_toonShader.settings().modelYaw) * GetMatrx());
    image.position = m_Position;
    image.age = 0.0f;
}

// 減速曲線の差分で移動量を求め、フレームレートによる距離の変化を防ぐ。
void Player::updateDodge(float dt)
{
    const float previousAge = m_dodgeAge;
    m_dodgeAge = std::min(1000.0f, m_dodgeAge + std::max(0.0f, dt));
    if (previousAge >= m_dodgeSettings.moveTime || m_dodgeBlocked || dt <= 0.0f)
    {
        return;
    }

    const auto progress = [&](float age)
    {
        const float t = clampValue(age / m_dodgeSettings.moveTime, 0.0f, 1.0f);
        return 1.0f - (1.0f - t) * (1.0f - t);
    };

    const float distance = m_dodgeSettings.distance * (progress(m_dodgeAge) - progress(previousAge));
    const int steps = std::max(1, static_cast<int>(ceilf(distance / dodgeSweepStep)));
    const Vector3 step = m_dodgeDirection * (distance / steps);
    const auto* field = Manager::GetGameObj<MeshField>();
    const auto trees = Manager::GetGameObjs<Tree>();

    // 移動を細かく分け、薄い壁も飛び越えないよう途中を確認する。
    for (int i = 0; i < steps; ++i)
    {
        Vector3 next = m_Position + step;
        if (!resolveDodgeStep(m_Position, next, field, trees))
        {
            m_dodgeBlocked = true;
            break;
        }

        m_Position = next;
        m_dodgeTrailHead = next;

        if (!m_afterimageCount
            || VectorMag(m_Position - m_afterimages[m_afterimageCount - 1].position) >= dodgeImageSpacing)
        {
            captureDodgeAfterimage();
        }
    }
}

// 現在の体と、このフレームで通過した区間だけを判定する。
// 回避開始地点を判定し続けると、離れた場所の攻撃でも成功してしまう。
bool Player::intersectsIncomingHit(const DamageHit& hit) const
{
    const Vector3 lower{ 0, BodyLowerCenter, 0 };
    const Vector3 upper{ 0, BodyUpperCenter, 0 };
    const float radius = BodyRadius + hit.radius;
    const float radiusSquared = radius * radius;

    const auto intersects = [&](const Vector3& start, const Vector3& end)
    {
        return segmentDistanceSquared(start, end, hit.start, hit.end) <= radiusSquared;
    };

    if (intersects(m_Position + lower, m_Position + upper))
    {
        return true;
    }

    return isDodgeInvincible()
        && (intersects(m_dodgePreviousPosition + lower, m_dodgePreviousPosition + upper)
            || intersects(m_dodgePreviousPosition + lower, m_Position + lower)
            || intersects(m_dodgePreviousPosition + upper, m_Position + upper));
}

// 命中・通常回避・ジャスト回避を決め、同じ攻撃について一度だけ通知する。
Player::HitResult Player::resolveIncomingHit(const DamageHit& hit)
{
    if (hit.id == 0 || !std::isfinite(hit.radius) || hit.radius < 0.0f)
    {
        return HitResult::miss;
    }

    if (std::find(m_handledAttacks.begin(), m_handledAttacks.end(), hit.id) != m_handledAttacks.end())
    {
        return HitResult::alreadyHandled;
    }

    if (!intersectsIncomingHit(hit))
    {
        return HitResult::miss;
    }

    HitResult result = HitResult::hit;
    if (isDodgeInvincible())
    {
        result = HitResult::evaded;
        if (!m_dodgeAwarded && m_dodgeAge < m_dodgeSettings.perfectWindow)
        {
            result = HitResult::perfectDodge;
            m_dodgeAwarded = true;
            m_perfectDodgePending = true;
            m_lastPerfectAttack = hit.id;
            m_perfectFlash = 0.30f;
            captureDodgeAfterimage();
        }
    }

    m_handledAttacks[m_handledCursor++ % m_handledAttacks.size()] = hit.id;

    // 通知は古い順に消費する。未消費のまま満杯になったら最古の一件を捨てる。
    if (m_hitEventCount == m_hitEvents.size())
    {
        m_hitEventBegin = (m_hitEventBegin + 1) % m_hitEvents.size();
        --m_hitEventCount;
    }
    const size_t nextEvent = (m_hitEventBegin + m_hitEventCount) % m_hitEvents.size();
    m_hitEvents[nextEvent] = { result, hit.id };
    ++m_hitEventCount;

    return result;
}

bool Player::consumePerfectDodge()
{
    const bool pending = m_perfectDodgePending;
    m_perfectDodgePending = false;
    return pending;
}

bool Player::consumeHitEvent(HitEvent& event)
{
    if (!m_hitEventCount)
    {
        return false;
    }

    event = m_hitEvents[m_hitEventBegin];
    m_hitEventBegin = (m_hitEventBegin + 1) % m_hitEvents.size();
    --m_hitEventCount;
    return true;
}

// 初回の描画時だけ専用リソースを作る。深度は比較し、書き込みは行わない。
bool Player::prepareDodgeEffects()
{
    auto* device = Renderer::GetDevice();

    if (!m_afterimageBlend)
    {
        D3D11_BLEND_DESC blend{};
        auto& target = blend.RenderTarget[0];
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D11_BLEND_BLEND_FACTOR;
        target.DestBlend = D3D11_BLEND_ONE;
        target.BlendOp = target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ZERO;
        target.DestBlendAlpha = D3D11_BLEND_ONE;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blend, m_afterimageBlend.GetAddressOf())))
        {
            return false;
        }
    }

    if (!m_afterimageDepth)
    {
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        if (FAILED(device->CreateDepthStencilState(&depth, m_afterimageDepth.GetAddressOf())))
        {
            return false;
        }
    }

    if (!m_dodgeTrailVertices)
    {
        D3D11_BUFFER_DESC buffer{};
        const size_t vertexCount = m_afterimages.size() * dodgeTrailStreams.size() * dodgeTrailVerticesPerSegment;
        buffer.ByteWidth = static_cast<UINT>(sizeof(VERTEX_3D) * vertexCount);
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, m_dodgeTrailVertices.GetAddressOf())))
        {
            return false;
        }
    }

    if (!m_dodgeTrailRasterizer)
    {
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&rasterizer, m_dodgeTrailRasterizer.GetAddressOf())))
        {
            return false;
        }
    }

    return true;
}

// 残像とトレイルを描き、変更した描画設定を本体の描画前に戻す。
void Player::drawDodgeAfterimages()
{
    if (!m_AnimationModel || !m_afterimageCount
        || m_afterimages[m_afterimageCount - 1].age >= m_dodgeSettings.afterimageTime)
    {
        return;
    }

    if (!prepareDodgeEffects())
    {
        return;
    }

    auto* context = Renderer::GetDeviceContext();

    // 本体の描画へ影響を残さないよう、呼び出し前の状態を保存する。
    Microsoft::WRL::ComPtr<ID3D11BlendState> previousBlend;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> previousDepth;
    float previousFactor[4]{};
    UINT previousMask = 0;
    UINT previousStencil = 0;
    context->OMGetBlendState(previousBlend.GetAddressOf(), previousFactor, &previousMask);
    context->OMGetDepthStencilState(previousDepth.GetAddressOf(), &previousStencil);

    const ToonSettings settings = m_toonShader.settings();
    m_toonShader.settings().shadingMode = 2;
    m_toonShader.settings().useBaseTextures = false;
    m_toonShader.settings().exposure = 1.0f;
    m_toonShader.bindSurface();
    context->OMSetDepthStencilState(m_afterimageDepth.Get(), 0);

    for (size_t i = 0; i < m_afterimageCount; ++i)
    {
        const auto& image = m_afterimages[i];
        const float opacity = afterimageOpacity(image.age, m_dodgeSettings.afterimageTime);
        if (opacity <= 0.0f)
        {
            continue;
        }

        const bool perfect = m_perfectFlash > 0.0f;
        const Vector3 color = perfect ? Vector3{ 1.0f, 0.78f, 0.20f } : Vector3{ 0.12f, 0.75f, 1.0f };
        const float alpha = opacity * (perfect ? 0.30f : 0.22f);
        const float factor[4] = { alpha * color.x, alpha * color.y, alpha * color.z, 1.0f };
        context->OMSetBlendState(m_afterimageBlend.Get(), factor, 0xffffffff);
        m_AnimationModel->drawAt(XMLoadFloat4x4(&image.world));
    }

    drawDodgeTrail();

    m_toonShader.settings() = settings;
    context->OMSetBlendState(previousBlend.Get(), previousFactor, previousMask);
    context->OMSetDepthStencilState(previousDepth.Get(), previousStencil);
}

// 中央の明るい頂点から両端の透明な頂点へ補間し、既存シェーダーで柔らかい光の筋を描く。
void Player::drawDodgeTrail()
{
    if (m_afterimageCount < 2)
    {
        return;
    }

    auto* camera = Manager::GetGameObj<Camera>();
    if (!camera)
    {
        return;
    }

    auto* context = Renderer::GetDeviceContext();

    struct TrailPoint
    {
        Vector3 position;
        float opacity;
    };

    // 消えていない履歴と、最後に到達できた先端をつなぐ。
    std::vector<TrailPoint> points;
    for (size_t i = 0; i < m_afterimageCount; ++i)
    {
        const auto& image = m_afterimages[i];
        const float opacity = afterimageOpacity(image.age, m_dodgeSettings.afterimageTime);
        if (opacity > 0.0f)
        {
            points.push_back({ image.position, opacity });
        }
    }

    if (points.empty())
    {
        return;
    }

    if (VectorMag(m_dodgeTrailHead - points.back().position) > 0.001f)
    {
        const float age = m_dodgeBlocked ? m_afterimages[m_afterimageCount - 1].age
            : std::max(0.0f, m_dodgeAge - m_dodgeSettings.moveTime);
        points.push_back({ m_dodgeTrailHead, afterimageOpacity(age, m_dodgeSettings.afterimageTime) });
    }

    // 履歴の各区間から、中央が明るく両端へ薄れる帯の頂点を作る。
    std::vector<VERTEX_3D> vertices;
    vertices.reserve(m_afterimages.size() * dodgeTrailStreams.size() * dodgeTrailVerticesPerSegment);
    const Vector3 right{ m_dodgeDirection.z, 0, -m_dodgeDirection.x };
    const Vector3 color = m_perfectFlash > 0.0f ? Vector3{ 1.0f, 0.78f, 0.20f } : Vector3{ 0.12f, 0.78f, 1.0f };
    const auto vertex = [&](const Vector3& position, float intensity)
    {
        VERTEX_3D value{};
        value.Position = { position.x, position.y, position.z };
        value.Normal = { 0, 1, 0 };
        value.Diffuse = { color.x * intensity, color.y * intensity, color.z * intensity, 1.0f };
        return value;
    };

    for (size_t i = 1; i < points.size(); ++i)
    {
        const Vector3 travel = points[i].position - points[i - 1].position;
        if (VectorMag(travel) < 0.001f)
        {
            continue;
        }

        for (const auto& stream : dodgeTrailStreams)
        {
            const Vector3 offset = right * stream.offset + Vector3{ 0, stream.height, 0 };
            const Vector3 start = points[i - 1].position + offset;
            const Vector3 end = points[i].position + offset;
            Vector3 side = crossProduct(travel, camera->GetPosition() - (start + end) * 0.5f);
            if (VectorMag(side) < 0.001f)
            {
                side = { 0, 1, 0 };
            }
            side.normalize();

            const float startWidth = stream.width * (0.25f + 0.75f * points[i - 1].opacity);
            const float endWidth = stream.width * (0.25f + 0.75f * points[i].opacity);
            const auto startCenter = vertex(start, points[i - 1].opacity * 0.85f);
            const auto endCenter = vertex(end, points[i].opacity * 0.85f);

            for (const float sign : { -1.0f, 1.0f })
            {
                const auto startEdge = vertex(start + side * (sign * startWidth), 0.0f);
                const auto endEdge = vertex(end + side * (sign * endWidth), 0.0f);
                vertices.insert(vertices.end(), { startCenter, startEdge, endCenter, endCenter, startEdge, endEdge });
            }
        }
    }

    if (vertices.empty())
    {
        return;
    }

    // 完成した頂点を一度だけ転送し、深度書き込みなしの加算合成で描く。
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_dodgeTrailVertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        return;
    }

    memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(VERTEX_3D));
    context->Unmap(m_dodgeTrailVertices.Get(), 0);

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
    context->RSGetState(previousRasterizer.GetAddressOf());
    context->RSSetState(m_dodgeTrailRasterizer.Get());

    const float factor[4] = { 1, 1, 1, 1 };
    context->OMSetBlendState(m_afterimageBlend.Get(), factor, 0xffffffff);
    Renderer::SetWorldMatrix(XMMatrixIdentity());
    MATERIAL material{};
    material.Diffuse = { 1, 1, 1, 1 };
    material.AlphaBlend = TRUE;
    Renderer::SetMaterial(material);

    constexpr UINT stride = sizeof(VERTEX_3D);
    constexpr UINT offset = 0;
    context->IASetVertexBuffers(0, 1, m_dodgeTrailVertices.GetAddressOf(), &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(static_cast<UINT>(vertices.size()), 0);
    context->RSSetState(previousRasterizer.Get());
}
