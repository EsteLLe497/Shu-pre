// =========================================================
// ファイルの情報[MeshField.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include "GameObject.h"
#include <array>
#include <cstdint>
#include <iosfwd>
#include <vector>
#include <wrl/client.h>
class MeshField :public GameObject
{
public:
    static constexpr int GridSize = 101;
    using Heights = std::array<float, GridSize * GridSize>;
    static constexpr int PaintSize = 512;
    struct PaintWeights
    {
        std::uint8_t soil = 0;
        std::uint8_t grass = 0;
        bool operator==(const PaintWeights& rhs) const { return soil == rhs.soil && grass == rhs.grass; }
    };
    using MaterialPaint = std::vector<PaintWeights>;
    const Heights& heights() const { return m_heights; }
    XMFLOAT4 worldGrid() const;
    unsigned int revision() const { return m_revision; }
    void setHeights(const Heights& heights);
    void sculpt(float x, float z, float radius, float amount, const float* targetHeight);
    const MaterialPaint& materialPaint() const { return m_materialPaint; }
    void setMaterialPaint(MaterialPaint paint);
    bool paintMaterial(const Vector3& from, const Vector3& to, float radius, float opacity, bool grass);
    bool hasMaterialPaint() const;
    static bool readMaterialPaint(std::istream& input, MaterialPaint& paint);
    void writeMaterialPaint(std::ostream& output) const;

private:
    Heights m_heights{};
    MaterialPaint m_materialPaint = MaterialPaint(PaintSize * PaintSize);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_paintTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_paintView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_soilTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_grassTexture;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_paintSampler;
    void uploadMaterialPaint();
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
    // 独自シェーダーを使用する地形描画入口 担当:ozawa souma        日付:2026/11/28
    void drawGeometry();

	// 回転のない地形のワールド座標 XZ から、表面の高さを取得する。
	// 範囲外では false を返す。
	bool tryGetHeight(float worldX, float worldZ, float& height) const;
};