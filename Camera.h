// =========================================================
// ファイルの情報[Camera.h]
// 制作者:Masatora Tanaka    日付：2026/11/01
// =========================================================
#pragma once
#include "GameObject.h"

struct CameraOrbit
{
    static constexpr float minDistance = 0.6f;
    static constexpr float maxDistance = 30.0f;
    static constexpr float minPitch = -1.48f;
    static constexpr float maxPitch = 1.48f;
    static constexpr float minLookHeight = 0.0f;
    static constexpr float maxLookHeight = 3.0f;

    float yaw = 0.0f;
    float pitch = 0.0f;
    float distance = 4.8f; // Retained for compatibility with saved appearance presets.
    float lookHeight = 1.60f;
};

class Camera : public GameObject
{
private:
    XMMATRIX m_viewMatrix;
	XMMATRIX m_projectionMatrix;
    Vector3 m_shake{ 0.0f, 0.0f, 0.0f };
    float m_shakeTime = 0.0f;
    CameraOrbit m_orbit;
    Vector3 m_renderPosition{ 0.0f, 0.0f, 0.0f };

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    bool CheckView(Vector3 Position);
    CameraOrbit& orbitSettings() { return m_orbit; }

    XMMATRIX GetViewMatrix() { return m_viewMatrix; }
    XMMATRIX projectionMatrix() const { return m_projectionMatrix; }
    const Vector3& renderPosition() const { return m_renderPosition; }
    Vector3 GetFoward() override;
    Vector3 GetRight() override;
    void Shake(Vector3 shake);
};