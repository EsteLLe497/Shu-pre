#include "main.h"
#include "renderer.h"
#include "Wave.h"
#include "audio.h"


void Wave::Init()
{
    m_Layer = 1;

    // 頂点バッファ生成
    {
        

        for (int x = 0; x < 21; x++)
        {
            for (int z = 0; z < 21; z++)
            {
                m_vertex[x][z].Position = XMFLOAT3((x - 10) * 10.0f, 1.0, (z - 10) * -10.0f);
                m_vertex[x][z].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                m_vertex[x][z].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
                m_vertex[x][z].TexCoord = XMFLOAT2(x, z);
            }
        }

        //法線計算
        for (int x = 1; x < 20; x++)
        {
            for (int z = 1; z < 20; z++)
            {
                Vector3 vx, vz, vn;
                vx.x = m_vertex[x + 1][z].Position.x - m_vertex[x - 1][z].Position.x;
                vx.y = m_vertex[x + 1][z].Position.y - m_vertex[x - 1][z].Position.y;
                vx.z = m_vertex[x + 1][z].Position.z - m_vertex[x - 1][z].Position.z;

                vz.x = m_vertex[x][z - 1].Position.x - m_vertex[x][z + 1].Position.x;
                vz.y = m_vertex[x][z - 1].Position.y - m_vertex[x][z + 1].Position.y;
                vz.z = m_vertex[x][z - 1].Position.z - m_vertex[x][z + 1].Position.z;

                vn = Vector3::cross(vz, vx);//外積
                vn.normalize();//正規化（長さ1にする）

                m_vertex[x][z].Normal.x = vn.x;
                m_vertex[x][z].Normal.y = vn.y;
                m_vertex[x][z].Normal.z = vn.z;
            }
        }

        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = sizeof(VERTEX_3D) * 21 * 21;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = m_vertex;

        Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_VertexBuffer);
    }

    //インデックスバッファ生成
    {
        unsigned int index[(22 * 2) * 20 - 2];

        int i = 0;
        for (int x = 0; x < 20; x++)
        {
            for (int z = 0; z < 21; z++)
            {
                index[i] = x * 21 + z;
                i++;

                index[i] = (x + 1) * 21 + z;
                i++;
            }

            if (x == 19)
                break;

            //縮退ポリゴン
            index[i] = (x + 1) * 21 + 20;
            i++;

            index[i] = (x + 1) * 21;
            i++;
        }

        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(unsigned int) * ((22 * 2) * 20 - 2);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = index;
        Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_IndexBuffer);
    }



    // シェーダー読込
    Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout,
        "shader\\WaveVS.cso");

    Renderer::CreatePixelShader(&m_PixelShader,
        "shader\\WavePS.cso");

    TexMetadata metadata;
    ScratchImage image;
    LoadFromWICFile(L"asset\\model\\SkyDome\\sky.jpg", WIC_FLAGS_NONE, &metadata, image);
    CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(),
        image.GetImageCount(), metadata, &m_Texture);
    assert(m_Texture);

}
void Wave::Uninit()
{
    m_VertexBuffer->Release();
    m_IndexBuffer->Release();


    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();

    GameObject::Uninit();
}
void  Wave::Update()
{
    GameObject::Update();

    float dt = 1.0f / 60.0f;
    m_Time += dt;

    for (int x = 0; x < 21; x++)
    {
        for (int z = 0; z < 21; z++)
        {
            float lenght = sqrtf(x * x + z * z);
            //float y = 1.0f + sinf(lenght * 2.0f + m_Time * 1.2f/*speed*/);
            float y = 1.0f + sinf(x * 2.0f + m_Time * 1.2f/*speed*/)
                           + sinf(x * 2.0f + m_Time * 1.2f/*speed*/);

            m_vertex[x][z].Position = XMFLOAT3((x - 10) * 10.0f, y, (z - 10) * -10.0f);
        }
    }

    //法線計算
    for (int x = 1; x < 20; x++)
    {
        for (int z = 1; z < 20; z++)
        {
            Vector3 vx, vz, vn;
            vx.x = m_vertex[x + 1][z].Position.x - m_vertex[x - 1][z].Position.x;
            vx.y = m_vertex[x + 1][z].Position.y - m_vertex[x - 1][z].Position.y;
            vx.z = m_vertex[x + 1][z].Position.z - m_vertex[x - 1][z].Position.z;

            vz.x = m_vertex[x][z - 1].Position.x - m_vertex[x][z + 1].Position.x;
            vz.y = m_vertex[x][z - 1].Position.y - m_vertex[x][z + 1].Position.y;
            vz.z = m_vertex[x][z - 1].Position.z - m_vertex[x][z + 1].Position.z;

            vn = Vector3::cross(vz, vx);//外積
            vn.normalize();//正規化（長さ1にする）

            m_vertex[x][z].Normal.x = vn.x;
            m_vertex[x][z].Normal.y = vn.y;
            m_vertex[x][z].Normal.z = vn.z;
        }
    }

    D3D11_MAPPED_SUBRESOURCE msr;
    Renderer::GetDeviceContext()->Map(m_VertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);

    VERTEX_3D* vertex = (VERTEX_3D*)msr.pData;

    memcpy(vertex, m_vertex, sizeof(VERTEX_3D) * 21 * 21);

    Renderer::GetDeviceContext()->Unmap(m_VertexBuffer, 0);
}
void  Wave::Draw()
{
    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);

    MATERIAL material{};
    material.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.TextureEnable = true;
    Renderer::SetMaterial(material);

    Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &m_Texture);

    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    Renderer::GetDeviceContext()->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);
    Renderer::GetDeviceContext()->IASetIndexBuffer(m_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);


    Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    GameObject::Draw();

    //Renderer::GetDeviceContext()->Draw(21 * 21, 0);
    Renderer::GetDeviceContext()->DrawIndexed((22 * 2) * 20 - 2, 0, 0);
}
