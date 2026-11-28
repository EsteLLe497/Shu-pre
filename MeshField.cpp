// =========================================================
// ファイルの情報[MeshField.cpp]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#include "main.h"
#include "renderer.h"
#include "MeshField.h"

// =========================================================
// 地形の格子と、描画・当たり判定で共有する高さ表
// =========================================================
namespace
{
constexpr int vertexCount = MeshField::GridSize;
constexpr int cellCount = vertexCount - 1;
constexpr float cellSize = 2.0f;
constexpr float gridCenter = cellCount * 0.5f;
constexpr int indexCount = vertexCount * 2 * cellCount + (cellCount - 1) * 2;

constexpr float fieldHeight[21][21] =
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
XMFLOAT4 MeshField::worldGrid() const
{
    const float stepX = m_Scale.x * cellSize, stepZ = -m_Scale.z * cellSize;
    return { m_Position.x - gridCenter * stepX, m_Position.z - gridCenter * stepZ, stepX, stepZ };
}

bool MeshField::tryGetHeight(float worldX, float worldZ, float& height) const
{
    if (m_Scale.x == 0.0f || m_Scale.z == 0.0f)
        return false;

    const auto grid = worldGrid();
    const float gridX = (worldX - grid.x) / grid.z;
    const float gridZ = (worldZ - grid.y) / grid.w;
    if (!(gridX >= 0.0f && gridX <= cellCount && gridZ >= 0.0f && gridZ <= cellCount))
        return false;

    // 外周の頂点も最後のセルに含める。
    const int cellX = gridX < cellCount ? static_cast<int>(gridX) : cellCount - 1;
    const int cellZ = gridZ < cellCount ? static_cast<int>(gridZ) : cellCount - 1;
    const float offsetX = gridX - cellX;
    const float offsetZ = gridZ - cellZ;

    // ストリップの対角線は (1, 0) と (0, 1) を結ぶ。
    const float rightHeight = m_heights[(cellZ) * vertexCount + (cellX + 1)];
    const float bottomHeight = m_heights[(cellZ + 1) * vertexCount + (cellX)];
    float localHeight;
    if (offsetX + offsetZ <= 1.0f)
    {
        const float cornerHeight = m_heights[(cellZ) * vertexCount + (cellX)];
        localHeight = cornerHeight + offsetX * (rightHeight - cornerHeight)
            + offsetZ * (bottomHeight - cornerHeight);
    }
    else
    {
        const float cornerHeight = m_heights[(cellZ + 1) * vertexCount + (cellX + 1)];
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
    // Resample the original 10-unit terrain without changing its triangle slopes.
    for (int z = 0; z < vertexCount; ++z)
    {
        for (int x = 0; x < vertexCount; ++x)
        {
            const int col = std::min(x / 5, 19);
            const int row = std::min(z / 5, 19);
            const float u = (x - col * 5) / 5.0f;
            const float v = (z - row * 5) / 5.0f;
            const float right = fieldHeight[row][col + 1];
            const float bottom = fieldHeight[row + 1][col];
            const float corner = u + v <= 1.0f ? fieldHeight[row][col] : fieldHeight[row + 1][col + 1];
            m_heights[z * vertexCount + x] = u + v <= 1.0f
                ? corner + u * (right - corner) + v * (bottom - corner)
                : corner + (1.0f - u) * (bottom - corner) + (1.0f - v) * (right - corner);
        }
    }
    updateVertices();

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


}
void MeshField::updateVertices()
{
    ++m_revision;
    std::vector<VERTEX_3D> vertices(vertexCount * vertexCount);
    const auto sample = [&](int x, int z) { return m_heights[z * vertexCount + x]; };
    for (int x = 0; x < vertexCount; ++x)
    {
        for (int z = 0; z < vertexCount; ++z)
        {
            auto& vertex = vertices[x * vertexCount + z];
            vertex.Position = { (x - gridCenter) * cellSize, sample(x, z), (gridCenter - z) * cellSize };
            const int left = std::max(0, x - 1), right = std::min(cellCount, x + 1);
            const int top = std::max(0, z - 1), bottom = std::min(cellCount, z + 1);
            const float dx = (sample(right, z) - sample(left, z)) / ((right - left) * cellSize);
            const float dz = (sample(x, top) - sample(x, bottom)) / ((bottom - top) * cellSize);
            XMStoreFloat3(&vertex.Normal, XMVector3Normalize(XMVectorSet(-dx, 1.0f, -dz, 0.0f)));
            vertex.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
            vertex.TexCoord = { x / 5.0f, z / 5.0f };
        }
    }

    if (m_VertexBuffer)
    {
        Renderer::GetDeviceContext()->UpdateSubresource(m_VertexBuffer, 0, nullptr, vertices.data(), 0, 0);
        return;
    }
    D3D11_BUFFER_DESC description{};
    description.Usage = D3D11_USAGE_DEFAULT;
    description.ByteWidth = static_cast<UINT>(sizeof(VERTEX_3D) * vertices.size());
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = vertices.data();
    Renderer::GetDevice()->CreateBuffer(&description, &data, &m_VertexBuffer);
}

void MeshField::setHeights(const Heights& heights)
{
    m_heights = heights;
    updateVertices();
}

void MeshField::sculpt(float x, float z, float radius, float amount, const float* targetHeight)
{
    bool changed = false;
    for (int row = 0; row < vertexCount; ++row)
    {
        for (int col = 0; col < vertexCount; ++col)
        {
            const float distance = hypotf((col - gridCenter) * cellSize - x, (gridCenter - row) * cellSize - z);
            if (distance >= radius) continue;
            const float t = 1.0f - distance / radius;
            const float weight = t * t * (3.0f - 2.0f * t);
            float& height = m_heights[row * vertexCount + col];
            height += targetHeight ? (*targetHeight - height) * std::min(1.0f, amount * weight) : amount * weight;
            height = std::max(-30.0f, std::min(30.0f, height));
            changed = true;
        }
    }
    if (changed) updateVertices();
}

void MeshField::Uninit()
{
    if (m_VertexBuffer) m_VertexBuffer->Release();
    m_IndexBuffer->Release();
    if (m_Texture) m_Texture->Release();


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
    if (!m_VertexBuffer) return;
    Renderer::GetDeviceContext()->IASetInputLayout(m_VertexLayout);

    Renderer::GetDeviceContext()->VSSetShader(m_VertexShader, NULL, 0);
    Renderer::GetDeviceContext()->PSSetShader(m_PixelShader, NULL, 0);

    MATERIAL material{};
    material.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.Ambient = { 1.0f, 1.0f, 1.0f, 1.0f };
    material.TextureEnable = true;
    Renderer::SetMaterial(material);

    Renderer::GetDeviceContext()->PSSetShaderResources(0, 1, &m_Texture);
    drawGeometry();
}

void MeshField::drawGeometry()
{
    if (!m_VertexBuffer) return;
    UINT stride = sizeof(VERTEX_3D);
    UINT offset = 0;
    Renderer::GetDeviceContext()->IASetVertexBuffers(0, 1, &m_VertexBuffer, &stride, &offset);
    Renderer::GetDeviceContext()->IASetIndexBuffer(m_IndexBuffer, DXGI_FORMAT_R32_UINT, 0);


    Renderer::GetDeviceContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    GameObject::Draw();

    Renderer::GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}
