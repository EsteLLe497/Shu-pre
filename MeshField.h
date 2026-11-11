// =========================================================
// ファイルの情報[MeshField.h]
//
// 制作者:Masatora Tanaka        日付：2026/11/11
// =========================================================
#pragma once

#include"GameObject.h"
class MeshField :public GameObject
{
private:
	ID3D11Buffer* m_VertexBuffer;
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

	// 回転のない地形のワールド座標 XZ から、表面の高さを取得する。
	// 範囲外では false を返す。
	bool tryGetHeight(float worldX, float worldZ, float& height) const;
};