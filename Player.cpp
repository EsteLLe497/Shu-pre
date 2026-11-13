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
#include "audio.h"

#include "Enemy.h"
#include "Tree.h"
#include "Box.h"
#include "Shadow.h"
#include "MeshField.h"

namespace
{
    constexpr float groundTolerance = 0.001f;
    constexpr float maxGroundDrop = 0.15f;
    constexpr float minLandingAirTime = 0.08f;
    constexpr float shadowOffset = 0.01f;
    constexpr float shadowSize = 1.5f;
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
    loadPlayerActions();
    updateLocomotion(0.0f, PlayerActionKind::idle);

    // シェーダー読込
    m_toonShader.init();

    //BGM
    m_JumpSE = AddGameComponent<Audio>(this);
    m_JumpSE->Load("asset\\Audio\\wan.wav");

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
    m_weapon.clear();
    m_toonShader.uninit();

    GameObject::Uninit();
}

// =========================================================
// プレイヤーの移動、衝突、モーションを更新
// =========================================================
void Player::Update()
{

    Vector3 oldPosition = m_Position;
    float dt = 1.0 / 60.0f;

    Vector3 rot = m_Child->GetRotation();
    rot.y += 1.0f * dt;
    m_Child->SetRotation(rot);

    const bool oldGround = m_Ground;
    m_Ground = false;

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

    const bool isMoving = VectorMag(moveDirection) > 0.0f;
    const bool isDashing = isMoving && Input::GetKeyPress(VK_SHIFT);

    // 踏ん張り中は移動入力で加速しない。
    const bool canMove = isMoving && !isLanding();

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
    if (oldGround && !isLanding() && Input::GetKeyTrigger(VK_SPACE))
    {
        m_Velocity.y = jumpSpeed;
        m_jumpedFromGround = true;
        playAction(isDashing ? PlayerActionKind::runJump : PlayerActionKind::jump);
        m_JumpSE->Play();
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

    //auto boxes = Manager::GetGameObjs<Box>();
    //for (auto box : boxes)
    //{
    //    Vector3 boxPosition = box->GetPosition();
    //    Vector3 boxScale = box->GetScale();

    //    if (boxPosition.x - boxScale.x < m_Position.x &&
    //        m_Position.x < boxPosition.x + boxScale.x &&
    //        boxPosition.z - boxScale.z < m_Position.z &&
    //        m_Position.z < boxPosition.z + boxScale.z)
    //    {
    //        if (boxPosition.y + boxScale.y < m_Position.y &&
    //            m_Position.y < boxPosition.y + boxScale.y * 2.0f)
    //        {
    //            //上面に衝突 top of the box
    //            m_Position.y = boxPosition.y + boxScale.y * 2.0f;
    //            m_Velocity.y = 0.0f;
    //            m_Ground = true;
    //        }
    //        else if (boxPosition.y - boxScale.y < m_Position.y &&
    //            m_Position.y < boxPosition.y + boxScale.y)
    //        {
    //            //側面に衝突
    //            m_Position.x = oldPosition.x;
    //            m_Position.z = oldPosition.z;
    //            m_Velocity.x = 0.0f;
    //            m_Velocity.z = 0.0f;
    //        }
    //    }
    //}

    //ボックスとの衝突判定
     auto boxes = Manager::GetGameObjs<Box>();
     for (auto box : boxes)
     {
         Vector3 boxPosition = box->GetPosition();
         Vector3 boxScale = box->GetScale();
		 Vector3 direction = m_Position - boxPosition;
         Vector3 axisX = box->GetRight();
         Vector3 axisZ = box->GetFoward();

		 float dotX = Vector3::dot(direction, axisX);
		 float dotZ = Vector3::dot(direction, axisZ);

         if(-boxScale.x < dotX && dotX < boxScale.x &&
            -boxScale.z < dotZ && dotZ < boxScale.z)
         {
             const float boxTop = boxPosition.y + boxScale.y * 2.0f;
             if (oldPosition.y >= boxTop - groundTolerance && m_Position.y <= boxTop &&
                 m_Velocity.y <= 0.0f)
             {
                 //上面に衝突 top of the box
                 m_Position.y = boxTop;
                 m_Velocity.y = 0.0f;
                 m_Ground = true;
             }
             else if (boxPosition.y - boxScale.y < m_Position.y &&
                      m_Position.y < boxPosition.y + boxScale.y)
             {
                 //側面に衝突
                 m_Position.x = oldPosition.x;
                 m_Position.z = oldPosition.z;
                 m_Velocity.x = 0.0f;
                 m_Velocity.z = 0.0f;
             }
		 }
     }

    auto enemies = Manager::GetGameObjs<Enemy>();

    for (auto enemy : enemies)
    {
        const float playerRadius = 0.7f;
        const float enemyRadius = 0.7f;

        Vector3 dir = m_Position - enemy->GetPosition();
        float length = dir.lenght();

        if (Collision::SphereVsSphere(m_Position, playerRadius, enemy->GetPosition(), enemyRadius))
        {
            if (length > 0.0f)
            {
                float overlap = (playerRadius + enemyRadius) - length;
                dir /= length;
                m_Position += dir * overlap;
            }

            break;
        }
    }

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
        if (m_jumpedFromGround)
        {
            if (m_playingActionKind == PlayerActionKind::jump || m_playingActionKind == PlayerActionKind::runJump)
                m_actionPlayback.stop();
        }
        else if (!oldGround && m_airborneTime >= minLandingAirTime && playAction(PlayerActionKind::land))
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
    const bool isIdle = m_Ground && !isMoving && Input::isGameplayActive() && !Input::hasGameplayActivity();
    if (m_yawning && !isIdle)
        m_actionPlayback.stop();
    if (!m_actionPlayback.active())
        m_yawning = false;

    if (m_actions.updateIdleTimer(isIdle && (!m_actionPlayback.active() || m_yawning), dt))
        m_yawning = playAction(PlayerActionKind::yawn);

    if (Input::GetKeyTrigger('J'))
    {
        Bullet* bullet = Manager::AddGameObj<Bullet>();
        bullet->SetPosition(m_Position);
        bullet->SetVelocity(GetFoward() * 20.0f);
        // 発射はできるが、着地の姿勢は最後まで再生する。
        if (m_Ground && !isLanding())
            playAction(PlayerActionKind::attack);
    }
    
    if (m_Ground)
    {
        //m_MoveAnimation += VectorMag(m_Velocity) * dt;
        //m_Scale.y += sinf(m_MoveAnimation * 3.0f) * 0.03f;
    }

    // 影を地表に置き、地形の外では表示しない。
    const float visibleShadowSize = hasGround ? shadowSize : 0.0f;
    m_Shadow->SetScale({ visibleShadowSize, visibleShadowSize, visibleShadowSize });
    m_Shadow->SetPosition({ m_Position.x, groundHeight + shadowOffset, m_Position.z });

    const auto kind = isMoving ? (isDashing ? PlayerActionKind::run : PlayerActionKind::walk)
        : PlayerActionKind::idle;
    // モーションと表情は実時間で進め、移動や衝突判定は固定間隔で更新する。
    const float animationDt = Manager::animationDeltaSeconds();
    updateLocomotion(animationDt, kind);
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
    if (m_playingAction.weapon.enabled && m_weaponTime >= 0.0f && m_weaponTime < m_playingAction.weapon.visualEnd())
    {
        m_toonShader.bindSurface();
        m_weapon.draw(m_playingAction.weapon, m_weaponTime, GetMatrx(), &m_weaponEndAnchor);
    }
    if (m_sampledActionTime >= 0.0f && m_playingAction.weapon.enabled)
    {
        const float age = m_sampledActionTime - m_playingAction.effectStart;
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
