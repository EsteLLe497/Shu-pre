// =========================================================
// ファイルの情報[PreviewCamera.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "PreviewCamera.h"
#include "imgui/imgui.h"

namespace
{
    constexpr float verticalFov = 0.80f;

    XMVECTOR orbitDirection(float yaw, float pitch)
    {
        const float horizontal = cosf(pitch);
        return XMVectorSet(sinf(yaw) * horizontal, sinf(pitch), cosf(yaw) * horizontal, 0.0f);
    }
}

// =========================================================
// ビュー上で開始したドラッグだけをカメラ操作として受け付ける
// =========================================================
bool PreviewCamera::mouseOverViewport()
{
    const auto& io = ImGui::GetIO();
    return ImGui::IsMousePosValid()
        && io.MousePos.x >= 0.0f && io.MousePos.x < io.DisplaySize.x
        && io.MousePos.y >= 0.0f && io.MousePos.y < io.DisplaySize.y
        && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)
        && !io.WantCaptureMouse;
}

void PreviewCamera::handleMouse()
{
    const auto& io = ImGui::GetIO();
    if (m_drag != Drag::none && !ImGui::IsMouseDown(m_dragButton))
    {
        m_drag = Drag::none;
        m_dragButton = -1;
    }

    if (m_drag == Drag::none && mouseOverViewport())
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            m_drag = Drag::pan;
            m_dragButton = ImGuiMouseButton_Middle;
        }
        else if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_drag = Drag::orbit;
            m_dragButton = ImGuiMouseButton_Left;
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_drag = io.KeyAlt ? Drag::dolly : Drag::orbit;
            m_dragButton = ImGuiMouseButton_Right;
        }

        if (io.MouseWheel != 0.0f)
            zoom(-io.MouseWheel * 0.15f);

        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
            reset();
    }

    // ドラッグ開始前のマウス移動をカメラへ混ぜない。
    if (m_dragButton >= 0 && ImGui::IsMouseClicked(m_dragButton))
        return;

    const ImVec2 delta = io.MouseDelta;
    if (m_drag == Drag::orbit)
    {
        yaw = remainderf(yaw + delta.x * 0.008f, XM_2PI);
        pitch = fmaxf(XMConvertToRadians(-25.0f), fminf(XMConvertToRadians(65.0f), pitch + delta.y * 0.008f));
    }
    else if (m_drag == Drag::pan && io.DisplaySize.y > 0.0f)
    {
        // カメラの右と上に沿って注視点を移動する。
        const XMVECTOR forward = XMVectorNegate(orbitDirection(yaw, pitch));
        const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
        const XMVECTOR up = XMVector3Cross(forward, right);
        const float worldPerPixel = 2.0f * distance * tanf(verticalFov * 0.5f) / io.DisplaySize.y;
        const XMVECTOR offset = (up * delta.y - right * delta.x) * worldPerPixel;
        m_panX += XMVectorGetX(offset);
        m_panY += XMVectorGetY(offset);
        m_panZ += XMVectorGetZ(offset);
    }
    else if (m_drag == Drag::dolly)
    {
        zoom((delta.y - delta.x) * 0.009f);
    }
}

void PreviewCamera::zoom(float amount)
{
    distance = fmaxf(minDistance, fminf(maxDistance, distance * expf(amount)));
}

void PreviewCamera::reset()
{
    *this = PreviewCamera{};
}

// =========================================================
// 注視点と距離からビュー・投影を設定する
// =========================================================
XMMATRIX PreviewCamera::projection()
{
    return XMMatrixPerspectiveFovLH(verticalFov,
        static_cast<float>(SCREEN_WIDTH) / SCREEN_HEIGHT, 0.1f, 100.0f);
}

XMMATRIX PreviewCamera::bind(const XMFLOAT3& targetOffset) const
{
    const XMVECTOR target = XMVectorSet(m_panX, lookHeight + m_panY, m_panZ, 1.0f);
    const XMVECTOR eye = target + orbitDirection(yaw, pitch) * distance;
    const XMVECTOR offset = XMVector3ClampLength(XMLoadFloat3(&targetOffset), 0.0f, distance * 0.8f);
    const XMVECTOR aimedTarget = target + offset;
    const auto view = XMMatrixLookAtLH(eye, aimedTarget, XMVectorSet(0, 1, 0, 0));
    Renderer::SetViewMatrix(view);
    Renderer::SetProjectionMatrix(projection());
    Renderer::SetCameraPosition({ XMVectorGetX(eye), XMVectorGetY(eye), XMVectorGetZ(eye) });
    return view;
}
