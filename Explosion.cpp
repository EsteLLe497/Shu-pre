#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Explosion.h"
#include "Camera.h"

#define SPRITE_ROW (4)
#define SPRITE_COLUMNS (4)


void Explosion::Init()
{
    m_Layer = 3;

    m_Scale = { 0.5f, 0.5f, 0.5f };

    VERTEX_3D vertex[4];

    vertex[0].Position = XMFLOAT3(-4.0f, 10.0f, 0.0f);
    vertex[0].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[0].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[0].TexCoord = XMFLOAT2(0.0f, 0.0f);

    vertex[1].Position = XMFLOAT3(4.0f, 10.0f, 0.0f);
    vertex[1].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[1].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[1].TexCoord = XMFLOAT2(1.0f, 0.0f);

    vertex[2].Position = XMFLOAT3(-4.0f, 0.0f, 0.0f);
    vertex[2].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[2].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[2].TexCoord = XMFLOAT2(0.0f, 1.0f);

    vertex[3].Position = XMFLOAT3(4.0f, 0.0f, 0.0f);
    vertex[3].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
    vertex[3].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    vertex[3].TexCoord = XMFLOAT2(1.0f, 1.0f);

    // 頂点バッファ生成
    D3D11_BUFFER_DESC bd{};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(VERTEX_3D) * 4;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

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
    LoadFromWICFile(L"asset\\texture\\Explosion.png", WIC_FLAGS_NONE, &metadata, image);
    CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(),
        image.GetImageCount(), metadata, &m_Texture);
    assert(m_Texture);
}
void Explosion::Uninit()
{
    m_VertexBuffer->Release();

    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();
    m_Texture->Release();
}
void  Explosion::Update()
{
    m_Count++;

    if (m_Count % 3 == 0)
    {
        m_Frame++;
    }

    if (m_Frame >= frameCount)
    {
        SetDestory();
        return;
    }

    GameObject::Update();
}
void Explosion::Draw()
{
    Camera* camera = Manager::GetGameObj<Camera>();
    if (camera)
        drawFrame(m_Frame, camera->GetViewMatrix());
}

// =========================================================
// 指定フレームを直接描き、巻き戻し時も同じ絵を表示する
// =========================================================
void Explosion::drawFrame(int frame, const XMMATRIX& view)
{
    if (frame < 0 || frame >= frameCount)
        return;

    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);

    XMMATRIX invView = XMMatrixInverse(nullptr, view);

    // Billboard
    invView.r[3].m128_f32[0] = 0.0f;
    invView.r[3].m128_f32[1] = 0.0f;
    invView.r[3].m128_f32[2] = 0.0f;

    XMMATRIX scale = XMMatrixScaling(
        m_Scale.x,
        m_Scale.y,
        m_Scale.z);

    XMMATRIX trans = XMMatrixTranslation(
        m_Position.x,
        m_Position.y,
        m_Position.z);

    XMMATRIX world = scale * invView * trans;

    Renderer::SetWorldMatrix(world);

    MATERIAL material{};
    material.Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    material.Ambient = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    material.TextureEnable = true;

    Renderer::SetMaterial(material);

    Renderer::GetDeviceContext()->PSSetShaderResources(
        0,
        1,
        &m_Texture);

    // ==================================================
    // Sprite Sheet UV
    // ==================================================

    const int wc = SPRITE_COLUMNS;    // columns
    const int hc = SPRITE_ROW;    // rows

    float w = 1.0f / (float)wc;
    float h = 1.0f / (float)hc;

    float x = (float)(frame % wc) * w;
    float y = (float)(frame / wc) * h;

    D3D11_MAPPED_SUBRESOURCE msr{};

    if (SUCCEEDED(Renderer::GetDeviceContext()->Map(
        m_VertexBuffer,
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &msr)))
    {
        VERTEX_3D* v = (VERTEX_3D*)msr.pData;

        v[0].Position = XMFLOAT3(-4.0f, 10.0f, 0.0f);
        v[0].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
        v[0].Diffuse = XMFLOAT4(1, 1, 1, 1);
        v[0].TexCoord = XMFLOAT2(x, y);

        v[1].Position = XMFLOAT3(4.0f, 10.0f, 0.0f);
        v[1].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
        v[1].Diffuse = XMFLOAT4(1, 1, 1, 1);
        v[1].TexCoord = XMFLOAT2(x + w, y);

        v[2].Position = XMFLOAT3(-4.0f, 0.0f, 0.0f);
        v[2].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
        v[2].Diffuse = XMFLOAT4(1, 1, 1, 1);
        v[2].TexCoord = XMFLOAT2(x, y + h);

        v[3].Position = XMFLOAT3(4.0f, 0.0f, 0.0f);
        v[3].Normal = XMFLOAT3(0.0f, 0.0f, -1.0f);
        v[3].Diffuse = XMFLOAT4(1, 1, 1, 1);
        v[3].TexCoord = XMFLOAT2(x + w, y + h);

        Renderer::GetDeviceContext()->Unmap(
            m_VertexBuffer,
            0);
    }

    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;

    Renderer::GetDeviceContext()->IASetVertexBuffers(
        0,
        1,
        &m_VertexBuffer,
        &stride,
        &offset);

    Renderer::GetDeviceContext()->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    Renderer::GetDeviceContext()->Draw(4, 0);
}


