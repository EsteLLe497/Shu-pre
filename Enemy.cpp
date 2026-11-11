#include "main.h"
#include "renderer.h"
#include "Enemy.h"
#include "modelRenderer.h"
#include "manager.h"
#include "Explosion.h"
#include "Camera.h"
#include "Score.h"
#include "Shadow.h"
#include <algorithm>
#define NOMINMAX
#include <cmath>

void Enemy::Init()
{
    m_Layer = 1;
    m_Scale = { 1.0f, 1.0f, 1.0f };
    m_Life = 2;
    m_Flash =  true;

    P0 = m_Position;
    P1 = m_Position;
    P2 = m_Position;
    P3 = m_Position;

    m_Rotation.y -= XM_PI;

    m_ModelRenderer = AddGameComponent<ModelRenderer>(this);
    m_ModelRenderer->Load("asset\\model\\Rabbit\\rabbit_1.obj");


    Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout, "shader\\litTextureVS.cso");
    Renderer::CreatePixelShader(&m_PixelShader, "shader\\litTexturePS.cso");

    m_Shadow = Manager::AddGameObj<Shadow>();
    m_Shadow->SetScale({ 1.5f ,1.5f ,1.5f });
}

void Enemy::Uninit()
{
    m_Shadow->SetDestory();

    if (m_VertexLayout) { m_VertexLayout->Release(); m_VertexLayout = nullptr; }
    if (m_VertexShader) { m_VertexShader->Release(); m_VertexShader = nullptr; }
    if (m_PixelShader) { m_PixelShader->Release();  m_PixelShader = nullptr; }

    GameObject::Uninit();
}

void Enemy::Update()
{
    const float dt = 1.0f / 60.0f;

    m_Position += m_Shake * cosf(m_ShakeTime * 100.0f);
    m_ShakeTime += dt;
    m_Shake *= 0.9;
    m_ModelRenderer->SetFlash(m_Flash);

    if (m_ShakeTime > 0.04f)
    {
        m_Flash = false;
    }

    if (!m_PathInitialized)
    {
        // first path starts at current position
        P0 = m_Position;
        P1 = m_Position;
        P2 = m_Position;
        P3 = m_Position;
        CreateRandomBezier();
        m_PathInitialized = true;
        m_T = 0.0f;
    }

    // -----------------------------
    // Advance Bezier parameter
    // -----------------------------
    m_T += m_MoveSpeed * dt;

    while (m_T > 1.0f)
    {
        m_T -= 1.0f;       // keep overflow (continuous motion)
        CreateRandomBezier();
    }

    // Current / next point on curve
    Vector3 pos = Bezier(m_T, P0, P1, P2, P3);

    float nextT = m_T + 0.01f;
    Vector3 nextP0 = P0, nextP1 = P1, nextP2 = P2, nextP3 = P3;

    if (nextT > 1.0f)
    {
        // preview direction across boundary without moving state
        nextT -= 1.0f;
        nextP0 = P3;
        // approximate forward continuation if next segment unknown:
        Vector3 tangent = P3 - P2;
        if ((tangent * tangent) < 0.000001f) tangent = Vector3(0.0f, 0.0f, 1.0f);
        tangent.normalize();
        nextP1 = nextP0 + tangent * 6.0f;
        nextP2 = nextP0 + tangent * 12.0f;
        nextP3 = nextP0 + tangent * 20.0f;
    }

    Vector3 nextPos = Bezier(nextT, nextP0, nextP1, nextP2, nextP3);

    // Forward basis
    Vector3 forward = nextPos - pos;
    if ((forward * forward) < 0.000001f) forward = Vector3(0.0f, 0.0f, 1.0f);
    else forward.normalize();

    Vector3 worldUp(0.0f, 1.0f, 0.0f);
    Vector3 right = Vector3::cross(forward, worldUp);
    if ((right * right) < 0.001f) right = Vector3(1.0f, 0.0f, 0.0f);
    right.normalize();

    Vector3 up = Vector3::cross(right, forward);
    up.normalize();

    // -----------------------------
    // Corkscrew offset
    // -----------------------------
    m_Time += dt;
    const float radius = 0.8f; // reduced to avoid visual pop
    const float speed = 8.0f;  // reduced to avoid jitter

    Vector3 offset =
        right * cosf(m_Time * speed) * radius +
        up * sinf(m_Time * speed) * radius;

    Vector3 desiredPos = pos + offset;

    // ---------------------------------------------------------
    // IMPORTANT: smooth follow, do NOT hard overwrite position
    // ---------------------------------------------------------
    Vector3 toDesired = desiredPos - m_Position;
    const float follow = 8.0f; // tune 5~12
    m_Velocity = toDesired * follow;
    m_Position += m_Velocity * dt;

    // Smooth yaw (neutral turn)
    float targetYaw = atan2f(forward.x, forward.z) + XM_PI;
    float deltaYaw = targetYaw - m_Rotation.y;
    while (deltaYaw > XM_PI)  deltaYaw -= XM_2PI;
    while (deltaYaw < -XM_PI) deltaYaw += XM_2PI;

    const float turnSpeed = 4.0f;
    float maxStep = turnSpeed * dt;
    if (deltaYaw > maxStep) deltaYaw = maxStep;
    if (deltaYaw < -maxStep) deltaYaw = -maxStep;
    m_Rotation.y += deltaYaw;

    // Squash & stretch
    float bounce = sinf(m_Time * m_Frequency);
    float scaleY = 1.0f + bounce * 0.15f;
    float scaleXZ = 1.0f - bounce * 0.08f;
    m_Scale.x = scaleXZ;
    m_Scale.y = scaleY;
    m_Scale.z = scaleXZ;

    // -----------------------------
    // Stable enemy-enemy collision
    // -----------------------------
    auto enemies = Manager::GetGameObjs<Enemy>();
    for (Enemy* other : enemies)
    {
        if (other == this) continue;
        if (this > other) continue; // resolve pair once

        Vector3 delta = m_Position - other->m_Position;
        float distSq = delta * delta;
        float minDist = m_Radius + other->m_Radius;
        float minDistSq = minDist * minDist;

        if (distSq >= minDistSq) continue;

        float dist = sqrtf(distSq);
        Vector3 n;
        if (dist < 0.00001f)
        {
            n = Vector3(1.0f, 0.0f, 0.0f);
            dist = minDist;
        }
        else
        {
            n = delta * (1.0f / dist);
        }

        // positional correction
        float penetration = minDist - dist;
        float totalMass = m_Mass + other->m_Mass;
        if (totalMass < 0.00001f) totalMass = 1.0f;

        float moveA = penetration * (other->m_Mass / totalMass);
        float moveB = penetration * (m_Mass / totalMass);

        m_Position += n * moveA;
        other->m_Position -= n * moveB;

        // impulse
        Vector3 rv = m_Velocity - other->m_Velocity;
        float velAlongNormal = Vector3::dot(rv, n);
        if (velAlongNormal > 0.0f) continue;

        float e = m_BoundConst;
        float invA = (m_Mass > 0.00001f) ? (1.0f / m_Mass) : 0.0f;
        float invB = (other->m_Mass > 0.00001f) ? (1.0f / other->m_Mass) : 0.0f;

        float denom = invA + invB;
        if (denom < 0.00001f) continue;

        float j = -(1.0f + e) * velAlongNormal / denom;
        Vector3 impulse = n * j;

        m_Velocity += impulse * invA;
        other->m_Velocity -= impulse * invB;
    }

    Vector3 shadowPos = m_Position;
    shadowPos.y = 0.01f;
    m_Shadow->SetPosition(shadowPos);

    GameObject::Update();
}

void Enemy::Draw()
{
    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);
    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);


    GameObject::Draw();
}

Vector3 Enemy::Bezier(float t, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3)
{
    float u = 1.0f - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}

void Enemy::CreateRandomBezier()
{
    P0 = m_Position;

    float angle = (rand() % 360) * XM_PI / 180.0f;
    float distance = 20.0f;

    // keep same Y as current position
    P3 = P0 + Vector3(
        cosf(angle) * distance,
        0.0f,
        sinf(angle) * distance);

    Vector3 dir = P3 - P0;
    if ((dir * dir) < 0.000001f) dir = Vector3(0.0f, 0.0f, 1.0f);
    else dir.normalize();

    P1 = P0 + dir * 6.0f;
    P2 = P0 + dir * 12.0f;

    // force all control points to same Y (no vertical randomness)
    P1.y = P0.y;
    P2.y = P0.y;
    P3.y = P0.y;

    P1.x = Clamp(P1.x, -25.0f, 25.0f);
    P2.x = Clamp(P2.x, -25.0f, 25.0f);
    P3.x = Clamp(P3.x, -25.0f, 25.0f);

    // keep Y fixed again after clamp logic
    P1.y = P0.y;
    P2.y = P0.y;
    P3.y = P0.y;

    P1.z = Clamp(P1.z, -25.0f, 25.0f);
    P2.z = Clamp(P2.z, -25.0f, 25.0f);
    P3.z = Clamp(P3.z, -25.0f, 25.0f);
}

float Enemy::Clamp(float value, float minValue, float maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

void Enemy::AddDamage(int Damage)
{
    m_Life -= Damage;
    m_Flash = true;
    if (m_Life <= 0)
    {
        SetDestory();
        Explosion* explosion = Manager::AddGameObj<Explosion>();
        explosion->SetPosition(m_Position);
        this->SetScale(m_Scale * 2.0f);

        Camera* camera = Manager::GetGameObj<Camera>();
        camera->Shake({ 1.0f,1.0f,0.0f });

        Manager::GetGameObj<Score>()->Add(1);
    }
}