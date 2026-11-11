#pragma once

#include"GameObject.h"

class Explosion :public GameObject
{
private:
	int m_Frame{ 0 };
	int m_Count{ 0 };

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

    static constexpr int frameCount = 16;
    static constexpr float frameDuration = 3.0f / 60.0f;
    void drawFrame(int frame, const XMMATRIX& view);
};
