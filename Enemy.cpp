#include "main.h"
#include "renderer.h"
#include "Enemy.h"
#include "manager.h"
#include "Player.h"
#include "MeshField.h"
#include "Box.h"
#include <cmath>

namespace
{
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
            vertices.insert(vertices.end(), { a, b, c, c, b, d });
        }
    }

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
    m_VertexBuffer.Reset();
    m_VertexLayout.Reset();
    m_VertexShader.Reset();
    m_PixelShader.Reset();
    GameObject::Uninit();
}

void Enemy::Configure(bool midBoss)
{
    m_MidBoss = midBoss;
    const float size = midBoss ? 1.55f : 0.75f;
    SetScale({ size, size, size });
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
    if (player && blocks(player->GetPosition(), 0.7f)) return false;
    for (const Enemy* other : Manager::GetGameObjs<Enemy>())
        if (other != this && !other->IsAirborne() && blocks(other->GetPosition(), other->BodyRadius())) return false;
    for (auto* box : Manager::GetGameObjs<Box>())
    {
        Vector3 offset = next - box->GetPosition();
        Vector3 right = box->GetRight();
        Vector3 forward = box->GetFoward();
        const Vector3& scale = box->GetScale();
        if (fabsf(offset.y) < scale.y
            && fabsf(Vector3::dot(offset, right)) < scale.x + BodyRadius()
            && fabsf(Vector3::dot(offset, forward)) < scale.z + BodyRadius()) return false;
    }
    m_Position = next;
    return true;
}

void Enemy::Update()
{
    const float dt = Manager::animationDeltaSeconds();
    const Player* player = Manager::GetGameObj<Player>();
    const Vector3 toPlayer = player ? player->GetPosition() - m_Position : Vector3{ 0.0f, 0.0f, 0.0f };
    const float horizontalDistance = hypotf(toPlayer.x, toPlayer.z);
    const float detectionRadius = m_MidBoss ? 7.0f : DetectionRadius;
    const bool detected = player && hypotf(horizontalDistance, toPlayer.y) <= detectionRadius;
    m_PunchCooldown = std::max(0.0f, m_PunchCooldown - dt);

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
        m_AttackTime += dt;
        float duration = SwingDuration;
        if (m_Attack == Attack::Slam)
            duration = SlamImpact + ShockwaveDuration;
        if (m_Attack == Attack::Charge)
        {
            duration = ChargeEnd + 0.33f;
            const Vector3 forward = GetFoward();
            const float retreatTime = std::max(0.0f, std::min(m_AttackTime, ChargeWindup) - previousTime);
            if (retreatTime > 0.0f)
                MoveOnGround(forward * (-2.7f * retreatTime), player);
            const float dashTime = std::min(m_AttackTime, ChargeEnd) - std::max(previousTime, ChargeWindup);
            if (dashTime > 0.0f && !m_ChargeBlocked)
                m_ChargeBlocked = !MoveOnGround(forward * (8.4f * dashTime), player);
        }
        if (m_AttackTime >= duration)
        {
            if (m_Attack == Attack::Punch) m_LeftHand = !m_LeftHand;
            m_Attack = Attack::None;
        }
    }
    else if (detected)
    {
        const float punchRange = m_MidBoss ? 2.65f : AttackRange * m_Scale.x + 0.20f;
        float startRange = punchRange;
        if (m_NextAttack == Attack::Slam) startRange = 2.95f;
        if (m_NextAttack == Attack::Charge) startRange = 5.2f;
        if (horizontalDistance <= startRange)
        {
            if (fabsf(facingError) < 0.35f && fabsf(toPlayer.y) < 1.6f
                && (m_NextAttack != Attack::Punch || m_PunchCooldown <= 0.0f))
            {
                m_Attack = m_NextAttack;
                m_AttackTime = 0.0f;
                m_ChargeBlocked = false;
                if (m_Attack == Attack::Punch) m_PunchCooldown = PunchInterval;
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
    GameObject::Update();
}

float Enemy::AttackPose(float ready, float strike) const
{
    if (m_Attack != Attack::Punch) return 0.0f;
    constexpr float windupEnd = 0.30f;
    constexpr float strikeEnd = 0.48f;
    constexpr float recoveryEnd = 0.87f;
    if (m_AttackTime < windupEnd)
        return SmoothLerp(0.0f, ready, m_AttackTime / windupEnd);
    if (m_AttackTime < strikeEnd)
        return SmoothLerp(ready, strike, (m_AttackTime - windupEnd) / (strikeEnd - windupEnd));
    return SmoothLerp(strike, 0.0f, (m_AttackTime - strikeEnd) / (recoveryEnd - strikeEnd));
}

float Enemy::JumpHeight() const
{
    if (m_Attack != Attack::Slam || m_AttackTime <= SlamWindup || m_AttackTime >= SlamImpact) return 0.0f;
    const float t = (m_AttackTime - SlamWindup) / (SlamImpact - SlamWindup);
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

    const float attackSide = m_LeftHand ? -1.0f : 1.0f;
    const float gait = sinf(m_GaitPhase) * m_GaitWeight;
    const float bob = 0.12f * fabsf(sinf(m_GaitPhase)) * m_GaitWeight;
    float extraPitch = 0.0f;
    float crouch = 0.0f;
    float specialSweep = 0.0f;
    float specialLift = 0.0f;
    if (m_Attack == Attack::Slam)
    {
        const float prepare = SmoothLerp(0.0f, 1.0f, m_AttackTime / SlamWindup);
        const float downswing = SmoothLerp(0.0f, 1.0f, (m_AttackTime - 0.62f) / (SlamImpact - 0.62f));
        const float recover = SmoothLerp(1.0f, 0.0f, (m_AttackTime - SlamImpact) / ShockwaveDuration);
        crouch = -0.24f * prepare * (1.0f - SmoothLerp(0.0f, 1.0f, (m_AttackTime - SlamWindup) / 0.18f))
            - 0.27f * downswing * recover;
        extraPitch = (-0.20f * prepare + 0.62f * downswing) * recover;
        specialLift = (1.93f * prepare - 2.85f * downswing) * recover;
        specialSweep = (0.24f * prepare + 0.83f * downswing) * recover;
    }
    if (m_Attack == Attack::Charge)
    {
        const float brace = SmoothLerp(0.0f, 1.0f, m_AttackTime / ChargeWindup)
            * SmoothLerp(1.0f, 0.0f, (m_AttackTime - ChargeEnd) / 0.33f);
        const float launch = SmoothLerp(0.0f, 1.0f, (m_AttackTime - ChargeWindup) / 0.12f);
        extraPitch = (0.19f + 0.41f * launch) * brace;
        crouch = -0.22f * brace;
        specialSweep = (0.40f - 1.12f * launch) * brace;
    }
    const XMMATRIX root = GetMatrx() * XMMatrixTranslation(0.0f, JumpHeight(), 0.0f);
    const XMMATRIX torso = XMMatrixRotationRollPitchYaw(
        0.12f * m_GaitWeight + AttackPose(-0.17f, 0.34f) + extraPitch,
        attackSide * AttackPose(0.32f, -0.40f),
        0.09f * gait + attackSide * AttackPose(-0.16f, 0.19f))
        * XMMatrixTranslation(0.0f, 1.05f + bob + crouch, AttackPose(-0.08f, 0.21f))
        * root;
    const XMFLOAT4 bodyColor = m_MidBoss ? XMFLOAT4{ 0.42f, 0.09f, 0.27f, 1.0f }
        : XMFLOAT4{ 0.65f, 0.18f, 0.15f, 1.0f };
    DrawPart({ m_MidBoss ? 0.80f : 0.70f, 1.05f, 0.55f }, torso, bodyColor);

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
        DrawPart({ 0.47f, 0.24f, 0.24f }, XMMatrixTranslation(side * 0.34f, 0.0f, 0.0f) * arm,
            { 0.72f, 0.24f, 0.15f, 1.0f });
        const float fistRadius = m_MidBoss ? 0.54f : 0.43f;
        DrawPart({ fistRadius, fistRadius, fistRadius }, XMMatrixTranslation(side * 0.83f, 0.0f, 0.0f) * arm,
            { 1.0f, 0.62f, 0.16f, 1.0f });

        DrawPart({ 0.105f, 0.14f, 0.07f }, XMMatrixTranslation(side * 0.23f, 0.40f, 0.48f) * torso,
            { 1.0f, 0.87f, 0.52f, 1.0f });
        DrawPart({ 0.043f, 0.075f, 0.035f }, XMMatrixTranslation(side * 0.23f, 0.39f, 0.54f) * torso,
            { 0.13f, 0.06f, 0.04f, 1.0f });
        if (m_MidBoss)
        {
            DrawPart({ 0.16f, 0.41f, 0.16f }, XMMatrixRotationZ(-side * 0.36f)
                * XMMatrixTranslation(side * 0.49f, 0.87f, 0.0f) * torso,
                { 0.72f, 0.36f, 0.16f, 1.0f });
            DrawPart({ 0.22f, 0.065f, 0.09f }, XMMatrixRotationZ(side * 0.30f)
                * XMMatrixTranslation(side * 0.24f, 0.48f, 0.52f) * torso, bodyColor);
        }
    }
    if (m_Attack == Attack::Slam && m_AttackTime >= SlamImpact)
    {
        const float t = (m_AttackTime - SlamImpact) / ShockwaveDuration;
        const float radius = 1.0f + 2.25f * t;
        DrawPart({ radius, 1.0f, radius },
            XMMatrixTranslation(m_Position.x, m_Position.y + 0.065f, m_Position.z),
            { 1.0f, 0.88f - 0.50f * t, 0.25f - 0.15f * t, 1.0f }, true);
    }
}
