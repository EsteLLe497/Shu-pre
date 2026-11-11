#include "main.h"
#include "renderer.h"
#include "Bullet.h"
#include "Explosion.h"
#include "modelRenderer.h"
#include "input.h"
#include "Collision.h"

#include "Manager.h"
#include "Camera.h"

#include "Enemy.h"
#include "Box.h"
#include "Score.h"

void Bullet::Init()
{
    m_Layer = 1;
    m_Position = { 0.0f, 0.0f, 0.0f };

    ModelRenderer* m_ModelRenderer = AddGameComponent<ModelRenderer>(this);
    m_ModelRenderer->Load("asset\\model\\bullet.obj");

    // シェーダー読込
    Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout,
        "shader\\unlitTextureVS.cso");

    Renderer::CreatePixelShader(&m_PixelShader,
        "shader\\unlitTexturePS.cso");
}

void Bullet::Uninit()
{
    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();

    GameObject::Uninit();
}

void Bullet::Update()
{
    float dt = 1.0 / 60.0f;


    m_Position += m_Velocity * dt;

    auto enemies = Manager::GetGameObjs<Enemy>();
    
    for (auto enemy : enemies)
    {
        Vector3 dir = enemy->GetPosition() - m_Position;
        float lenght = VectorMag(dir);
        if (lenght < 1.0f)
        {
            enemy->AddDamage(1);
            enemy->Shake(m_Velocity * 0.1f);

            SetDestory();
            break;
        }
    }

    auto boxes = Manager::GetGameObjs<Box>();

    for (auto box : boxes)
    {
        if (Collision::SphereVsAABB(m_Position, 0.2f, box->GetPosition(), box->GetScale()))
        {
            SetDestory();
            Manager::AddGameObj<Explosion>()->SetPosition(m_Position);
            break;
        }
    }

    m_Lefttime -= dt;
    if (m_Lefttime <= 0.0f)
    {
        SetDestory();
    }

    GameObject::Update();
}

void Bullet::Draw()
{
    // 入力レイアウト設定
    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    // シェーダ設定
    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);


    GameObject::Draw();
}
