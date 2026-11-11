#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Particle.h"
#include "Camera.h"
#include "input.h"


void Particle::Init()
{
    m_Layer = 3;

    VERTEX_3D vertex[4];

    vertex[0].Position = XMFLOAT3(-0.5f, 0.5f, 0.0f);
    vertex[0].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[0].TexCoord = XMFLOAT2(0.0f, 0.0f);

    vertex[1].Position = XMFLOAT3(0.5f, 0.5f, 0.0f);
    vertex[1].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[1].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[1].TexCoord = XMFLOAT2(1.0f, 0.0f);

    vertex[2].Position = XMFLOAT3(-0.5f, -0.5f, 0.0f);
    vertex[2].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[2].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[2].TexCoord = XMFLOAT2(0.0f, 1.0f);

    vertex[3].Position = XMFLOAT3(0.5f, -0.5f, 0.0f);
    vertex[3].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[3].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[3].TexCoord = XMFLOAT2(1.0f, 1.0f);

    // 頂点バッファ生成
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(VERTEX_3D) * 4;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = vertex;

    Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_VertexBuffer);

    // シェーダー読込
    Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout,
        "shader\\unlitTextureVS.cso");

    Renderer::CreatePixelShader(&m_PixelShader,
        "shader\\unlitTexturePS.cso");

    TexMetadata metadata;
    ScratchImage image;
    LoadFromWICFile(L"asset\\bill_board\\particle.png", WIC_FLAGS_NONE, &metadata, image); 
    CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(),
        image.GetImageCount(), metadata, &m_LaunchTexture);
    assert(m_LaunchTexture);

    LoadFromWICFile(L"asset\\bill_board\\tora.png", WIC_FLAGS_NONE, &metadata, image);
    CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(),
        image.GetImageCount(), metadata, &m_ToraTexture);
    assert(m_ToraTexture);

    

    for (int i = 0; i < PARTICLE_MAX; i++)
    {
        m_Particle[i].Enable = false;
    }
}
void Particle::Uninit()
{
    if (m_LaunchTexture)
        m_LaunchTexture->Release();

    if (m_ToraTexture)
        m_ToraTexture->Release();

    m_VertexBuffer->Release();

    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();
}
void  Particle::Update()
{
    float dt = 1.0f / 60.0f;
 

    if (Input::GetKeyTrigger(VK_SPACE))
    {
        for (int i = 0; i < PARTICLE_MAX; i++)
        {
            if (!m_Particle[i].Enable)
            {
                m_Particle[i].Enable = true;
                m_Particle[i].Type = PARTICLE_LAUNCH;
                m_Particle[i].Life = 200;

                m_Particle[i].Position = m_Position;
                m_Particle[i].Velocity = Vector3(0.0f, 10.0f, 0.0f);

                break;
            }

        }
    }
    Vector3 gravity{ 0.0f, -9.8f, 0.0f };

    for (int i = 0; i < PARTICLE_MAX; i++)
    {
        if (!m_Particle[i].Enable)
            continue;

        m_Particle[i].Velocity += gravity * dt;
        m_Particle[i].Position += m_Particle[i].Velocity * dt;

        if (m_Particle[i].Type == PARTICLE_LAUNCH)
        {
            if (m_Particle[i].Velocity.y <= 0.0f)
            {
                CreateExplosion(m_Particle[i].Position);
                m_Particle[i].Enable = false;
                continue;
            }
        }

        m_Particle[i].Life--;

        if (m_Particle[i].Life <= 0)
        {
            m_Particle[i].Enable = false;
        }
    }


}
void  Particle::Draw()
{
    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);

    Camera* camera = Manager::GetGameObj<Camera>();
    XMMATRIX view = camera->GetViewMatrix();
    XMMATRIX invView = XMMatrixInverse(NULL, view);
    invView.r[3].m128_f32[0] = 0.0f;
    invView.r[3].m128_f32[1] = 0.0f;
    invView.r[3].m128_f32[2] = 0.0f;


    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    Renderer::GetDeviceContext()->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);

    Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    Renderer::SetDepthEnable(false);
    Renderer::SetAddBlendEnable(true);

    MATERIAL material{};
    material.Diffuse = { 1.0f, 0.2f, 0.2f, 1.0f };
    material.Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.TextureEnable = true;
    Renderer::SetMaterial(material);

    for (int i = 0; i < PARTICLE_MAX; i++)
    {
        if (!m_Particle[i].Enable)
            continue;

        if (m_Particle[i].Type == PARTICLE_LAUNCH)
        {
            Renderer::GetDeviceContext()->
                PSSetShaderResources(0, 1, &m_LaunchTexture);
        }
        else
        {
            Renderer::GetDeviceContext()->
                PSSetShaderResources(0, 1, &m_ToraTexture);
        }

        XMMATRIX world, scale, trans;
        scale = XMMatrixScaling(m_Scale.x, m_Scale.y, m_Scale.z);
        trans = XMMatrixTranslation(m_Particle[i].Position.x, m_Particle[i].Position.y, m_Particle[i].Position.z);
        world = scale * invView * trans;

        Renderer::SetWorldMatrix(world);

        Renderer::GetDeviceContext()->Draw(4, 0);
    }

    Renderer::SetDepthEnable(true);
    Renderer::SetAddBlendEnable(false);
    
}

void Particle::CreateExplosion(Vector3 pos)
{
    for (int n = 0; n < 50; n++)
    {
        for (int i = 0; i < PARTICLE_MAX; i++)
        {
            if (!m_Particle[i].Enable)
            {
                float theta =
                    XM_2PI * ((float)rand() / RAND_MAX);

                float phi =
                    XM_PI * ((float)rand() / RAND_MAX);

                float speed =
                    8.0f + ((float)rand() / RAND_MAX) * 12.0f;

                Vector3 dir;

                dir.x = sinf(phi) * cosf(theta);
                dir.y = cosf(phi);
                dir.z = sinf(phi) * sinf(theta);

                m_Particle[i].Enable = true;
                m_Particle[i].Type = PARTICLE_EXPLOSION;

                m_Particle[i].Position = pos;
                m_Particle[i].Velocity = dir * speed;
                m_Particle[i].Life = 100;

                break;
            }
        }
    }
}
