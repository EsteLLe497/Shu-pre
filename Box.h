#pragma once
#include "GameObject.h"

class Box : public GameObject
{
private:


    ID3D11InputLayout* m_VertexLayout;
    ID3D11VertexShader* m_VertexShader;
    ID3D11PixelShader* m_PixelShader;

public:
    void Init() override;
    void Uninit() override;
    void Update() override;
    void Draw() override;

};


