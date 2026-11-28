#include "main.h"
#include "renderer.h"
#include "Player.h"
#include "Camera.h"
#include "MeshField.h"
#include "SceneEditor.h"
#include "Tree.h"
#include "input.h"
#include "manager.h"

void Player::Init()
{
    m_Layer = 1;
    m_Position = { 0, 0, 0 };
}

void Player::Update()
{
    const float dt = Manager::animationDeltaSeconds();
    auto* movementCamera = Manager::GetGameObj<Camera>();
    if (!movementCamera) return;
    Vector3 forward = movementCamera->GetFoward();
    Vector3 right = movementCamera->GetRight();
    forward.y = right.y = 0.0f;
    forward.normalize();
    right.normalize();

    Vector3 direction{ 0, 0, 0 };
    if (Input::GetKeyPress('W')) direction += forward;
    if (Input::GetKeyPress('S')) direction -= forward;
    if (Input::GetKeyPress('D')) direction += right;
    if (Input::GetKeyPress('A')) direction -= right;
    const bool moving = direction.lenght() > 0.001f;
    if (moving) direction.normalize();
    const Vector3 target = direction * 2.5f;
    const float blend = 1.0f - expf(-(moving ? 16.0f : 35.0f) * dt);
    m_Velocity.x += (target.x - m_Velocity.x) * blend;
    m_Velocity.z += (target.z - m_Velocity.z) * blend;
    if (!moving && hypotf(m_Velocity.x, m_Velocity.z) < 0.05f)
        m_Velocity.x = m_Velocity.z = 0.0f;
    m_Velocity.y -= 25.0f * dt;

    const Vector3 previous = m_Position;
    const bool wasGrounded = m_Ground;
    m_Position += m_Velocity * dt;
    for (auto* tree : Manager::GetGameObjs<Tree>())
    {
        auto offset = m_Position - tree->GetPosition();
        offset.y = 0;
        const float distance = offset.lenght();
        if (distance < 1.4f)
            m_Position += distance > 0.001f ? offset * ((1.4f - distance) / distance) : Vector3{ 1.4f, 0, 0 };
    }
    m_Ground = false;
    SceneEditor::resolvePlayerCollision(previous, m_Position, m_Velocity, m_Ground);
    const auto* terrain = Manager::GetGameObj<MeshField>();
    float height = 0, oldHeight = 0;
    const bool hasGround = !terrain || terrain->tryGetHeight(m_Position.x, m_Position.z, height);
    const bool followSlope = terrain && hasGround && wasGrounded && m_Velocity.y <= 0
        && terrain->tryGetHeight(previous.x, previous.z, oldHeight)
        && fabsf(previous.y - oldHeight) <= 0.08f && oldHeight - height <= 0.6f;
    if (hasGround && (m_Position.y <= height || followSlope))
    {
        m_Position.y = height;
        m_Velocity.y = 0;
        m_Ground = true;
    }
    if (!hasGround)
    {
        m_Position = previous;
        m_Velocity = { 0, 0, 0 };
    }
    if (moving) m_Rotation.y = atan2f(direction.x, direction.z);
}
