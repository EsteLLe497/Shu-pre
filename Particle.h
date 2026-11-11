#pragma once
#include"GameObject.h"
			

class Particle :public GameObject
{
private:

	ID3D11Buffer* m_VertexBuffer;
	ID3D11InputLayout* m_VertexLayout;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;

	ID3D11ShaderResourceView* m_LaunchTexture;
	ID3D11ShaderResourceView* m_ToraTexture;

	enum PARTICLE_TYPE
	{
		PARTICLE_LAUNCH,
		PARTICLE_EXPLOSION,
	};
	// 変数
	struct PARTICLE
	{
		bool	Enable;
		PARTICLE_TYPE Type;
		Vector3	Position;		// 位置
		Vector3	Velocity;		// 速度
		int		Life;		// 寿命
	};

	static const int PARTICLE_MAX = 50; // パーティクルの数
	PARTICLE	m_Particle[PARTICLE_MAX];

	void CreateExplosion(Vector3 pos);

public:
	void Init();
	void Uninit();
	void Update();
	void Draw();
};

