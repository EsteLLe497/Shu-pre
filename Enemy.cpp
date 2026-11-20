#include "main.h"
#include "renderer.h"
#include "Enemy.h"
#include "manager.h"
#include "Player.h"
#include "MeshField.h"
#include "SceneAudio.h"
#include "SceneEditor.h"
#include <cmath>
#include <cfloat>

namespace
{
    const std::vector<VERTEX_3D>& bodyMesh()
    {
        static const auto mesh = []
        {
            constexpr int latitudeCount = 12;
            constexpr int longitudeCount = 20;
            std::vector<VERTEX_3D> vertices;
            vertices.reserve(latitudeCount * longitudeCount * 6);
            const auto vertex = [](int latitude, int longitude)
            {
                const float theta = XM_PI * latitude / latitudeCount;
                const float phi = XM_2PI * longitude / longitudeCount;
                VERTEX_3D result{};
                result.Position = { sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi) };
                result.Normal = result.Position;
                result.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
                return result;
            };
            for (int latitude = 0; latitude < latitudeCount; ++latitude)
            {
                for (int longitude = 0; longitude < longitudeCount; ++longitude)
                {
                    const auto a = vertex(latitude, longitude);
                    const auto b = vertex(latitude, longitude + 1);
                    const auto c = vertex(latitude + 1, longitude);
                    const auto d = vertex(latitude + 1, longitude + 1);
                    if (latitude != 0) vertices.insert(vertices.end(), { a, b, c });
                    if (latitude + 1 != latitudeCount) vertices.insert(vertices.end(), { c, b, d });
                }
            }
            return vertices;
        }();
        return mesh;
    }

    float SmoothLerp(float from, float to, float t)
    {
        t = std::max(0.0f, std::min(t, 1.0f));
        return from + (to - from) * t * t * (3.0f - 2.0f * t);
    }
}

void Enemy::Init()
{
    m_Layer = 1;
    m_Rotation.y = XM_PI;

    std::vector<VERTEX_3D> vertices = bodyMesh();
    m_VertexCount = static_cast<UINT>(vertices.size());
    const auto ringVertex = [](float radius, UINT index)
    {
        const float angle = XM_2PI * index / (RingVertexCount / 6);
        VERTEX_3D result{};
        result.Position = { radius * cosf(angle), 0.0f, radius * sinf(angle) };
        result.Normal = { 0.0f, 1.0f, 0.0f };
        result.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
        return result;
    };
    for (UINT i = 0; i < RingVertexCount / 6; ++i)
    {
        const auto a = ringVertex(0.91f, i);
        const auto b = ringVertex(0.91f, i + 1);
        const auto c = ringVertex(1.0f, i);
        const auto d = ringVertex(1.0f, i + 1);
        vertices.insert(vertices.end(), { a, b, c, c, b, d });
    }

    D3D11_BUFFER_DESC description{};
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.ByteWidth = static_cast<UINT>(sizeof(VERTEX_3D) * vertices.size());
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices.data();
    const HRESULT result = Renderer::GetDevice()->CreateBuffer(
        &description, &data, m_VertexBuffer.GetAddressOf());
    assert(SUCCEEDED(result));
    if (FAILED(result)) return;

    Renderer::CreateVertexShader(m_VertexShader.GetAddressOf(), m_VertexLayout.GetAddressOf(),
        "shader\\litTextureVS.cso");
    Renderer::CreatePixelShader(m_PixelShader.GetAddressOf(), "shader\\litTexturePS.cso");
}

void Enemy::Uninit()
{
    if (m_character) m_character->Uninit();
    m_character.reset();
    m_VertexBuffer.Reset();
    m_VertexLayout.Reset();
    m_VertexShader.Reset();
    m_PixelShader.Reset();
    GameObject::Uninit();
}

void Enemy::Configure(bool midBoss)
{
    m_MidBoss = midBoss;
    m_attackSettings = defaultAttackSettings(midBoss);
    const float size = midBoss ? 1.55f : 0.75f;
    SetScale({ size, size, size });
}

Enemy::AttackSettings Enemy::defaultAttackSettings(bool midBoss)
{
    AttackSettings settings;
    if (midBoss)
    {
        settings.detectionDistance = 7.0f;
        settings.attackInterval = 0.0f;
        settings.punchStartRange = 2.65f;
    }
    return settings;
}

float Enemy::attackStartRange(Motion motion) const
{
    switch (motion)
    {
    case Motion::Punch:
        return m_MidBoss ? m_attackSettings.punchStartRange
            : m_attackSettings.punchStartRange * ((1.8f * m_Scale.x + 0.20f) / 1.55f);
    case Motion::Slam: return m_attackSettings.slamStartRange;
    case Motion::Charge: return m_attackSettings.chargeStartRange;
    default: return 0.0f;
    }
}

Enemy::AttackPhase Enemy::attackPhase(Motion motion)
{
    switch (motion)
    {
    case Motion::Punch: return { "Punch swing", PunchWindupEnd, PunchStrikeEnd };
    case Motion::Slam: return { "Shockwave effect", SlamImpact, SlamImpact + ShockwaveDuration };
    case Motion::Charge: return { "Forward charge", ChargeWindup, ChargeEnd };
    default: return {};
    }
}

float Enemy::shockwaveRadiusAt(float time) const
{
    const float progress = std::max(0.0f, std::min((time - SlamImpact) / ShockwaveDuration, 1.0f));
    return m_attackSettings.shockwaveRadius * ((1.0f + 2.25f * progress) / 3.25f);
}

float Enemy::chargeOffsetAt(float time) const
{
    const float retreat = ChargeRetreatSpeed * std::max(0.0f, std::min(time, ChargeWindup));
    const float dashTime = std::max(0.0f, std::min(time, ChargeEnd) - ChargeWindup);
    const float progress = dashTime / (ChargeEnd - ChargeWindup);
    return m_attackSettings.chargeDistance * progress - retreat;
}

bool Enemy::MoveOnGround(const Vector3& displacement, const Player* player)
{
    Vector3 next = m_Position + displacement;
    const MeshField* field = Manager::GetGameObj<MeshField>();
    if (field && !field->tryGetHeight(next.x, next.z, next.y)) return false;

    const auto blocks = [&](const Vector3& position, float otherRadius)
    {
        if (fabsf(position.y - next.y) > 1.3f) return false;
        const float nextDistance = hypotf(next.x - position.x, next.z - position.z);
        const float currentDistance = hypotf(m_Position.x - position.x, m_Position.z - position.z);
        return nextDistance < BodyRadius() + otherRadius && nextDistance < currentDistance;
    };
    if (m_MeshCollider && player && blocks(player->GetPosition(), 0.7f)) return false;
    for (const Enemy* other : Manager::GetGameObjs<Enemy>())
        if (m_MeshCollider && other != this && other->HasMeshCollider()
            && !other->IsAirborne() && blocks(other->GetPosition(), other->BodyRadius())) return false;
    if (SceneEditor::blocksEnemy(next, BodyRadius())) return false;
    m_Position = next;
    return true;
}

void Enemy::Update()
{
    const float dt = Manager::animationDeltaSeconds();
    Player* player = Manager::GetGameObj<Player>();
    const Vector3 toPlayer = player ? player->GetPosition() - m_Position : Vector3{ 0.0f, 0.0f, 0.0f };
    const float horizontalDistance = hypotf(toPlayer.x, toPlayer.z);
    const bool detected = player && hypotf(horizontalDistance, toPlayer.y) <= m_attackSettings.detectionDistance;
    m_PunchCooldown = std::max(0.0f, m_PunchCooldown - dt);
    m_attackCooldown = std::max(0.0f, m_attackCooldown - dt);

    float facingError = 0.0f;
    const bool canTurn = m_Attack != Attack::Charge || m_AttackTime + dt <= ChargeWindup;
    if (canTurn && player && horizontalDistance > 0.001f)
    {
        const float targetYaw = atan2f(toPlayer.x, toPlayer.z);
        const float turn = XMScalarModAngle(targetYaw - m_Rotation.y);
        const float maxTurn = TurnSpeed * dt;
        const float yawStep = std::max(-maxTurn, std::min(turn, maxTurn));
        m_Rotation.y = XMScalarModAngle(m_Rotation.y + yawStep);
        facingError = turn - yawStep;
    }

    bool moving = false;
    if (m_Attack != Attack::None)
    {
        const float previousTime = m_AttackTime;
        const Vector3 previousPosition = m_Position;
        m_AttackTime += dt;
        const auto motion = static_cast<Motion>(static_cast<int>(m_Attack) + 1);
        const float duration = motionDuration(motion);
        const auto& markers = m_motionSounds[static_cast<size_t>(motion)];
        if (!markers.empty())
            SceneAudio::playMarkers(markers, previousTime, std::min(m_AttackTime, duration));
        else
        {
            if (m_Attack == Attack::Punch && previousTime < 0.43f && m_AttackTime >= 0.43f)
                SceneAudio::play(SceneAudio::Cue::Punch);
            if (m_Attack == Attack::Slam && previousTime < SlamImpact && m_AttackTime >= SlamImpact)
                SceneAudio::play(SceneAudio::Cue::Slam);
            if (m_Attack == Attack::Charge && previousTime < ChargeWindup && m_AttackTime >= ChargeWindup)
                SceneAudio::play(SceneAudio::Cue::Charge);
        }
        if (m_Attack == Attack::Charge)
        {
            const Vector3 forward = GetFoward();
            const float retreatTime = std::max(0.0f, std::min(m_AttackTime, ChargeWindup) - previousTime);
            if (retreatTime > 0.0f)
                MoveOnGround(forward * (-ChargeRetreatSpeed * retreatTime), player);
            const float dashTime = std::min(m_AttackTime, ChargeEnd) - std::max(previousTime, ChargeWindup);
            if (dashTime > 0.0f && !m_ChargeBlocked && m_attackSettings.chargeDistance > 0.0f)
            {
                const float distance = m_attackSettings.chargeDistance * (dashTime / (ChargeEnd - ChargeWindup));
                // Test along the path so a higher configured speed cannot skip obstacles.
                const int steps = std::max(1, static_cast<int>(ceilf(distance / 0.25f)));
                const Vector3 step = forward * (distance / steps);
                for (int i = 0; i < steps && !m_ChargeBlocked; ++i)
                    m_ChargeBlocked = !MoveOnGround(step, player);
            }
        }
        if (player) submitAttackHit(*player, previousTime, previousPosition);
        if (m_AttackTime >= duration)
        {
            if (m_Attack == Attack::Punch) m_LeftHand = !m_LeftHand;
            m_Attack = Attack::None;
        }
    }
    else if (detected)
    {
        const auto nextMotion = static_cast<Motion>(static_cast<int>(m_NextAttack) + 1);
        const float startRange = attackStartRange(nextMotion);
        if (horizontalDistance <= startRange)
        {
            if (fabsf(facingError) < 0.35f && fabsf(toPlayer.y) < 1.6f
                && m_attackCooldown <= 0.0f
                && (m_NextAttack != Attack::Punch || m_PunchCooldown <= 0.0f))
            {
                m_Attack = m_NextAttack;
                m_AttackTime = 0.0f;
                m_attackHitId = Player::newAttackId();
                const auto motion = static_cast<Motion>(static_cast<int>(m_Attack) + 1);
                SceneAudio::playMarkers(m_motionSounds[static_cast<size_t>(motion)], -1.0f, 0.0f);
                m_ChargeBlocked = false;
                m_attackCooldown = m_attackSettings.attackInterval;
                if (m_MidBoss && m_Attack == Attack::Punch)
                    m_PunchCooldown = m_attackSettings.punchRepeatInterval;
                if (m_MidBoss)
                    m_NextAttack = m_Attack == Attack::Punch ? Attack::Slam
                        : m_Attack == Attack::Slam ? Attack::Charge : Attack::Punch;
            }
        }
        else
        {
            const float step = std::min(ChaseSpeed * dt, horizontalDistance - (startRange - 0.05f));
            if (step > 0.0f)
                moving = MoveOnGround(Vector3{ toPlayer.x, 0.0f, toPlayer.z } * (step / horizontalDistance), player);
        }
    }
    const float gaitTarget = moving ? 1.0f : 0.0f;
    m_GaitWeight += (gaitTarget - m_GaitWeight) * std::min(1.0f, 10.0f * dt);
    m_GaitPhase = XMScalarModAngle(m_GaitPhase + 8.5f * dt);
    const auto motion = m_Attack == Attack::None ? (moving ? Motion::Move : Motion::Idle)
        : static_cast<Motion>(static_cast<int>(m_Attack) + 1);
    if (motion == Motion::Idle || motion == Motion::Move)
    {
        const bool changed = motion != m_currentMotion;
        const float previous = changed ? -1.0f : m_characterTime;
        const float duration = motionDuration(motion);
        const float next = changed ? 0.0f : m_characterTime + dt;
        m_characterTime = fmodf(next, duration);
        SceneAudio::playMarkers(m_motionSounds[static_cast<size_t>(motion)], previous, m_characterTime,
            next >= duration ? duration : 0.0f);
    }
    else m_characterTime = m_AttackTime;
    if (motion != m_currentMotion || motionClip(motion))
        sampleCharacterMotion(motion, m_characterTime);
    m_currentMotion = motion;
    GameObject::Update();
}

float Enemy::AttackPose(float ready, float strike) const
{
    if (poseAttack() != Attack::Punch) return 0.0f;
    constexpr float recoveryEnd = 0.87f;
    if (poseTime() < PunchWindupEnd)
        return SmoothLerp(0.0f, ready, poseTime() / PunchWindupEnd);
    if (poseTime() < PunchStrikeEnd)
        return SmoothLerp(ready, strike, (poseTime() - PunchWindupEnd) / (PunchStrikeEnd - PunchWindupEnd));
    return SmoothLerp(strike, 0.0f, (poseTime() - PunchStrikeEnd) / (recoveryEnd - PunchStrikeEnd));
}

float Enemy::JumpHeight() const
{
    if (poseAttack() != Attack::Slam || poseTime() <= SlamWindup || poseTime() >= SlamImpact) return 0.0f;
    const float t = (poseTime() - SlamWindup) / (SlamImpact - SlamWindup);
    return 8.8f * t * (1.0f - t);
}

void Enemy::DrawPart(const Vector3& scale, const XMMATRIX& transform, const XMFLOAT4& color, bool shockwave)
{
    const XMMATRIX world = XMMatrixScaling(scale.x, scale.y, scale.z) * transform;
    Renderer::SetWorldMatrix(world);
    MATERIAL material{};
    material.Diffuse = color;
    material.Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.TextureEnable = FALSE;
    Renderer::SetMaterial(material);
    Renderer::GetDeviceContext()->Draw(shockwave ? RingVertexCount : m_VertexCount,
        shockwave ? m_VertexCount : 0);
}

void Enemy::Draw()
{
    if (!m_VertexBuffer || !m_VertexShader || !m_PixelShader || !m_VertexLayout) return;
    auto* context = Renderer::GetDeviceContext();
    context->IASetInputLayout(m_VertexLayout.Get());
    context->VSSetShader(m_VertexShader.Get(), nullptr, 0);
    context->PSSetShader(m_PixelShader.Get(), nullptr, 0);
    const UINT stride = sizeof(VERTEX_3D);
    const UINT offset = 0;
    ID3D11Buffer* buffer = m_VertexBuffer.Get();
    context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (m_character) m_character->drawAt(characterWorld());
    else
        VisitBodyParts([&](const Vector3& scale, const XMMATRIX& transform, const XMFLOAT4& color, bool)
        {
            DrawPart(scale, transform, color);
        });
    if (poseAttack() == Attack::Slam && poseTime() >= SlamImpact)
    {
        context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
        const float t = (poseTime() - SlamImpact) / ShockwaveDuration;
        const float radius = shockwaveRadiusAt(poseTime());
        DrawPart({ radius, 1.0f, radius },
            XMMatrixTranslation(m_Position.x, m_Position.y + 0.065f, m_Position.z),
            { 1.0f, 0.88f - 0.50f * t, 0.25f - 0.15f * t, 1.0f }, true);
    }
}

void Enemy::VisitBodyParts(const std::function<void(const Vector3&, const XMMATRIX&, const XMFLOAT4&, bool)>& visit,
    const std::function<void(const Vector3&, float)>& fist)
{
    const float attackSide = m_LeftHand ? -1.0f : 1.0f;
    const float gaitWeight = m_previewMotion == Motion::Count ? m_GaitWeight : (m_previewMotion == Motion::Move ? 1.0f : 0.0f);
    const float phase = m_previewMotion == Motion::Count ? m_GaitPhase : poseTime() * 8.5f;
    const float gait = sinf(phase) * gaitWeight;
    const float bob = 0.12f * fabsf(sinf(phase)) * gaitWeight;
    float extraPitch = 0.0f;
    float crouch = 0.0f;
    float specialSweep = 0.0f;
    float specialLift = 0.0f;
    if (poseAttack() == Attack::Slam)
    {
        const float prepare = SmoothLerp(0.0f, 1.0f, poseTime() / SlamWindup);
        const float downswing = SmoothLerp(0.0f, 1.0f, (poseTime() - 0.62f) / (SlamImpact - 0.62f));
        const float recover = SmoothLerp(1.0f, 0.0f, (poseTime() - SlamImpact) / ShockwaveDuration);
        crouch = -0.24f * prepare * (1.0f - SmoothLerp(0.0f, 1.0f, (poseTime() - SlamWindup) / 0.18f))
            - 0.27f * downswing * recover;
        extraPitch = (-0.20f * prepare + 0.62f * downswing) * recover;
        specialLift = (1.93f * prepare - 2.85f * downswing) * recover;
        specialSweep = (0.24f * prepare + 0.83f * downswing) * recover;
    }
    if (poseAttack() == Attack::Charge)
    {
        const float brace = SmoothLerp(0.0f, 1.0f, poseTime() / ChargeWindup)
            * SmoothLerp(1.0f, 0.0f, (poseTime() - ChargeEnd) / 0.33f);
        const float launch = SmoothLerp(0.0f, 1.0f, (poseTime() - ChargeWindup) / 0.12f);
        extraPitch = (0.19f + 0.41f * launch) * brace;
        crouch = -0.22f * brace;
        specialSweep = (0.40f - 1.12f * launch) * brace;
    }
    const XMMATRIX root = GetMatrx() * XMMatrixTranslation(0.0f, JumpHeight(), 0.0f);
    const XMMATRIX torso = XMMatrixRotationRollPitchYaw(
        0.12f * gaitWeight + AttackPose(-0.17f, 0.34f) + extraPitch,
        attackSide * AttackPose(0.32f, -0.40f),
        0.09f * gait + attackSide * AttackPose(-0.16f, 0.19f))
        * XMMatrixTranslation(0.0f, 1.05f + bob + crouch, AttackPose(-0.08f, 0.21f))
        * root;
    const XMFLOAT4 bodyColor = m_MidBoss ? XMFLOAT4{ 0.42f, 0.09f, 0.27f, 1.0f }
        : XMFLOAT4{ 0.65f, 0.18f, 0.15f, 1.0f };
    visit({ m_MidBoss ? 0.80f : 0.70f, 1.05f, 0.55f }, torso, bodyColor, true);

    for (int hand = 0; hand < 2; ++hand)
    {
        const bool left = hand == 0;
        const float side = left ? -1.0f : 1.0f;
        const bool strikingHand = left == m_LeftHand;
        const float sweep = 0.35f * side * gait + specialSweep
            + (strikingHand ? AttackPose(-0.78f, 1.27f) : AttackPose(0.23f, -0.40f));
        const float lift = -0.32f + specialLift
            + (strikingHand ? AttackPose(1.03f, 0.22f) : AttackPose(0.10f, -0.14f));
        const XMMATRIX arm = XMMatrixRotationRollPitchYaw(0.0f, -side * sweep, side * lift)
            * XMMatrixTranslation(side * 0.48f, 0.24f, 0.03f) * torso;
        visit({ 0.47f, 0.24f, 0.24f }, XMMatrixTranslation(side * 0.34f, 0.0f, 0.0f) * arm,
            { 0.72f, 0.24f, 0.15f, 1.0f }, true);
        const float fistRadius = m_MidBoss ? 0.54f : 0.43f;
        if (fist && strikingHand)
        {
            XMFLOAT3 center;
            XMStoreFloat3(&center, XMVector3TransformCoord(XMVectorSet(side * 0.83f, 0, 0, 1), arm));
            fist({ center.x, center.y, center.z }, fistRadius * std::max({ m_Scale.x, m_Scale.y, m_Scale.z }));
        }
        visit({ fistRadius, fistRadius, fistRadius }, XMMatrixTranslation(side * 0.83f, 0.0f, 0.0f) * arm,
            { 1.0f, 0.62f, 0.16f, 1.0f }, true);

        visit({ 0.105f, 0.14f, 0.07f }, XMMatrixTranslation(side * 0.23f, 0.40f, 0.48f) * torso,
            { 1.0f, 0.87f, 0.52f, 1.0f }, false);
        visit({ 0.043f, 0.075f, 0.035f }, XMMatrixTranslation(side * 0.23f, 0.39f, 0.54f) * torso,
            { 0.13f, 0.06f, 0.04f, 1.0f }, false);
        if (m_MidBoss)
        {
            visit({ 0.16f, 0.41f, 0.16f }, XMMatrixRotationZ(-side * 0.36f)
                * XMMatrixTranslation(side * 0.49f, 0.87f, 0.0f) * torso,
                { 0.72f, 0.36f, 0.16f, 1.0f }, true);
            visit({ 0.22f, 0.065f, 0.09f }, XMMatrixRotationZ(side * 0.30f)
                * XMMatrixTranslation(side * 0.24f, 0.48f, 0.52f) * torso, bodyColor, true);
        }
    }
}

void Enemy::VisitColliderParts(const std::function<void(const XMMATRIX&)>& visit)
{
    VisitBodyParts([&](const Vector3& scale, const XMMATRIX& transform, const XMFLOAT4&, bool solid)
    {
        if (solid) visit(XMMatrixScaling(scale.x, scale.y, scale.z) * transform);
    });
}

bool Enemy::IntersectsMesh(const BoundingSphere& sphere)
{
    if (!m_MeshCollider) return false;
    if (m_character)
    {
        size_t part;
        return queryHurtbox({ sphere.Center.x, sphere.Center.y, sphere.Center.z }, sphere.Radius, part);
    }
    bool hit = false;
    const auto& vertices = bodyMesh();
    VisitColliderParts([&](const XMMATRIX& world)
    {
        if (hit) return;
        BoundingBox broad;
        BoundingBox({ 0, 0, 0 }, { 1, 1, 1 }).Transform(broad, world);
        if (!broad.Intersects(sphere)) return;
        const XMVECTOR center = XMVector3TransformCoord(XMLoadFloat3(&sphere.Center), XMMatrixInverse(nullptr, world));
        if (XMVectorGetX(XMVector3LengthSq(center)) < 0.95f)
        {
            hit = true;
            return;
        }
        for (size_t i = 0; i < vertices.size(); i += 3)
        {
            const auto a = XMVector3TransformCoord(XMLoadFloat3(&vertices[i].Position), world);
            const auto b = XMVector3TransformCoord(XMLoadFloat3(&vertices[i + 1].Position), world);
            const auto c = XMVector3TransformCoord(XMLoadFloat3(&vertices[i + 2].Position), world);
            if (sphere.Intersects(a, b, c))
            {
                hit = true;
                break;
            }
        }
    });
    return hit;
}

bool Enemy::RaycastMesh(FXMVECTOR origin, FXMVECTOR direction, float& distance)
{
    bool found = false;
    distance = FLT_MAX;
    if (m_character)
    {
        for (size_t i = 0; i < hurtboxCount(); ++i)
        {
            const auto pose = hurtboxPose(i);
            const auto segment = pose.end - pose.start;
            const int steps = static_cast<int>(std::max(1.0f, std::min(32.0f,
                ceilf(segment.lenght() / std::max(pose.radius, 0.001f)))));
            for (int sample = 0; sample <= steps; ++sample)
            {
                const auto center = pose.start + segment * (static_cast<float>(sample) / steps);
                float hit;
                if (BoundingSphere({ center.x, center.y, center.z }, pose.radius).Intersects(origin, direction, hit)
                    && hit >= 0 && hit < distance)
                {
                    found = true;
                    distance = hit;
                }
            }
        }
        return found;
    }
    const auto& vertices = bodyMesh();
    VisitColliderParts([&](const XMMATRIX& world)
    {
        const XMMATRIX inverse = XMMatrixInverse(nullptr, world);
        const XMVECTOR localOrigin = XMVector3TransformCoord(origin, inverse);
        const XMVECTOR localVector = XMVector3TransformNormal(direction, inverse);
        const float length = XMVectorGetX(XMVector3Length(localVector));
        const XMVECTOR localDirection = localVector / length;
        float broadDistance;
        if (!BoundingSphere({ 0, 0, 0 }, 1.001f).Intersects(localOrigin, localDirection, broadDistance)) return;
        for (size_t i = 0; i < vertices.size(); i += 3)
        {
            float hit;
            if (TriangleTests::Intersects(localOrigin, localDirection, XMLoadFloat3(&vertices[i].Position),
                XMLoadFloat3(&vertices[i + 1].Position), XMLoadFloat3(&vertices[i + 2].Position), hit))
            {
                found = true;
                distance = std::min(distance, hit / length);
            }
        }
    });
    return found;
}

bool Enemy::loadCharacter(const std::string& path)
{
    if (path == m_characterPath) return true;
    if (path.empty())
    {
        if (m_character) m_character->Uninit();
        m_character.reset();
        m_characterPath.clear();
        m_capsules.clear();
        m_adjustments.clear();
        m_hasCharacterMotion = false;
        m_motionFiles = {};
        m_motionClips = {};
        return true;
    }

    auto model = std::make_unique<AnimationModel>(this);
    if (!model->tryLoad(path.c_str())) return false;
    std::vector<BoneCapsule> capsules;
    float bottom = FLT_MAX, top = -FLT_MAX;
    for (const auto& name : model->dominantSkinBones(32))
    {
        BoneCapsule capsule;
        if (!model->fitBoneCapsule(name.c_str(), capsule)) continue;
        const float halfHeight = fabsf(capsule.axis.y) * capsule.halfLength + capsule.radius;
        bottom = std::min(bottom, capsule.center.y - halfHeight);
        top = std::max(top, capsule.center.y + halfHeight);
        capsules.push_back(std::move(capsule));
    }
    const float height = top - bottom;
    if (capsules.empty() || !std::isfinite(height) || height <= 0.001f)
    {
        model->Uninit();
        return false;
    }
    const float scale = 1.75f / height;
    for (auto& part : capsules)
    {
        part.center.x *= scale;
        part.center.y *= scale;
        part.center.z *= scale;
        part.radius *= scale;
        part.halfLength *= scale;
        XMStoreFloat4x4(&part.inverseBindSkin, XMMatrixScaling(1 / scale, 1 / scale, 1 / scale)
            * XMLoadFloat4x4(&part.inverseBindSkin));
    }
    model->setSkeletonUnitScale(scale);
    const bool hasMotion = model->loadEmbeddedClip("enemy_embedded");
    if (!hasMotion || !model->sampleClip("enemy_embedded", 0.0, AnimationModel::RootTravel::lockTranslation))
        model->applyBindPose();

    if (m_character) m_character->Uninit();
    m_character = std::move(model);
    m_characterPath = path;
    m_capsules = std::move(capsules);
    m_adjustments.assign(m_capsules.size(), {});
    m_characterGroundOffset = -bottom * scale;
    m_characterTime = 0.0f;
    m_hasCharacterMotion = hasMotion;
    m_motionFiles = {};
    m_motionClips = {};
    m_currentMotion = Motion::Count;
    return true;
}

XMMATRIX Enemy::characterWorld()
{
    return XMMatrixRotationY(XM_PI) * XMMatrixTranslation(0, m_characterGroundOffset, 0)
        * GetMatrx() * XMMatrixTranslation(0, JumpHeight(), 0);
}

Enemy::HurtboxPose Enemy::hurtboxPose(size_t i)
{
    const auto& part = m_capsules[i];
    const auto& edit = m_adjustments[i];
    const auto center = XMLoadFloat3(&part.center) + XMVectorSet(edit.offset.x, edit.offset.y, edit.offset.z, 0);
    const auto axis = XMLoadFloat3(&part.axis);
    const auto halfLength = std::max(0.0f, (part.halfLength + part.radius) * edit.lengthScale - part.radius);
    const auto segment = axis * halfLength;
    const XMMATRIX world = m_character->capsulePose(part) * characterWorld();
    XMFLOAT3 start, end;
    XMStoreFloat3(&start, XMVector3TransformCoord(center - segment, world));
    XMStoreFloat3(&end, XMVector3TransformCoord(center + segment, world));
    const float scale = std::max({ XMVectorGetX(XMVector3Length(world.r[0])),
        XMVectorGetX(XMVector3Length(world.r[1])), XMVectorGetX(XMVector3Length(world.r[2])) });
    return { { start.x, start.y, start.z }, { end.x, end.y, end.z }, part.radius * edit.radiusScale * scale };
}

bool Enemy::queryHurtbox(const Vector3& center, float radius, size_t& part)
{
    if (!m_MeshCollider) return false;
    float best = FLT_MAX;
    for (size_t i = 0; i < hurtboxCount(); ++i)
    {
        const auto pose = hurtboxPose(i);
        auto segment = pose.end - pose.start;
        auto delta = center - pose.start;
        const float lengthSquared = Vector3::dot(segment, segment);
        const float t = lengthSquared > 0.000001f
            ? std::max(0.0f, std::min(1.0f, Vector3::dot(delta, segment) / lengthSquared)) : 0.0f;
        auto separation = delta - segment * t;
        const float gap = sqrtf(Vector3::dot(separation, separation)) - pose.radius;
        if (gap > radius || gap >= best) continue;
        best = gap;
        part = i;
    }
    return best != FLT_MAX;
}

Enemy::Attack Enemy::poseAttack() const
{
    if (m_previewMotion == Motion::Count) return m_Attack;
    return m_previewMotion >= Motion::Punch ? static_cast<Attack>(static_cast<int>(m_previewMotion) - 1) : Attack::None;
}

float Enemy::poseTime() const
{
    return m_previewMotion == Motion::Count ? m_AttackTime : m_previewTime;
}

const char* Enemy::motionClip(Motion motion) const
{
    if (!m_character || motion == Motion::Count) return nullptr;
    const auto& clip = m_motionClips[static_cast<size_t>(motion)];
    if (!clip.empty()) return clip.c_str();
    return m_hasCharacterMotion ? "enemy_embedded" : nullptr;
}

float Enemy::motionDuration(Motion motion) const
{
    switch (motion)
    {
    case Motion::Punch: return SwingDuration;
    case Motion::Slam: return SlamImpact + ShockwaveDuration;
    case Motion::Charge: return ChargeEnd + 0.33f;
    default: break;
    }
    const char* clip = motionClip(motion);
    if (clip) return std::max(0.1f, static_cast<float>(m_character->clipDuration(clip)));
    return motion == Motion::Move ? XM_2PI / 8.5f : 1.0f;
}

void Enemy::sampleCharacterMotion(Motion motion, float time)
{
    if (!m_character) return;
    const char* clip = motionClip(motion);
    if (!clip) { m_character->applyBindPose(); return; }
    const bool loop = motion == Motion::Idle || motion == Motion::Move;
    const double sampleTime = loop ? time : time * m_character->clipDuration(clip) / motionDuration(motion);
    m_character->sampleBlendedClip(clip, sampleTime, 0.0f, AnimationModel::RootTravel::lockTranslation, false, loop);
}

bool Enemy::assignMotion(Motion motion, const std::string& file)
{
    const size_t index = static_cast<size_t>(motion);
    auto& oldFile = m_motionFiles[index];
    auto& oldClip = m_motionClips[index];
    if (oldFile == file) return true;
    if (!m_character) return false;
    std::string key;
    if (!file.empty())
    {
        const std::string base = "enemy_motion_" + std::to_string(index);
        key = base + (oldClip == base + "_A" ? "_B" : "_A");
        if (!m_character->LoadAnimation(file.c_str(), key.c_str())) return false;
    }
    if (!oldClip.empty()) m_character->unloadAnimation(oldClip.c_str());
    oldClip = std::move(key);
    oldFile = file;
    m_currentMotion = Motion::Count;
    return true;
}

void Enemy::setMotionSounds(Motion motion, const SceneAudio::Timeline& markers)
{
    m_motionSounds[static_cast<size_t>(motion)] = markers;
}

void Enemy::previewMotion(Motion motion, float time)
{
    m_previewMotion = motion;
    m_previewTime = time;
    sampleCharacterMotion(motion, time);
}

void Enemy::clearMotionPreview()
{
    if (m_previewMotion == Motion::Count) return;
    m_previewMotion = Motion::Count;
    sampleCharacterMotion(m_currentMotion == Motion::Count ? Motion::Idle : m_currentMotion, m_characterTime);
}

// Attack volumes are active only during the same strike phases shown by the editor.
void Enemy::submitAttackHit(Player& player, float previousTime, const Vector3& previousPosition)
{
    const auto motion = static_cast<Motion>(static_cast<int>(m_Attack) + 1);
    const auto phase = attackPhase(motion);
    if (!m_attackHitId || m_AttackTime < phase.start || previousTime >= phase.end) return;
    const float sampleTime = std::min(m_AttackTime, phase.end);
    if (m_Attack == Attack::Punch)
    {
        // Reuse the rendered fist transform, including scale and the active left/right hand.
        const float savedTime = m_AttackTime;
        const auto noDraw = [](const Vector3&, const XMMATRIX&, const XMFLOAT4&, bool) {};
        Vector3 from{ 0, 0, 0 }, to{ 0, 0, 0 };
        float radius = 0.0f;
        m_AttackTime = std::max(previousTime, phase.start);
        VisitBodyParts(noDraw, [&](const Vector3& p, float r) { from = p; radius = r; });
        m_AttackTime = sampleTime;
        VisitBodyParts(noDraw, [&](const Vector3& p, float) { to = p; });
        m_AttackTime = savedTime;
        if (m_character)
        {
            // Imported characters use the animated hand hurtbox instead of prototype limbs.
            const auto handIndex = [&]() -> size_t
            {
                for (size_t i = 0; i < m_capsules.size(); ++i)
                {
                    const auto& name = m_capsules[i].bone;
                    if (name.find("Hand") == std::string::npos && name.find("hand") == std::string::npos) continue;
                    const bool left = name.find("Left") != std::string::npos || name.find("left") != std::string::npos
                        || name.find("_L_") != std::string::npos;
                    if (left == m_LeftHand) return i;
                }
                return m_capsules.size();
            }();
            if (handIndex < m_capsules.size())
            {
                sampleCharacterMotion(motion, std::max(previousTime, phase.start));
                auto hand = hurtboxPose(handIndex);
                from = hand.end;
                sampleCharacterMotion(motion, sampleTime);
                hand = hurtboxPose(handIndex);
                to = hand.end;
                radius = std::max(0.15f * m_Scale.x, hand.radius);
            }
        }
        player.submitIncomingHit({ m_attackHitId, from, to, radius });
    }
    else if (m_Attack == Attack::Charge)
    {
        const Vector3 height{ 0, 0.9f * m_Scale.y, 0 };
        player.submitIncomingHit({ m_attackHitId, previousPosition + height, m_Position + height,
            BodyRadius() + 0.35f * m_Scale.x });
    }
    else if (m_Attack == Attack::Slam)
    {
        // Sweep the visible ground ring, leaving its hollow center safe and allowing jumps.
        const float from = shockwaveRadiusAt(std::max(previousTime, phase.start));
        const float to = shockwaveRadiusAt(sampleTime);
        const int steps = std::max(1, static_cast<int>(ceilf((to - from) / 0.15f)));
        constexpr int sides = 32;
        for (int step = 0; step <= steps; ++step)
        {
            const float radius = from + (to - from) * step / steps;
            for (int side = 0; side < sides; ++side)
            {
                const float a = XM_2PI * side / sides, b = XM_2PI * (side + 1) / sides;
                const Vector3 start = m_Position + Vector3{ sinf(a) * radius, 0.065f, cosf(a) * radius };
                const Vector3 end = m_Position + Vector3{ sinf(b) * radius, 0.065f, cosf(b) * radius };
                player.submitIncomingHit({ m_attackHitId, start, end, 0.12f });
            }
        }
    }
}
