#include "main.h"
#include "renderer.h"
#include "Box.h"
#include "Explosion.h"
#include "modelRenderer.h"
#include "input.h"

#include "Manager.h"
#include "Camera.h"

#include "Enemy.h"

void Box::Init()
{
    m_Layer = 1;
    m_Position = { 0.0f, 0.0f, 0.0f };

    ModelRenderer* m_ModelRenderer = AddGameComponent<ModelRenderer>(this);
    m_ModelRenderer->Load("asset\\model\\Box\\box.obj");

    // シェーダー読込
    Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout,
        "shader\\unlitTextureVS.cso");

    Renderer::CreatePixelShader(&m_PixelShader,
        "shader\\unlitTexturePS.cso");
}

void Box::Uninit()
{
    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();

    GameObject::Uninit();
}

void Box::Update()
{
    float dt = 1.0 / 60.0f;
    

    GameObject::Update();
}

void Box::Draw()
{
    // 入力レイアウト設定
    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    // シェーダ設定
    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);


    GameObject::Draw();
}
