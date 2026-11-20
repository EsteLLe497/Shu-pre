// =========================================================
// ファイルの情報[Camera.cpp]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Camera.h"
#include "Player.h"
#include "input.h"
#include "SceneEditor.h"
#include "planetarium.h"

namespace
{
    // Exact critically damped spring for a constant target over this frame.
    // Smooth both position and velocity without depending on the frame rate.
    float smoothCamera(float current, float target, float& velocity, float smoothTime, float dt)
    {
        const float omega = 2.0f / smoothTime;
        const float decay = expf(-omega * dt);
        const float difference = current - target;
        const float step = (velocity + omega * difference) * dt;
        velocity = (velocity - omega * step) * decay;
        return target + (difference + step) * decay;
    }
}

// =========================================================
// カメラの初期化
// =========================================================
void Camera::Init()
{
    m_Layer = 0;
    m_distance = m_orbit.distance;
    m_followInitialized = false;
    m_followVelocity = { 0.0f, 0.0f, 0.0f };
    m_yawVelocity = m_pitchVelocity = m_zoomVelocity = 0.0f;
    m_Position = { 0.0f, m_orbit.lookHeight + sinf(m_orbit.pitch) * m_distance,
        -cosf(m_orbit.pitch) * m_distance };
    m_Rotation.x = m_orbit.pitch;
    m_Rotation.y = m_orbit.yaw;
    m_renderPosition = m_Position;
}

void Camera::Uninit()
{
}

// =========================================================
// マウス操作と滑らかな追従を更新
// =========================================================
void Camera::Update()
{
    Player* player = Manager::GetGameObj<Player>();
    if (!player)
    {
        return;
    }

    const float dt = Manager::animationDeltaSeconds();
    constexpr float mouseSensitivity = 0.0025f;
    constexpr float keyboardSpeed = 2.5f;
    const float mouseX = static_cast<float>(Input::GetMouseDeltaX());
    const float mouseY = static_cast<float>(Input::GetMouseDeltaY());

    // 入力で目標角度を変え、実際の向きは滑らかに追従させる。
    m_orbit.yaw += mouseX * mouseSensitivity;
    m_orbit.pitch += mouseY * mouseSensitivity;
    if (Input::GetKeyPress(VK_RIGHT)) m_orbit.yaw += keyboardSpeed * dt;
    if (Input::GetKeyPress(VK_LEFT)) m_orbit.yaw -= keyboardSpeed * dt;
    if (Input::GetKeyPress(VK_UP)) m_orbit.pitch -= keyboardSpeed * dt;
    if (Input::GetKeyPress(VK_DOWN)) m_orbit.pitch += keyboardSpeed * dt;
    m_orbit.pitch = fmaxf(CameraOrbit::minPitch, fminf(CameraOrbit::maxPitch, m_orbit.pitch));

    // 距離に比例してズームし、近距離では細かく調整できるようにする。
    const float wheelSteps = static_cast<float>(Input::GetMouseWheel()) / WHEEL_DELTA;
    if (wheelSteps != 0.0f)
    {
        m_orbit.distance *= powf(0.87f, wheelSteps);
        m_orbit.distance = fmaxf(CameraOrbit::minDistance,
            fminf(CameraOrbit::maxDistance, m_orbit.distance));
    }

    constexpr float rotationSmoothTime = 0.12f;
    constexpr float zoomSmoothTime = 0.16f;
    const float yawDifference = remainderf(m_orbit.yaw - m_Rotation.y, XM_2PI);
    m_Rotation.y = remainderf(smoothCamera(m_Rotation.y, m_Rotation.y + yawDifference,
        m_yawVelocity, rotationSmoothTime, dt), XM_2PI);
    m_orbit.yaw = remainderf(m_orbit.yaw, XM_2PI);
    m_Rotation.x = smoothCamera(m_Rotation.x, m_orbit.pitch, m_pitchVelocity, rotationSmoothTime, dt);
    m_distance = smoothCamera(m_distance, m_orbit.distance, m_zoomVelocity, zoomSmoothTime, dt);
    // A quick reversal at a limit must not carry spring momentum outside it.
    const float pitch = fmaxf(CameraOrbit::minPitch, fminf(CameraOrbit::maxPitch, m_Rotation.x));
    if (pitch != m_Rotation.x) m_pitchVelocity = 0.0f;
    m_Rotation.x = pitch;
    const float distance = fmaxf(CameraOrbit::minDistance, fminf(CameraOrbit::maxDistance, m_distance));
    if (distance != m_distance) m_zoomVelocity = 0.0f;
    m_distance = distance;

    // Keep shake decay independent of the follow spring.
    m_shakeTime += dt;
    m_shake *= powf(0.9f, dt * 60.0f);
}

// =========================================================
// 透視投影とビュー行列を設定
// =========================================================
void Camera::Draw()
{
    if (SceneEditor::cameraMatrices(m_renderPosition, m_viewMatrix, m_projectionMatrix))
    {
        Renderer::SetProjectionMatrix(m_projectionMatrix);
        Renderer::SetViewMatrix(m_viewMatrix);
        Renderer::SetCameraPosition(m_renderPosition);
        return;
    }
    // 接写時だけ手前のクリップ面を近づけ、遠景の深度精度を保つ。
    const auto* player = Manager::GetGameObj<Player>();
    // Smooth the orbit center using the final player position for this frame.
    if (player)
    {
        const Vector3 desiredTarget = player->GetPosition() + Vector3(0.0f, m_orbit.lookHeight, 0.0f);
        // Sample after world updates; do not cancel the lag with a render-time offset.
        if (!m_followInitialized || (desiredTarget - m_target).lenght() > 12.0f)
        {
            m_target = desiredTarget;
            m_followVelocity = { 0.0f, 0.0f, 0.0f };
            m_followInitialized = true;
        }
        else
        {
            constexpr float followSmoothTime = 0.18f;
            const float dt = Manager::animationDeltaSeconds();
            m_target.x = smoothCamera(m_target.x, desiredTarget.x, m_followVelocity.x, followSmoothTime, dt);
            m_target.y = smoothCamera(m_target.y, desiredTarget.y, m_followVelocity.y, followSmoothTime, dt);
            m_target.z = smoothCamera(m_target.z, desiredTarget.z, m_followVelocity.z, followSmoothTime, dt);
        }
    }
    const float horizontalDistance = cosf(m_Rotation.x) * m_distance;
    const Vector3 orbitOffset(-sinf(m_Rotation.y) * horizontalDistance,
        sinf(m_Rotation.x) * m_distance, -cosf(m_Rotation.y) * horizontalDistance);
    m_Position = m_target + orbitOffset;
    if (player)
        m_Position.y = fmaxf(player->GetPosition().y + 0.40f, m_Position.y);
    m_viewTarget = m_target + m_shake * cosf(m_shakeTime * 100.0f);
    m_renderPosition = m_Position;
    auto aim = m_viewTarget;
    if (player)
        planetarium::frameCamera(player->GetPosition(), m_Rotation, m_distance, m_renderPosition, aim);

    const float eyeDistance = player ? (m_renderPosition - player->GetPosition()).lenght() : m_distance;
    const float nearPlane = fminf(1.0f, eyeDistance * 0.16f);
    m_projectionMatrix = XMMatrixPerspectiveFovLH(1.0f,
        static_cast<float>(SCREEN_WIDTH) / SCREEN_HEIGHT, nearPlane, 1000.0f);
    Renderer::SetProjectionMatrix(m_projectionMatrix);

    planetarium::assistAim(m_renderPosition, aim, m_projectionMatrix);
    const auto target = XMVectorSet(aim.x, aim.y, aim.z, 1.0f);
    const XMFLOAT3 up(0.0f, 1.0f, 0.0f);
    m_viewMatrix = XMMatrixLookAtLH(XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&m_renderPosition)),
        target, XMLoadFloat3(&up));
    Renderer::SetViewMatrix(m_viewMatrix);
    Renderer::SetCameraPosition(m_renderPosition);
}

bool Camera::CheckView(Vector3 Position)
{
    XMMATRIX vp;
	vp = m_viewMatrix * m_projectionMatrix;

    XMMATRIX invVp;
	invVp = XMMatrixInverse(nullptr, vp);

    XMFLOAT3 vpos[4];

	vpos[0] = XMFLOAT3(-1.0f, 1.0f, 1.0f);
    vpos[1] = XMFLOAT3(1.0f, 1.0f, 1.0f);
    vpos[2] = XMFLOAT3(-1.0f, -1.0f, 1.0f);
    vpos[3] = XMFLOAT3(1.0f, -1.0f, 1.0f);

    XMVECTOR vposv[4];
	vposv[0] = XMLoadFloat3(&vpos[0]);  
    vposv[1] = XMLoadFloat3(&vpos[1]);
    vposv[2] = XMLoadFloat3(&vpos[2]);
    vposv[3] = XMLoadFloat3(&vpos[3]);
  
	XMVECTOR wposv[4];
    wposv[0] = XMVector3TransformCoord(vposv[0], invVp);
    wposv[1] = XMVector3TransformCoord(vposv[1], invVp);
    wposv[2] = XMVector3TransformCoord(vposv[2], invVp);
    wposv[3] = XMVector3TransformCoord(vposv[3], invVp);
	
	Vector3 wp[4];
	XMStoreFloat3((XMFLOAT3*)&wp[0], wposv[0]);
    XMStoreFloat3((XMFLOAT3*)&wp[1], wposv[1]);
    XMStoreFloat3((XMFLOAT3*)&wp[2], wposv[2]);
    XMStoreFloat3((XMFLOAT3*)&wp[3], wposv[3]);

    Vector3 v;
    v = Position - m_Position;

    //左面
    {
        Vector3 v1, v2;
        v1 = wp[0] - m_Position;
        v2 = wp[2] - m_Position;

        Vector3 n;
        n = Vector3::cross(v1, v2);
        n.normalize();

        float d;
        d = Vector3::dot(n, v);

        if (d < 0.0f)
        {
            return false;
        }

        return true;
    }

    //右面
    {
        Vector3 v3, v4;
        v3 = wp[1] - m_Position;
        v4 = wp[3] - m_Position;

        Vector3 n;
        n = Vector3::cross(v3, v4);
        n.normalize();

        float d;
        d = Vector3::dot(n, v);
        if (d < 0.0f)
        {
            return false;
        }

        return true;
    }

    //上面
    {
        Vector3 v1, v3;
        v1 = wp[0] - m_Position;
        v3 = wp[1] - m_Position;

        Vector3 n;
        n = Vector3::cross(v1, v3);
        n.normalize();

        float d;
        d = Vector3::dot(n, v);
        if (d < 0.0f)
        {
            return false;
        }

        return true;
    }

    //上面
    {
        Vector3 v2, v4;
        v2 = wp[2] - m_Position;
        v4 = wp[3] - m_Position;

        Vector3 n;
        n = Vector3::cross(v2, v4);
        n.normalize();

        float d;
        d = Vector3::dot(n, v);
        if (d < 0.0f)
        {
            return false;
        }

        return true;
    }
}

Vector3 Camera::GetFoward()
{
    Vector3 forward = m_target - m_Position;
    forward.normalize();
    return forward;
}

Vector3 Camera::GetRight()
{
    Vector3 forward = GetFoward();
    Vector3 up(0.0f, 1.0f, 0.0f);
    Vector3 right = Vector3::cross(up, forward);
    right.normalize();
    return right;
}

void Camera::Shake(Vector3 shake)
{
    m_shake = shake;
    m_shakeTime = 0.0f;
}