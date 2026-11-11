#include "main.h"
#include "renderer.h"
#include "SkyDome.h"
#include "Camera.h"
#include "manager.h"
#include "modelRenderer.h"


void SkyDome::Init()
{
    m_Layer = 1;
    m_Position = { 0.0f, 0.0f, 0.0f };
    m_Scale = { 200.0f, 200.0f, 200.0f };

    ModelRenderer* m_ModelRenderer = AddGameComponent<ModelRenderer>(this);
    m_ModelRenderer->Load("asset\\model\\SkyDome\\sky.obj");

    // シェーダー読込
    Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout,
        "shader\\unlitTextureVS.cso");

    Renderer::CreatePixelShader(&m_PixelShader,
        "shader\\unlitTexturePS.cso");

}
void SkyDome::Uninit()
{
    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();

    GameObject::Uninit();
}
void  SkyDome::Update()
{
    Camera* camera = Manager::GetGameObj<Camera>();
    m_Position = camera->GetPosition();

    GameObject::Update();
}
void  SkyDome::Draw()
{
    // 入力レイアウト設定
    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    // シェーダ設定
    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);


    GameObject::Draw();
}