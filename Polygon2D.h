#pragma once
#include "GameObject.h"
class Polygon2D :public GameObject
{
private:
	ID3D11Buffer* m_VertexBuffer;
	ID3D11InputLayout* m_VertexLayout;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;

	ID3D11ShaderResourceView* m_Texture;
public:
	void Init() override {}
	void Init(float x, float y, float Width, float Height, const WCHAR* TextureName);
	void Uninit();
	void Update();
	void Draw();
};

