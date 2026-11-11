#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "Tree.h"
#include "Camera.h"
#include "Shadow.h"

 ID3D11ShaderResourceView* Tree:: m_Texture = nullptr;

void Tree::Init()
{
    m_Layer = 3;

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

    if (m_Texture == nullptr)
    {
        TexMetadata metadata;
        ScratchImage image;
        LoadFromWICFile(L"asset\\bill_board\\tree.png", WIC_FLAGS_NONE, &metadata, image);
        CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(),
            image.GetImageCount(), metadata, &m_Texture);
        assert(m_Texture);
    }


    m_Shadow = Manager::AddGameObj<Shadow>();
    m_Shadow->SetScale({ 20.0f ,20.0f ,20.0f });
}
void Tree::Uninit()
{
    if (m_Texture != nullptr)
    {
        m_Texture->Release();
        m_Texture = nullptr;
    }
    m_VertexBuffer->Release();

    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();

    GameObject::Uninit();
}
void  Tree::Update()
{
    Vector3 shadowPos = m_Position;
    shadowPos.y = 0.01f;
    m_Shadow->SetPosition(shadowPos);

    GameObject::Update();
}
void  Tree::Draw()
{
    Camera* camera = Manager::GetGameObj<Camera>();
    if (!camera->CheckView(m_Position))
        return;

    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);

    XMMATRIX view = camera->GetViewMatrix();
    XMMATRIX invView = XMMatrixInverse(NULL, view);
    invView.r[3].m128_f32[0] = 0.0f;
    invView.r[3].m128_f32[1] = 0.0f;
    invView.r[3].m128_f32[2] = 0.0f;

    XMMATRIX world, scale, rot, trans;
    scale = XMMatrixScaling(m_Scale.x, m_Scale.y, m_Scale.z);
    rot = XMMatrixRotationRollPitchYaw(m_Rotation.x, m_Rotation.y, m_Rotation.z);
    trans = XMMatrixTranslation(m_Position.x, m_Position.y, m_Position.z);
    world = scale * invView * rot * trans;

    Renderer::SetWorldMatrix(world);

    MATERIAL material{};
    material.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.TextureEnable = true;
    Renderer::SetMaterial(material);

    Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &m_Texture);

    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    Renderer::GetDeviceContext()->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);

    Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    Renderer::GetDeviceContext()->Draw(4, 0);

    GameObject::Draw();
}
