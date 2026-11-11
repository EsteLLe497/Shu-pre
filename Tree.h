#pragma once

#include"GameObject.h"

class Tree :public GameObject
{
private:
	//Vector3 m_Position{ 0.0f,0.0f,0.0f };
	//Vector3 m_Rotation{ 0.0f,0.0f,0.0f };
	//Vector3 m_Scale{ 1.0f,1.0f,1.0f };

	ID3D11Buffer* m_VertexBuffer;
	ID3D11InputLayout* m_VertexLayout;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;

	static ID3D11ShaderResourceView* m_Texture;

	GameObject* m_Shadow;

public:
	void Init();
	void Uninit();
	void Update();
	void Draw();
};
