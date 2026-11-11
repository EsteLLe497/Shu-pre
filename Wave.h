#pragma once

#include"GameObject.h"
class Wave :public GameObject
{
private:
	Vector3 m_Position{ 0.0f,0.0f,0.0f };
	Vector3 m_Rotation{ 0.0f,0.0f,0.0f };
	Vector3 m_Scale{ 1.0f,1.0f,1.0f };

	ID3D11Buffer* m_VertexBuffer;
	ID3D11Buffer* m_IndexBuffer;

	ID3D11InputLayout* m_VertexLayout;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;

	ID3D11ShaderResourceView* m_Texture;

	VERTEX_3D m_vertex[21][21];
	float m_Time = 0.0f;

public:
	void Init();
	void Uninit();
	void Update();
	void Draw();
};
