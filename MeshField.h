// =========================================================
// ファイルの情報[MeshField.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include "GameObject.h"
#include <array>
class MeshField :public GameObject
{
public:
    static constexpr int GridSize = 101;
    using Heights = std::array<float, GridSize * GridSize>;
    const Heights& heights() const { return m_heights; }
    XMFLOAT4 worldGrid() const;
    unsigned int revision() const { return m_revision; }
    void setHeights(const Heights& heights);
    void sculpt(float x, float z, float radius, float amount, const float* targetHeight);

private:
    Heights m_heights{};
    unsigned int m_revision = 0;
    void updateVertices();
	ID3D11Buffer* m_VertexBuffer = nullptr;
	ID3D11Buffer* m_IndexBuffer;

	ID3D11InputLayout* m_VertexLayout;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;

	ID3D11ShaderResourceView* m_Texture;

public:
	void Init();
	void Uninit();
	void Update();
	void Draw();
    void drawGeometry();

	// 回転のない地形のワールド座標 XZ から、表面の高さを取得する。
	// 範囲外では false を返す。
	bool tryGetHeight(float worldX, float worldZ, float& height) const;
};