#pragma once
#include "GameObject.h"
class Score :public GameObject
{
private:
	int m_Score;
	ID3D11Buffer* m_VertexBuffer;
	ID3D11InputLayout* m_VertexLayout;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;

	ID3D11ShaderResourceView* m_Texture;
public:
	void Init();
	void Uninit();
	void Update();
	void Draw();

	void Add(int Score) { m_Score += Score; }
};

