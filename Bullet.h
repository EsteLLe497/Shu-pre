#pragma once
#include "GameObject.h"

class Bullet : public GameObject
{
private:
    Vector3 m_Velocity{ 0.0f, 0.0f, 0.0f }; //‘¬“x
    float m_Lefttime{ 1.0f };


    ID3D11InputLayout* m_VertexLayout;
    ID3D11VertexShader* m_VertexShader;
    ID3D11PixelShader* m_PixelShader;

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

    void SetVelocity(const Vector3& Velocity) { m_Velocity = Velocity; }
};


