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

// =========================================================
// プレイヤーの目の位置を基準にした一人称カメラ
// =========================================================
void Camera::Init()
{
    m_Layer = 0;
    m_Position = { 0.0f, m_orbit.lookHeight, 0.0f };
    m_Rotation = { m_orbit.pitch, m_orbit.yaw, 0.0f };
    m_renderPosition = m_Position;
    m_viewMatrix = XMMatrixIdentity();
    m_projectionMatrix = XMMatrixPerspectiveFovLH(1.0f,
        static_cast<float>(SCREEN_WIDTH) / SCREEN_HEIGHT, 0.05f, 1000.0f);
}

void Camera::Uninit()
{
}

void Camera::Update()
{
    if (!Manager::GetGameObj<Player>()) return;

    const float dt = Manager::animationDeltaSeconds();
    constexpr float mouseSensitivity = 0.0025f;
    constexpr float keyboardSpeed = 2.5f;
    m_orbit.yaw += static_cast<float>(Input::GetMouseDeltaX()) * mouseSensitivity;
    m_orbit.pitch += static_cast<float>(Input::GetMouseDeltaY()) * mouseSensitivity;
    if (Input::GetKeyPress(VK_RIGHT)) m_orbit.yaw += keyboardSpeed * dt;
    if (Input::GetKeyPress(VK_LEFT)) m_orbit.yaw -= keyboardSpeed * dt;
    if (Input::GetKeyPress(VK_UP)) m_orbit.pitch -= keyboardSpeed * dt;
    if (Input::GetKeyPress(VK_DOWN)) m_orbit.pitch += keyboardSpeed * dt;
    m_orbit.yaw = remainderf(m_orbit.yaw, XM_2PI);
    m_orbit.pitch = fmaxf(CameraOrbit::minPitch, fminf(CameraOrbit::maxPitch, m_orbit.pitch));
    m_Rotation = { m_orbit.pitch, m_orbit.yaw, 0.0f };

    m_shakeTime += dt;
    m_shake *= powf(0.9f, dt * 60.0f);
}

void Camera::Draw()
{
    if (SceneEditor::cameraMatrices(m_renderPosition, m_viewMatrix, m_projectionMatrix))
    {
        Renderer::SetProjectionMatrix(m_projectionMatrix);
        Renderer::SetViewMatrix(m_viewMatrix);
        Renderer::SetCameraPosition(m_renderPosition);
        return;
    }

    // 移動後の位置に直結させ、追従の遅れで体から視点が離れないようにする。
    if (const auto* player = Manager::GetGameObj<Player>())
        m_Position = player->GetPosition() + Vector3{ 0, fmaxf(0.8f, fminf(2.0f, m_orbit.lookHeight)), 0 };
    m_renderPosition = m_Position;
    const Vector3 aim = m_renderPosition + GetFoward() + m_shake * cosf(m_shakeTime * 100.0f);
    m_viewMatrix = XMMatrixLookAtLH(
        XMVectorSet(m_renderPosition.x, m_renderPosition.y, m_renderPosition.z, 1),
        XMVectorSet(aim.x, aim.y, aim.z, 1), XMVectorSet(0, 1, 0, 0));
    m_projectionMatrix = XMMatrixPerspectiveFovLH(1.0f,
        static_cast<float>(SCREEN_WIDTH) / SCREEN_HEIGHT, 0.05f, 1000.0f);
    Renderer::SetProjectionMatrix(m_projectionMatrix);
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
    const float horizontal = cosf(m_Rotation.x);
    return { sinf(m_Rotation.y) * horizontal, -sinf(m_Rotation.x), cosf(m_Rotation.y) * horizontal };
}

Vector3 Camera::GetRight()
{
    return { cosf(m_Rotation.y), 0, -sinf(m_Rotation.y) };
}

void Camera::Shake(Vector3 shake)
{
    m_shake = shake;
    m_shakeTime = 0.0f;
}