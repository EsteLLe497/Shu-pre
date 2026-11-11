// =========================================================
// ファイルの情報[MeshField.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "MeshField.h"
#include "audio.h"

// =========================================================
// 地形の格子と、描画・当たり判定で共有する高さ表
// =========================================================
namespace
{
constexpr int vertexCount = 21;
constexpr int cellCount = vertexCount - 1;
constexpr float cellSize = 10.0f;
constexpr float gridCenter = cellCount * 0.5f;
constexpr int indexCount = vertexCount * 2 * cellCount + (cellCount - 1) * 2;

constexpr float fieldHeight[vertexCount][vertexCount] =
{
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,1.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,5.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},

};
}

// =========================================================
// 描画と同じ三角形を使って地形の高さを補間
// =========================================================
bool MeshField::tryGetHeight(float worldX, float worldZ, float& height) const
{
    if (m_Scale.x == 0.0f || m_Scale.z == 0.0f)
        return false;

    const float gridX = (worldX - m_Position.x) / (m_Scale.x * cellSize) + gridCenter;
    const float gridZ = gridCenter - (worldZ - m_Position.z) / (m_Scale.z * cellSize);
    if (!(gridX >= 0.0f && gridX <= cellCount && gridZ >= 0.0f && gridZ <= cellCount))
        return false;

    // 外周の頂点も最後のセルに含める。
    const int cellX = gridX < cellCount ? static_cast<int>(gridX) : cellCount - 1;
    const int cellZ = gridZ < cellCount ? static_cast<int>(gridZ) : cellCount - 1;
    const float offsetX = gridX - cellX;
    const float offsetZ = gridZ - cellZ;

    // ストリップの対角線は (1, 0) と (0, 1) を結ぶ。
    const float rightHeight = fieldHeight[cellZ][cellX + 1];
    const float bottomHeight = fieldHeight[cellZ + 1][cellX];
    float localHeight;
    if (offsetX + offsetZ <= 1.0f)
    {
        const float cornerHeight = fieldHeight[cellZ][cellX];
        localHeight = cornerHeight + offsetX * (rightHeight - cornerHeight)
            + offsetZ * (bottomHeight - cornerHeight);
    }
    else
    {
        const float cornerHeight = fieldHeight[cellZ + 1][cellX + 1];
        localHeight = cornerHeight + (1.0f - offsetX) * (bottomHeight - cornerHeight)
            + (1.0f - offsetZ) * (rightHeight - cornerHeight);
    }

    height = m_Position.y + localHeight * m_Scale.y;
    return true;
}

// =========================================================
// 地形メッシュの初期化
// =========================================================
void MeshField::Init()
{
    m_Layer = 1;

    // 頂点バッファ生成
    {
        VERTEX_3D vertex[vertexCount][vertexCount];

        for (int x = 0; x < vertexCount; x++)
        {
            for (int z = 0; z < vertexCount; z++)
            {
                vertex[x][z].Position = XMFLOAT3(
                    (x - gridCenter) * cellSize, fieldHeight[z][x], (gridCenter - z) * cellSize);
                vertex[x][z].Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
                vertex[x][z].Diffuse = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
                vertex[x][z].TexCoord = XMFLOAT2(x, z);
            }
        }

        //法線計算
        for (int x = 1; x < cellCount; x++)
        {
            for (int z = 1; z < cellCount; z++)
            {
                Vector3 vx, vz, vn;
                vx.x = vertex[x + 1][z].Position.x - vertex[x - 1][z].Position.x;
                vx.y = vertex[x + 1][z].Position.y - vertex[x - 1][z].Position.y;
                vx.z = vertex[x + 1][z].Position.z - vertex[x - 1][z].Position.z;

                vz.x = vertex[x][z - 1].Position.x - vertex[x][z + 1].Position.x;
                vz.y = vertex[x][z - 1].Position.y - vertex[x][z + 1].Position.y;
                vz.z = vertex[x][z - 1].Position.z - vertex[x][z + 1].Position.z;

                vn = Vector3::cross(vz, vx);//外積
                vn.normalize();//正規化（長さ1にする）

                vertex[x][z].Normal.x = vn.x;
                vertex[x][z].Normal.y = vn.y;
                vertex[x][z].Normal.z = vn.z;
            }
        }

        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(VERTEX_3D) * vertexCount * vertexCount;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = vertex;

        Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_VertexBuffer);
    }

    //インデックスバッファ生成
    {
        unsigned int index[indexCount];

        int i = 0;
        for (int x = 0; x < cellCount; x++)
        {
            for (int z = 0; z < vertexCount; z++)
            {
                index[i] = x * vertexCount + z;
                i++;

                index[i] = (x + 1) * vertexCount + z;
                i++;
            }

            if (x == cellCount - 1)
                break;

            //縮退ポリゴン
            index[i] = (x + 1) * vertexCount + cellCount;
            i++;

            index[i] = (x + 1) * vertexCount;
            i++;
        }

        D3D11_BUFFER_DESC bd{};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(unsigned int) * indexCount;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = index;
        Renderer::GetDevice()->CreateBuffer(&bd, &sd, &m_IndexBuffer);
    }

    
 
    // シェーダー読込
    Renderer::CreateVertexShader(&m_VertexShader, &m_VertexLayout,
        "shader\\FieldVS.cso");

    Renderer::CreatePixelShader(&m_PixelShader,
        "shader\\FieldPS.cso");

    TexMetadata metadata;
    ScratchImage image;
    LoadFromWICFile(L"asset\\texture\\Brick.jpg", WIC_FLAGS_NONE, &metadata, image);
    CreateShaderResourceView(Renderer::GetDevice(), image.GetImages(),
        image.GetImageCount(), metadata, &m_Texture);
    assert(m_Texture);

    //BGM
    Audio* bgm = AddGameComponent<Audio>(this);
    bgm->Load("asset\\Audio\\bgm.wav");
    //bgm->Play(true);

}
void MeshField::Uninit()
{
    m_VertexBuffer->Release();
    m_IndexBuffer->Release();


    m_VertexLayout->Release();
    m_VertexShader->Release();
    m_PixelShader->Release();

    GameObject::Uninit();
}
void  MeshField::Update()
{
    GameObject::Update();

}
void  MeshField::Draw()
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

    Renderer::GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}
