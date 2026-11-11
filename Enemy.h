#pragma once

#include "GameObject.h"
#include <DirectXMath.h>

class Enemy : public GameObject
{
private:
    // Bezier
    float m_T = 0.0f;
    float m_MoveSpeed = 0.2f;

    // Sine Wave
    float m_Time = 0.0f;
    float m_Amplitude = 0.7f;
    float m_Frequency = 5.0f;

    bool m_PathInitialized = false;

    Vector3 P0 = Vector3(0.0f, 0.0f, 0.0f);
    Vector3 P1 = Vector3(0.0f, 0.0f, 0.0f);
    Vector3 P2 = Vector3(0.0f, 0.0f, 0.0f);
    Vector3 P3 = Vector3(0.0f, 0.0f, 0.0f);

    Vector3 Bezier(float t, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3);

    void CreateRandomBezier();
    float Clamp(float value, float minValue, float maxValue);

    // Collision / physics
    Vector3 m_Velocity = Vector3(0.0f, 0.0f, 0.0f);
    float m_Radius = 1.0f;
    float m_Mass = 1.0f;
    float m_BoundConst = 0.8f; // e

    float m_BeforeDistance = 999999.0f;

    // Shader
    ID3D11InputLayout* m_VertexLayout = nullptr;
    ID3D11VertexShader* m_VertexShader = nullptr;
    ID3D11PixelShader* m_PixelShader = nullptr;

    class ModelRenderer* m_ModelRenderer;

    Vector3 m_Shake;
    float m_ShakeTime;

    float m_Life;
    bool m_Flash;

    GameObject* m_Shadow;

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    void Shake(Vector3 Shake)
    {
        m_Shake = Shake;
        m_ShakeTime = 0;
    }
    void AddDamage(int Damage);

};