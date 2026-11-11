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
    static constexpr float minPitch = -0.5f;
    static constexpr float maxPitch = 1.0f;
    static constexpr float minLookHeight = 0.0f;
    static constexpr float maxLookHeight = 3.0f;

    float yaw = 0.0f;
    float pitch = 0.36f;
    float distance = 5.0f;
    float lookHeight = 0.95f;
};

class Camera : public GameObject
{
private:
    Vector3 m_target{ 0.0f, 0.0f, 0.0f };
    Vector3 m_viewTarget{ 0.0f, 0.0f, 0.0f };
    XMMATRIX m_viewMatrix;
	XMMATRIX m_projectionMatrix;
    Vector3 m_shake{ 0.0f, 0.0f, 0.0f };
    float m_shakeTime = 0.0f;
    CameraOrbit m_orbit;
    float m_distance = m_orbit.distance;

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    bool CheckView(Vector3 Position);
    CameraOrbit& orbitSettings() { return m_orbit; }

    XMMATRIX GetViewMatrix() { return m_viewMatrix; }
    Vector3 GetFoward() override;
    Vector3 GetRight() override;
    void Shake(Vector3 shake);
};